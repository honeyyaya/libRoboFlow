#!/usr/bin/env python3
"""
多轮 E2E 基线回归（默认 stable + UI），重点统计 v4l2->present_submit 与主链路尾延迟。
"""
from __future__ import annotations

import argparse
import datetime as dt
import os
import re
import statistics
import subprocess
import sys
from typing import Dict, List

RE_SIG = re.compile(r"^E2E-SIGNATURE .*$", re.MULTILINE)
RE_SUM = re.compile(r"\[E2E_SUMMARY\].*?p50=([0-9.]+)ms p95=([0-9.]+)ms p99=([0-9.]+)ms n=(\d+)")
RE_PRIMARY_V4L2_PRESENT = re.compile(r"#1 v4l2->present_submit:\s*p50=([0-9.]+)ms p95=([0-9.]+)ms p99=([0-9.]+)ms")
RE_TAIL = re.compile(r"\[E2E_TAIL\] over_(\d+)ms=(\d+)/(\d+) \(([0-9.]+)%\)")


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("--runs", type=int, default=10)
    p.add_argument("--frames", type=int, default=400)
    p.add_argument("--timeout-sec", type=int, default=25)
    p.add_argument("--camera", default="")
    p.add_argument("--profile", default="stable", choices=["stable", "balanced", "aggressive", "off"])
    p.add_argument("--warn-ms", type=int, default=30)
    p.add_argument("--skip-first-pairs", type=int, default=30)
    p.add_argument("--cpu-pin-mode", default="auto", choices=["auto", "off"])
    p.add_argument("--sched-mode", default="auto", choices=["auto", "rr", "off"])
    return p.parse_args()


def pct(vals: List[float], p: float) -> float:
    s = sorted(vals)
    i = min(len(s) - 1, max(0, int(round((len(s) - 1) * p))))
    return s[i]


def stats_line(name: str, vals: List[float]) -> str:
    if not vals:
        return f"{name}: n=0"
    return (
        f"{name}: n={len(vals)} min={min(vals):.3f} p50={pct(vals, 0.50):.3f} "
        f"p95={pct(vals, 0.95):.3f} p99={pct(vals, 0.99):.3f} max={max(vals):.3f} "
        f"mean={statistics.mean(vals):.3f}"
    )


def run_once(root: str, idx: int, args: argparse.Namespace) -> Dict[str, object]:
    env = os.environ.copy()
    env["E2E_UI_MODE"] = "1"
    env["E2E_FRAMES"] = str(args.frames)
    env["E2E_PULL_TIMEOUT_SEC"] = str(args.timeout_sec)
    env["E2E_LOWLAT_PROFILE"] = args.profile
    env["PUSH_LOWLATENCY_PROFILE"] = args.profile
    env["E2E_CPU_PIN_MODE"] = args.cpu_pin_mode
    env["E2E_SCHED_MODE"] = args.sched_mode
    env["WEBRTC_PULL_UI_POLL_SLEEP_MS"] = "0"
    env["E2E_VIDEO_STATS"] = "0"
    env["E2E_SKIP_FIRST_PAIRS"] = str(args.skip_first_pairs)
    env["WEBRTC_E2E_WARN_MS"] = str(args.warn_ms)
    if args.profile == "aggressive":
        env["RFLOW_ZERO_PLAYOUT_MIN_PACING_MS"] = "0"
        env["RFLOW_MAX_DECODE_QUEUE_SIZE"] = "4"
    elif args.profile == "balanced":
        env["RFLOW_ZERO_PLAYOUT_MIN_PACING_MS"] = "1"
        env["RFLOW_MAX_DECODE_QUEUE_SIZE"] = "6"
    elif args.profile == "stable":
        env["RFLOW_ZERO_PLAYOUT_MIN_PACING_MS"] = "2"
        env["RFLOW_MAX_DECODE_QUEUE_SIZE"] = "8"
    env["RFLOW_ENABLE_DECODE_QUEUE_GUARD"] = env.get("RFLOW_ENABLE_DECODE_QUEUE_GUARD", "1")
    env["RFLOW_DECODE_QUEUE_GUARD_CAP"] = env.get("RFLOW_DECODE_QUEUE_GUARD_CAP", "8")

    cam = args.camera.strip()
    cmd = ["bash", "-lc", f"./scripts/pull.sh --e2e 127.0.0.1:8765 livestream {cam}" if cam else "./scripts/pull.sh --e2e"]
    proc = subprocess.run(cmd, cwd=root, env=env, text=True, capture_output=True)
    out = proc.stdout + proc.stderr

    ts = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = os.path.join(root, "build", "e2e_baseline")
    os.makedirs(out_dir, exist_ok=True)
    run_log = os.path.join(out_dir, f"run{idx:02d}_{ts}.log")
    with open(run_log, "w", encoding="utf-8") as f:
        f.write(out)

    m_sig = RE_SIG.search(out)
    m_sum = RE_SUM.search(out)
    m_pri = RE_PRIMARY_V4L2_PRESENT.search(out)
    m_tail = RE_TAIL.search(out)
    ret: Dict[str, object] = {"ok": False, "rc": proc.returncode, "run_log": run_log}
    if m_sig:
        ret["signature"] = m_sig.group(0)
    if m_sum:
        ret["mjpeg_sink_p50"] = float(m_sum.group(1))
        ret["mjpeg_sink_p95"] = float(m_sum.group(2))
        ret["mjpeg_sink_p99"] = float(m_sum.group(3))
        ret["pairs"] = int(m_sum.group(4))
    if m_pri:
        ret["v4l2_present_p50"] = float(m_pri.group(1))
        ret["v4l2_present_p95"] = float(m_pri.group(2))
        ret["v4l2_present_p99"] = float(m_pri.group(3))
    if m_tail:
        ret["tail_warn_ms"] = int(m_tail.group(1))
        ret["tail_ratio"] = float(m_tail.group(4))
    ret["ok"] = bool(m_sum and m_pri and proc.returncode == 0)
    return ret


def main() -> int:
    args = parse_args()
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    all_res: List[Dict[str, object]] = []
    for i in range(1, args.runs + 1):
        r = run_once(root, i, args)
        all_res.append(r)
        if r.get("ok"):
            print(
                "run#{:02d} ok  v4l2->present p50={:.3f} p95={:.3f} p99={:.3f}  mjpeg->sink p99={:.3f}".format(
                    i,
                    float(r.get("v4l2_present_p50", -1)),
                    float(r.get("v4l2_present_p95", -1)),
                    float(r.get("v4l2_present_p99", -1)),
                    float(r.get("mjpeg_sink_p99", -1)),
                )
            )
        else:
            print(f"run#{i:02d} failed rc={r.get('rc')} log={r.get('run_log')}")

    oks = [x for x in all_res if x.get("ok")]
    if not oks:
        print("no successful runs")
        return 1
    print(f"success={len(oks)}/{len(all_res)}")
    print(stats_line("v4l2->present p50(ms)", [float(x["v4l2_present_p50"]) for x in oks]))
    print(stats_line("v4l2->present p95(ms)", [float(x["v4l2_present_p95"]) for x in oks]))
    print(stats_line("v4l2->present p99(ms)", [float(x["v4l2_present_p99"]) for x in oks]))
    print(stats_line("mjpeg->sink p99(ms)", [float(x["mjpeg_sink_p99"]) for x in oks]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

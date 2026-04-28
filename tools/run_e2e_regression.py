#!/usr/bin/env python3
"""
多轮本机 E2E 回归：重复执行 ./scripts/pull.sh --e2e，并汇总 p50/p95/p99 与尾延迟比例。
"""
from __future__ import annotations

import argparse
import datetime as dt
import os
import re
import shutil
import statistics
import subprocess
import sys
from typing import Dict, List

RE_SIG = re.compile(r"^E2E-SIGNATURE .*$", re.MULTILINE)
RE_SUM = re.compile(r"\[E2E_SUMMARY\].*?p50=([0-9.]+)ms p95=([0-9.]+)ms p99=([0-9.]+)ms n=(\d+)")
RE_TAIL = re.compile(r"\[E2E_TAIL\] over_(\d+)ms=(\d+)/(\d+) \(([0-9.]+)%\)")


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("--runs", type=int, default=5)
    p.add_argument("--frames", type=int, default=400)
    p.add_argument("--warn-ms", type=int, default=30)
    p.add_argument("--pull-profile", default="aggressive", choices=["aggressive", "balanced", "off"])
    p.add_argument("--push-profile", default="aggressive", choices=["aggressive", "balanced", "off"])
    p.add_argument("--cpu-pin-mode", default="auto", choices=["auto", "off"])
    return p.parse_args()


def ms_stats(v: List[float]) -> str:
    if not v:
        return "n=0"
    s = sorted(v)
    n = len(s)
    p50 = s[(n - 1) * 50 // 100]
    p95 = s[(n - 1) * 95 // 100]
    p99 = s[(n - 1) * 99 // 100]
    return "n={} min={:.3f} p50={:.3f} p95={:.3f} p99={:.3f} max={:.3f} mean={:.3f}".format(
        n, s[0], p50, p95, p99, s[-1], statistics.mean(s)
    )


def run_once(root: str, i: int, args: argparse.Namespace) -> Dict[str, object]:
    env = os.environ.copy()
    env["E2E_FRAMES"] = str(args.frames)
    env["WEBRTC_E2E_WARN_MS"] = str(args.warn_ms)
    env["E2E_LOWLAT_PROFILE"] = args.pull_profile
    env["PUSH_LOWLATENCY_PROFILE"] = args.push_profile
    env["E2E_CPU_PIN_MODE"] = args.cpu_pin_mode
    if args.pull_profile == "aggressive":
        env["RFLOW_ZERO_PLAYOUT_MIN_PACING_MS"] = "0"
        env["RFLOW_MAX_DECODE_QUEUE_SIZE"] = "4"
    elif args.pull_profile == "balanced":
        env["RFLOW_ZERO_PLAYOUT_MIN_PACING_MS"] = "1"
        env["RFLOW_MAX_DECODE_QUEUE_SIZE"] = "6"
    env["RFLOW_ENABLE_DECODE_QUEUE_GUARD"] = env.get("RFLOW_ENABLE_DECODE_QUEUE_GUARD", "1")
    env["RFLOW_DECODE_QUEUE_GUARD_CAP"] = env.get("RFLOW_DECODE_QUEUE_GUARD_CAP", "8")
    proc = subprocess.run(["bash", "-lc", "./scripts/pull.sh --e2e"], cwd=root, env=env, text=True, capture_output=True)
    out = proc.stdout + proc.stderr

    ts = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = os.path.join(root, "build", "e2e_regression")
    os.makedirs(out_dir, exist_ok=True)
    run_log = os.path.join(out_dir, "run{}_{}.log".format(i, ts))
    with open(run_log, "w", encoding="utf-8") as f:
        f.write(out)
    for stem in ("push", "pull"):
        src = os.path.join(root, "build", f"e2e_last_{stem}.log")
        dst = os.path.join(out_dir, f"run{i}_{ts}_{stem}.log")
        if os.path.exists(src):
            shutil.copy2(src, dst)

    sig_m = RE_SIG.search(out)
    sum_m = RE_SUM.search(out)
    tail_m = RE_TAIL.search(out)
    result: Dict[str, object] = {"ok": proc.returncode == 0 and bool(sum_m), "rc": proc.returncode, "run_log": run_log}
    result["signature"] = sig_m.group(0) if sig_m else "(missing signature)"
    if sum_m:
        result["p50"] = float(sum_m.group(1))
        result["p95"] = float(sum_m.group(2))
        result["p99"] = float(sum_m.group(3))
        result["n"] = int(sum_m.group(4))
    if tail_m:
        result["tail_ratio"] = float(tail_m.group(4))
    return result


def main() -> int:
    args = parse_args()
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    all_res: List[Dict[str, object]] = []
    for i in range(1, args.runs + 1):
        r = run_once(root, i, args)
        all_res.append(r)
        print(f"run#{i:02d} {'ok' if r.get('ok') else 'failed'} rc={r.get('rc')}")
    ok = [r for r in all_res if r.get("ok")]
    if not ok:
        print("all runs failed")
        return 1
    print("valid_runs={}/{}".format(len(ok), len(all_res)))
    print("p50(ms): {}".format(ms_stats([float(r["p50"]) for r in ok])))
    print("p95(ms): {}".format(ms_stats([float(r["p95"]) for r in ok])))
    print("p99(ms): {}".format(ms_stats([float(r["p99"]) for r in ok])))
    return 0


if __name__ == "__main__":
    sys.exit(main())

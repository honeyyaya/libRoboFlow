#!/usr/bin/env python3
"""按 trace_id 对齐 push/pull/ui，输出分阶段时延与抖动贡献。"""
from __future__ import annotations

import argparse
import re
import statistics
from typing import Dict, List, Tuple

RE_TX = re.compile(
    r"\[E2E_TX\] rtp_ts=(\d+) trace_id=(\d+) t_mjpeg_input_us=(-?\d+) t_v4l2_us=(-?\d+) "
    r"t_on_frame_us=(-?\d+) t_enc_done_us=(-?\d+) t_after_onencoded_us=(-?\d+)"
)
RE_RX = re.compile(
    r"\[E2E_RX\] rtp_ts=(\d+) trace_id=(\d+) frame_id=(\d+) t_sink_us=(-?\d+) "
    r"(?:wall_utc_ms=(-?\d+) )?t_argb_done_us=(-?\d+) t_callback_done_us=(-?\d+)"
)
RE_UI = re.compile(r"\[E2E_UI\] trace_id=(\d+) t_sink_callback_done_us=(-?\d+) t_present_submit_us=(-?\d+)")


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("push_log")
    p.add_argument("pull_log")
    p.add_argument("--skip-first", type=int, default=30)
    return p.parse_args()


def pct(vals: List[int], p: float) -> float:
    s = sorted(vals)
    i = min(len(s) - 1, max(0, int(round((len(s) - 1) * p))))
    return float(s[i])


def line_for(name: str, vals: List[int]) -> str:
    if not vals:
        return f"{name:28s} n=0"
    p50 = pct(vals, 0.50) / 1000.0
    p95 = pct(vals, 0.95) / 1000.0
    p99 = pct(vals, 0.99) / 1000.0
    jitter = (pct(vals, 0.99) - pct(vals, 0.50)) / 1000.0
    mean_ms = statistics.mean(vals) / 1000.0
    return f"{name:28s} n={len(vals):4d}  p50={p50:8.3f}ms  p95={p95:8.3f}ms  p99={p99:8.3f}ms  p99-p50={jitter:8.3f}ms  mean={mean_ms:8.3f}ms"


def append_if_nonneg(d: Dict[str, List[int]], key: str, a: int, b: int) -> None:
    if a > 0 and b >= a:
        d[key].append(b - a)


def main() -> int:
    args = parse_args()
    tx_by_trace: Dict[int, Tuple[int, int, int, int, int]] = {}
    rx_by_trace: Dict[int, Tuple[int, int]] = {}
    ui_by_trace: Dict[int, int] = {}
    with open(args.push_log, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = RE_TX.search(line)
            if m:
                tid = int(m.group(2))
                tx_by_trace[tid] = (int(m.group(4)), int(m.group(3)), int(m.group(5)), int(m.group(6)), int(m.group(7)))
    with open(args.pull_log, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = RE_RX.search(line)
            if m:
                tid = int(m.group(2))
                rx_by_trace.setdefault(tid, (int(m.group(4)), int(m.group(7))))
                continue
            m = RE_UI.search(line)
            if m:
                ui_by_trace.setdefault(int(m.group(1)), int(m.group(3)))

    tids = sorted(set(tx_by_trace) & set(rx_by_trace))
    if not tids:
        print("no trace_id pairs found")
        return 1
    if args.skip_first > 0 and len(tids) > args.skip_first:
        tids = tids[args.skip_first:]
    buckets: Dict[str, List[int]] = {k: [] for k in [
        "A v4l2->mjpeg_input", "B mjpeg->on_frame", "C on_frame->enc_done", "D enc_done->after_onenc",
        "E after_onenc->sink", "F sink->callback_done", "G callback->present_submit",
        "T v4l2->present_submit", "T2 mjpeg->sink",
    ]}
    for tid in tids:
        v4l2, mjpeg, onf, enc, after = tx_by_trace[tid]
        sink, cb_done = rx_by_trace[tid]
        append_if_nonneg(buckets, "A v4l2->mjpeg_input", v4l2, mjpeg)
        append_if_nonneg(buckets, "B mjpeg->on_frame", mjpeg, onf)
        append_if_nonneg(buckets, "C on_frame->enc_done", onf, enc)
        append_if_nonneg(buckets, "D enc_done->after_onenc", enc, after)
        append_if_nonneg(buckets, "E after_onenc->sink", after, sink)
        append_if_nonneg(buckets, "F sink->callback_done", sink, cb_done)
        append_if_nonneg(buckets, "T2 mjpeg->sink", mjpeg, sink)
        if tid in ui_by_trace:
            append_if_nonneg(buckets, "G callback->present_submit", cb_done, ui_by_trace[tid])
            append_if_nonneg(buckets, "T v4l2->present_submit", v4l2, ui_by_trace[tid])
    print(f"pairs={len(tids)} (after skip-first={args.skip_first})")
    for k in buckets:
        print(line_for(k, buckets[k]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""
合并推流/拉流两份日志里的 [E2E_TX] / [E2E_RX]，配对后输出多段延迟。

配对优先级（可信度高 -> 低）：
  1) trace_id：发送端与接收端 VideoFrame::id()（需 MPP 解码传播 tracking id）
  2) RTP 时间戳完全一致
  3) 推断固定 RTP 偏移 - 易误配帧，脚本会明确警告
"""
from __future__ import annotations

import argparse
import os
import re
import statistics
from collections import defaultdict
from typing import Tuple

RE_TX = re.compile(
    r"\[E2E_TX\] rtp_ts=(\d+) trace_id=(\d+) t_mjpeg_input_us=(-?\d+) t_v4l2_us=(-?\d+) "
    r"t_on_frame_us=(-?\d+) t_enc_done_us=(-?\d+) t_after_onencoded_us=(-?\d+) wall_utc_ms=(-?\d+)"
)
RE_TX_OLD = re.compile(
    r"\[E2E_TX\] rtp_ts=(\d+) trace_id=(\d+) t_mjpeg_input_us=(-?\d+) t_v4l2_us=(-?\d+) "
    r"t_on_frame_us=(-?\d+) t_enc_done_us=(-?\d+) t_after_onencoded_us=(-?\d+)"
)
RE_RX = re.compile(
    r"\[E2E_RX\] rtp_ts=(\d+) trace_id=(\d+) frame_id=(\d+) t_sink_us=(-?\d+) wall_utc_ms=(-?\d+) "
    r"t_argb_done_us=(-?\d+) t_callback_done_us=(-?\d+)"
)
RE_RX_NO_WALL = re.compile(
    r"\[E2E_RX\] rtp_ts=(\d+) trace_id=(\d+) frame_id=(\d+) t_sink_us=(-?\d+) "
    r"t_argb_done_us=(-?\d+) t_callback_done_us=(-?\d+)"
)
RE_RX_LEGACY = re.compile(
    r"\[E2E_RX\] rtp_ts=(\d+) frame_id=(\d+) t_sink_us=(-?\d+) "
    r"t_argb_done_us=(-?\d+) t_callback_done_us=(-?\d+)"
)
RE_UI = re.compile(r"\[E2E_UI\] trace_id=(\d+) t_sink_callback_done_us=(-?\d+) t_present_submit_us=(-?\d+)")
U32 = 1 << 32

TxTuple = Tuple[int, int, int, int, int, int, int]
RxTuple = Tuple[int, int, int, int]


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="合并推流/拉流日志，输出 E2E 统计。")
    p.add_argument("push_log", help="推流日志路径")
    p.add_argument("pull_log", help="拉流日志路径")
    p.add_argument("--dump-outliers", type=int, default=0, metavar="N")
    p.add_argument("--skip-first-pairs", type=int, default=0, metavar="N")
    return p.parse_args()


def infer_rtp_offset(tx_keys: list[int], rx_keys: list[int]) -> int | None:
    if not tx_keys or not rx_keys:
        return None
    tx_probe = tx_keys[:120]
    rx_probe = rx_keys[:120]
    score: defaultdict[int, int] = defaultdict(int)
    for tx in tx_probe:
        for rx in rx_probe:
            score[(rx - tx) % U32] += 1
    if not score:
        return None
    best_off, best_cnt = max(score.items(), key=lambda kv: kv[1])
    return best_off if best_cnt >= 4 else None


def percentile_sorted(sorted_values: list[int], p: float) -> int:
    n = len(sorted_values)
    if n == 0:
        return 0
    i = min(n - 1, max(0, int(round((n - 1) * p))))
    return sorted_values[i]


def print_stats(name: str, values: list[int], note: str = "") -> None:
    if not values:
        return
    values.sort()
    mean_v = statistics.mean(values)
    p50 = percentile_sorted(values, 0.50)
    p95 = percentile_sorted(values, 0.95)
    p99 = percentile_sorted(values, 0.99)
    suffix = f"  {note}" if note else ""
    print(
        f"{name}: n={len(values)} "
        f"us[min={values[0]} p50={p50} p95={p95} p99={p99} max={values[-1]} mean={mean_v:.1f}] "
        f"ms[min={values[0]/1000.0:.3f} p50={p50/1000.0:.3f} "
        f"p95={p95/1000.0:.3f} p99={p99/1000.0:.3f} max={values[-1]/1000.0:.3f} mean={mean_v/1000.0:.3f}]"
        f"{suffix}"
    )


def print_stats_wall_ms(name: str, values: list[int], note: str = "") -> None:
    if not values:
        return
    values.sort()
    mean_v = statistics.mean(values)
    p50 = percentile_sorted(values, 0.50)
    p95 = percentile_sorted(values, 0.95)
    p99 = percentile_sorted(values, 0.99)
    suffix = f"  {note}" if note else ""
    print(
        f"{name}: n={len(values)} "
        f"ms[min={values[0]} p50={p50} p95={p95} p99={p99} max={values[-1]} mean={mean_v:.1f}]"
        f"{suffix}"
    )


def collect_deltas(tx: TxTuple, rx: RxTuple) -> dict[str, int]:
    _trace_id, t_mjpeg, t_v4l2, t_onf, _t_enc_done, _t_after, wall_tx = tx
    t_sink, t_argb_done, t_cb_done, wall_rx = rx
    out: dict[str, int] = {}
    if wall_tx > 0 and wall_rx > 0:
        out["e2e_wall_utc_mjpeg_to_sink"] = wall_rx - wall_tx
    if t_mjpeg > 0 and t_sink >= t_mjpeg:
        out["e2e_mjpeg_input_to_sink"] = t_sink - t_mjpeg
    if t_v4l2 > 0 and t_sink >= t_v4l2:
        out["e2e_v4l2_to_sink"] = t_sink - t_v4l2
    if t_onf > 0 and t_sink >= t_onf:
        out["e2e_onframe_to_sink"] = t_sink - t_onf
    if t_mjpeg > 0 and t_argb_done >= t_mjpeg:
        out["e2e_mjpeg_input_to_argb_done"] = t_argb_done - t_mjpeg
    if t_mjpeg > 0 and t_cb_done >= t_mjpeg:
        out["e2e_mjpeg_input_to_callback_done"] = t_cb_done - t_mjpeg
    if t_argb_done >= t_sink:
        out["rx_sink_to_argb_done"] = t_argb_done - t_sink
    if t_cb_done >= t_argb_done:
        out["rx_argb_done_to_callback_done"] = t_cb_done - t_argb_done
    return out


def empty_buckets() -> dict[str, list[int]]:
    return {k: [] for k in [
        "e2e_wall_utc_mjpeg_to_sink",
        "e2e_v4l2_to_sink",
        "e2e_v4l2_to_present_submit",
        "e2e_mjpeg_input_to_sink",
        "e2e_mjpeg_input_to_present_submit",
        "e2e_onframe_to_sink",
        "e2e_mjpeg_input_to_argb_done",
        "e2e_mjpeg_input_to_callback_done",
        "rx_sink_callback_to_present_submit",
        "rx_sink_to_argb_done",
        "rx_argb_done_to_callback_done",
    ]}


def flush_bucket_print(buckets: dict[str, list[int]], note: str) -> None:
    print_stats_wall_ms("e2e_wall_utc_mjpeg_to_sink", buckets["e2e_wall_utc_mjpeg_to_sink"], note)
    print_stats("e2e_v4l2_to_sink", buckets["e2e_v4l2_to_sink"], note)
    print_stats("e2e_v4l2_to_present_submit", buckets["e2e_v4l2_to_present_submit"], note)
    print_stats("e2e_mjpeg_input_to_sink", buckets["e2e_mjpeg_input_to_sink"], note)
    print_stats("e2e_mjpeg_input_to_present_submit", buckets["e2e_mjpeg_input_to_present_submit"], note)
    print_stats("e2e_onframe_to_sink", buckets["e2e_onframe_to_sink"], note)
    print_stats("e2e_mjpeg_input_to_argb_done", buckets["e2e_mjpeg_input_to_argb_done"], note)
    print_stats("e2e_mjpeg_input_to_callback_done", buckets["e2e_mjpeg_input_to_callback_done"], note)
    print_stats("rx_sink_callback_to_present_submit", buckets["rx_sink_callback_to_present_submit"], note)
    print_stats("rx_sink_to_argb_done", buckets["rx_sink_to_argb_done"], note)
    print_stats("rx_argb_done_to_callback_done", buckets["rx_argb_done_to_callback_done"], note)


def parse_tx_line(line: str) -> tuple[int, TxTuple] | None:
    m = RE_TX.search(line)
    if m:
        return int(m.group(1)), (
            int(m.group(2)),
            int(m.group(3)),
            int(m.group(4)),
            int(m.group(5)),
            int(m.group(6)),
            int(m.group(7)),
            int(m.group(8)),
        )
    m = RE_TX_OLD.search(line)
    if m:
        return int(m.group(1)), (
            int(m.group(2)),
            int(m.group(3)),
            int(m.group(4)),
            int(m.group(5)),
            int(m.group(6)),
            int(m.group(7)),
            -1,
        )
    return None


def main() -> int:
    args = parse_args()
    tx_by_rtp: dict[int, TxTuple] = {}
    tx_by_trace: dict[int, TxTuple] = {}
    tx_rtp_by_trace: dict[int, int] = {}
    with open(args.push_log, encoding="utf-8", errors="replace") as f:
        for line in f:
            parsed = parse_tx_line(line)
            if parsed:
                rtp, tup = parsed
                tx_by_rtp[rtp] = tup
                if tup[0] != 0:
                    tx_by_trace[tup[0]] = tup
                    tx_rtp_by_trace[tup[0]] = rtp

    rx_first_rtp: dict[int, RxTuple] = {}
    rx_first_trace: dict[int, RxTuple] = {}
    rx_rtp_by_trace: dict[int, int] = {}
    ui_present_by_trace: dict[int, int] = {}
    with open(args.pull_log, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = RE_RX.search(line)
            if m:
                rtp = int(m.group(1))
                trace = int(m.group(2))
                rx = (int(m.group(4)), int(m.group(6)), int(m.group(7)), int(m.group(5)))
                rx_first_rtp.setdefault(rtp, rx)
                if trace != 0:
                    rx_first_trace.setdefault(trace, rx)
                    rx_rtp_by_trace.setdefault(trace, rtp)
                continue
            m = RE_RX_NO_WALL.search(line)
            if m:
                rtp = int(m.group(1))
                trace = int(m.group(2))
                rx = (int(m.group(4)), int(m.group(5)), int(m.group(6)), -1)
                rx_first_rtp.setdefault(rtp, rx)
                if trace != 0:
                    rx_first_trace.setdefault(trace, rx)
                    rx_rtp_by_trace.setdefault(trace, rtp)
                continue
            m = RE_RX_LEGACY.search(line)
            if m:
                rtp = int(m.group(1))
                rx = (int(m.group(3)), int(m.group(4)), int(m.group(5)), -1)
                rx_first_rtp.setdefault(rtp, rx)
                continue
            m = RE_UI.search(line)
            if m:
                tid = int(m.group(1))
                if tid != 0:
                    ui_present_by_trace.setdefault(tid, int(m.group(3)))

    trace_buckets = empty_buckets()
    trace_pairs = 0
    for tid, rx in rx_first_trace.items():
        if tid not in tx_by_trace:
            continue
        tx = tx_by_trace[tid]
        rec = collect_deltas(tx, rx)
        if tid in ui_present_by_trace and tx[2] > 0 and ui_present_by_trace[tid] >= tx[2]:
            rec["e2e_v4l2_to_present_submit"] = ui_present_by_trace[tid] - tx[2]
        if tid in ui_present_by_trace and tx[1] > 0 and ui_present_by_trace[tid] >= tx[1]:
            rec["e2e_mjpeg_input_to_present_submit"] = ui_present_by_trace[tid] - tx[1]
        if tid in ui_present_by_trace and rx[2] > 0 and ui_present_by_trace[tid] >= rx[2]:
            rec["rx_sink_callback_to_present_submit"] = ui_present_by_trace[tid] - rx[2]
        for k, v in rec.items():
            if k in trace_buckets:
                trace_buckets[k].append(v)
        trace_pairs += 1

    direct_buckets = empty_buckets()
    direct_pairs = 0
    for rtp, rx in rx_first_rtp.items():
        if rtp not in tx_by_rtp:
            continue
        rec = collect_deltas(tx_by_rtp[rtp], rx)
        for k, v in rec.items():
            if k in direct_buckets:
                direct_buckets[k].append(v)
        direct_pairs += 1

    print(
        f"TX rtp keys={len(tx_by_rtp)} trace keys={len(tx_by_trace)}  |  "
        f"RX first-seen rtp={len(rx_first_rtp)} trace={len(rx_first_trace)}"
    )
    print(f"paired: trace_id={trace_pairs}  direct_rtp={direct_pairs}")

    if trace_pairs >= 8:
        print("\n=== 以下按 trace_id 配对（推荐）===\n")
        flush_bucket_print(trace_buckets, "")
    elif direct_pairs >= 8:
        print("\n=== 以下按 RTP 时间戳完全一致配对 ===\n")
        flush_bucket_print(direct_buckets, "")
    else:
        inferred = infer_rtp_offset(sorted(tx_by_rtp.keys()), sorted(rx_first_rtp.keys()))
        if inferred is None:
            print("No pairs; check WEBRTC_E2E_LATENCY_TRACE=1 on both processes.")
            return 1
        print(f"\n*** 警告: 进入 RTP 偏移推断配对，结果仅供参考。offset={inferred} ***\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

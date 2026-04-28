#!/usr/bin/env bash
# Weak-network E2E runner.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"
CONFIG="${CONFIG_FILE:-$ROOT/src/service/impl/config/streams.conf}"

cfg_get() {
  local key="$1" def="$2"
  [ ! -f "$CONFIG" ] && echo "$def" && return
  local line
  line="$(awk -F= -v k="$key" '$1 ~ "^[[:space:]]*"k"[[:space:]]*$" {print $0}' "$CONFIG" | tail -1)" || true
  [ -z "$line" ] && echo "$def" && return
  echo "${line#*=}" | sed 's/#.*//' | xargs
}

profile="fair"; camera=""; iface=""; duration_sec=45; frames=500; skip_first=30; apply_only=0; clear_only=0; assume_yes=0
while [ $# -gt 0 ]; do
  case "$1" in
    --profile) profile="${2:-}"; shift 2 ;;
    --camera) camera="${2:-}"; shift 2 ;;
    --iface) iface="${2:-}"; shift 2 ;;
    --duration-sec) duration_sec="${2:-}"; shift 2 ;;
    --frames) frames="${2:-}"; shift 2 ;;
    --skip-first) skip_first="${2:-}"; shift 2 ;;
    --apply-only) apply_only=1; shift ;;
    --clear-only) clear_only=1; shift ;;
    --yes) assume_yes=1; shift ;;
    *) echo "Unknown arg: $1" >&2; exit 2 ;;
  esac
done

if [ -z "$iface" ]; then
  sig_addr="$(cfg_get SIGNALING_ADDR 127.0.0.1:8765)"
  host="${sig_addr%%:*}"
  [ "$host" = "127.0.0.1" ] || [ "$host" = "localhost" ] || { echo "please provide --iface explicitly"; exit 2; }
  iface="lo"
fi

if [ "$clear_only" = "1" ]; then
  sudo tc qdisc del dev "$iface" root 2>/dev/null || true
  echo "[weaknet] cleared qdisc on $iface"
  exit 0
fi

case "$profile" in
  good) delay_ms=25; jitter_ms=8; loss_pct=0.2; reorder_pct=0; reorder_corr_pct=0; dup_pct=0; corrupt_pct=0; rate="20mbit" ;;
  fair) delay_ms=60; jitter_ms=20; loss_pct=1.0; reorder_pct=1.0; reorder_corr_pct=25; dup_pct=0; corrupt_pct=0; rate="8mbit" ;;
  bad) delay_ms=120; jitter_ms=40; loss_pct=2.5; reorder_pct=3.0; reorder_corr_pct=30; dup_pct=0.2; corrupt_pct=0; rate="4mbit" ;;
  harsh) delay_ms=180; jitter_ms=60; loss_pct=4.0; reorder_pct=5.0; reorder_corr_pct=35; dup_pct=0.5; corrupt_pct=0.1; rate="2mbit" ;;
  custom)
    delay_ms="${WEAKNET_DELAY_MS:-60}"; jitter_ms="${WEAKNET_JITTER_MS:-20}"; loss_pct="${WEAKNET_LOSS_PCT:-1.5}"
    reorder_pct="${WEAKNET_REORDER_PCT:-1}"; reorder_corr_pct="${WEAKNET_REORDER_CORR_PCT:-25}"
    dup_pct="${WEAKNET_DUP_PCT:-0}"; corrupt_pct="${WEAKNET_CORRUPT_PCT:-0}"; rate="${WEAKNET_RATE:-6mbit}"
    ;;
  *) echo "Unsupported profile: $profile" >&2; exit 2 ;;
esac

if [ "$assume_yes" != "1" ]; then
  echo "[weaknet] profile=$profile iface=$iface delay=${delay_ms}ms jitter=${jitter_ms}ms loss=${loss_pct}% reorder=${reorder_pct}% rate=$rate"
  read -r -p "Apply tc netem and run e2e? [y/N] " yn
  case "$yn" in [Yy]*) ;; *) echo "cancelled"; exit 0 ;; esac
fi

cleanup() { sudo tc qdisc del dev "$iface" root 2>/dev/null || true; }
trap cleanup EXIT INT TERM
sudo tc qdisc del dev "$iface" root 2>/dev/null || true
sudo tc qdisc add dev "$iface" root netem delay "${delay_ms}ms" "${jitter_ms}ms" loss "${loss_pct}%" reorder "${reorder_pct}%" "${reorder_corr_pct}%" duplicate "${dup_pct}%" corrupt "${corrupt_pct}%" rate "$rate"
[ "$apply_only" = "1" ] && exit 0

mkdir -p "$ROOT/build/e2e_weaknet"
ts="$(date +%Y%m%d-%H%M%S)"
run_log="$ROOT/build/e2e_weaknet/${ts}_${profile}.log"
export E2E_FRAMES="$frames"
export E2E_PULL_TIMEOUT_SEC="$duration_sec"
export E2E_SKIP_FIRST_PAIRS="$skip_first"
export E2E_LOWLAT_PROFILE="${E2E_LOWLAT_PROFILE:-steady}"
export PUSH_LOWLATENCY_PROFILE="${PUSH_LOWLATENCY_PROFILE:-stable}"
case "${E2E_LOWLAT_PROFILE}" in
  aggressive)
    export RFLOW_ZERO_PLAYOUT_MIN_PACING_MS="${RFLOW_ZERO_PLAYOUT_MIN_PACING_MS:-0}"
    export RFLOW_MAX_DECODE_QUEUE_SIZE="${RFLOW_MAX_DECODE_QUEUE_SIZE:-4}"
    ;;
  balanced)
    export RFLOW_ZERO_PLAYOUT_MIN_PACING_MS="${RFLOW_ZERO_PLAYOUT_MIN_PACING_MS:-1}"
    export RFLOW_MAX_DECODE_QUEUE_SIZE="${RFLOW_MAX_DECODE_QUEUE_SIZE:-6}"
    ;;
  stable|steady)
    export RFLOW_ZERO_PLAYOUT_MIN_PACING_MS="${RFLOW_ZERO_PLAYOUT_MIN_PACING_MS:-2}"
    export RFLOW_MAX_DECODE_QUEUE_SIZE="${RFLOW_MAX_DECODE_QUEUE_SIZE:-8}"
    ;;
esac
export RFLOW_ENABLE_DECODE_QUEUE_GUARD="${RFLOW_ENABLE_DECODE_QUEUE_GUARD:-1}"
export RFLOW_DECODE_QUEUE_GUARD_CAP="${RFLOW_DECODE_QUEUE_GUARD_CAP:-8}"

if [ -n "$camera" ]; then
  (cd "$ROOT" && ./scripts/pull.sh --e2e 127.0.0.1:8765 livestream "$camera") | tee "$run_log"
else
  (cd "$ROOT" && ./scripts/pull.sh --e2e) | tee "$run_log"
fi

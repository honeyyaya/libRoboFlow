#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"
CONFIG="${CONFIG_FILE:-$ROOT/src/service/impl/config/streams.conf}"

STREAM_ID="${1:-}"
CAMERA="${2:-}"
SIGNALING_ADDR="${SIGNALING_ADDR:-127.0.0.1:8765}"
START_SIGNALING="${START_SIGNALING:-1}"

APP_DIR="${RFLOW_APPS_BIN:-$ROOT/build/apps}"
PUSH_BIN="${APP_DIR}/push_demo"
SIGNAL_BIN="${APP_DIR}/signaling_server"

if [[ ! -x "$PUSH_BIN" || ! -x "$SIGNAL_BIN" ]]; then
  echo "缺少可执行文件，请先运行 ./scripts/build.sh"
  exit 1
fi

SIG_PID=""
cleanup() {
  if [[ -n "${SIG_PID}" ]]; then
    kill "$SIG_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

if [[ "$START_SIGNALING" == "1" ]]; then
  PORT="${SIGNALING_ADDR##*:}"
  [[ "$PORT" =~ ^[0-9]+$ ]] || PORT=8765
  "$SIGNAL_BIN" "$PORT" &
  SIG_PID=$!
  sleep 1
fi

CMD=("$PUSH_BIN" "--config" "$CONFIG" "--signaling" "$SIGNALING_ADDR")
[[ -n "$STREAM_ID" ]] && CMD+=("$STREAM_ID")
[[ -n "$CAMERA" ]] && CMD+=("$CAMERA")
exec "${CMD[@]}"

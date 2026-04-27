#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build/linux-arm64}"
export LD_LIBRARY_PATH="${BUILD}/src/client:${BUILD}/src/service"
SIGNAL_PORT="${SIGNAL_PORT:-8765}"
LOG_DIR="${LOG_DIR:-$BUILD/run-logs}"
mkdir -p "$LOG_DIR"
export RFLOW_PUSH_DEMO_CAMERA="${RFLOW_PUSH_DEMO_CAMERA:-/dev/video0}"

for p in $(lsof -t -i :"${SIGNAL_PORT}" 2>/dev/null || true); do kill -TERM "$p" 2>/dev/null || true; done
sleep 0.3
lsof -t -i :"${SIGNAL_PORT}" 2>/dev/null | xargs -r kill -9 || true
pkill -9 -f 'push_demo_sdk' 2>/dev/null || true
fuser -k /dev/video0 2>/dev/null || true
sleep 0.5

SIGLOG="${LOG_DIR}/signaling_server.log"
PUSHLOG="${LOG_DIR}/push_demo_sdk.log"
: > "$SIGLOG"
: > "$PUSHLOG"

nohup "${BUILD}/apps/signaling_server" "${SIGNAL_PORT}" 4 >>"$SIGLOG" 2>&1 &
SIG_PID=$!
for _ in $(seq 1 60); do
  if nc -z 127.0.0.1 "${SIGNAL_PORT}" 2>/dev/null; then break; fi
  sleep 0.1
  if ! kill -0 "${SIG_PID}" 2>/dev/null; then echo "signaling died:"; cat "$SIGLOG"; exit 1; fi
done
if ! nc -z 127.0.0.1 "${SIGNAL_PORT}" 2>/dev/null; then echo "signaling not listening"; cat "$SIGLOG"; exit 1; fi

nohup "${BUILD}/apps/push_demo_sdk" "127.0.0.1:${SIGNAL_PORT}" demo_device 1280 720 60 0 >>"$PUSHLOG" 2>&1 &
PUSH_PID=$!
{ echo "SIG_PID=${SIG_PID}"; echo "PUSH_PID=${PUSH_PID}"; } > "${LOG_DIR}/live_pids.txt"
sleep 3
if ! kill -0 "${PUSH_PID}" 2>/dev/null; then echo "push exited:"; cat "$PUSHLOG"; exit 1; fi

echo "信令 pid=${SIG_PID}  推流 push_demo_sdk pid=${PUSH_PID}  1280x720@60  端口 ${SIGNAL_PORT}"
grep -E 'Direct capture|target |started \(signaling' "$PUSHLOG" | head -5 || true

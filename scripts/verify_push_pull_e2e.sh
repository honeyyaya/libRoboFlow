#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build/linux-arm64}"
export LD_LIBRARY_PATH="${BUILD}/src/client:${BUILD}/src/service"
PORT="${SIGNAL_PORT:-8765}"
export RFLOW_PUSH_DEMO_CAMERA="${RFLOW_PUSH_DEMO_CAMERA:-/dev/video0}"

cleanup() {
  [[ -n "${PUSH_PID:-}" ]] && kill "$PUSH_PID" 2>/dev/null || true
  [[ -n "${SIG_PID:-}" ]] && kill "$SIG_PID" 2>/dev/null || true
}
trap cleanup EXIT

for p in $(lsof -t -i :"${PORT}" 2>/dev/null || true); do kill -9 "$p" 2>/dev/null || true; done
pkill -9 -f push_demo_sdk 2>/dev/null || true
pkill -9 -f pull_demo_sdk 2>/dev/null || true
fuser -k /dev/video0 2>/dev/null || true
sleep 0.4

"$BUILD/apps/signaling_server" "${PORT}" 4 >/tmp/rflow_verify_sig.log 2>&1 &
SIG_PID=$!
for _ in $(seq 1 60); do
  if nc -z 127.0.0.1 "${PORT}" 2>/dev/null; then break; fi
  sleep 0.1
  if ! kill -0 "$SIG_PID" 2>/dev/null; then
    echo "FAIL: signaling died"
    cat /tmp/rflow_verify_sig.log
    exit 1
  fi
done
if ! nc -z 127.0.0.1 "${PORT}" 2>/dev/null; then
  echo "FAIL: port ${PORT}"
  cat /tmp/rflow_verify_sig.log
  exit 1
fi
echo "[ok] signaling on ${PORT} pid=${SIG_PID}"

# 640x480@30 兼容性最好；若设备只支持 720p 可改环境变量 WIDTH HEIGHT FPS
W="${WIDTH:-640}"
H="${HEIGHT:-480}"
F="${FPS:-30}"
"$BUILD/apps/push_demo_sdk" "127.0.0.1:${PORT}" demo_device "${W}" "${H}" "${F}" 0 >/tmp/rflow_verify_push.log 2>&1 &
PUSH_PID=$!
sleep 5
if ! kill -0 "$PUSH_PID" 2>/dev/null; then
  echo "FAIL: push exited"
  cat /tmp/rflow_verify_push.log
  exit 1
fi
echo "[ok] push_demo_sdk pid=${PUSH_PID} ${W}x${H}@${F}"

set +e
timeout 22 "$BUILD/apps/pull_demo_sdk" "127.0.0.1:${PORT}" demo_device 0 2>&1 | tee /tmp/rflow_verify_pull.log
PULL_RC=$?
set -e
echo "[info] pull exit=${PULL_RC}"

if grep -qE '\[demo\] frame#1 ' /tmp/rflow_verify_pull.log || grep -q 'frame#1' /tmp/rflow_verify_pull.log; then
  echo ""
  echo "=== E2E 成功：拉流收到 frame 统计 ==="
  grep 'frame#' /tmp/rflow_verify_pull.log | head -5
  exit 0
fi
echo ""
echo "=== E2E 失败：未见 frame# ==="
grep -E 'state=|reason|failed|init' /tmp/rflow_verify_pull.log | tail -25
exit 1

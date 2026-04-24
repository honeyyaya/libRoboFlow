#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/linux-arm64}"
SIGNAL_HOST="${SIGNAL_HOST:-127.0.0.1}"
SIGNAL_PORT="${SIGNAL_PORT:-8765}"
SIGNAL_URL="${SIGNAL_URL:-${SIGNAL_HOST}:${SIGNAL_PORT}}"
SIGNAL_THREADS="${SIGNAL_THREADS:-4}"
DEVICE_ID="${DEVICE_ID:-demo_device}"
WIDTH="${WIDTH:-640}"
HEIGHT="${HEIGHT:-360}"
FPS="${FPS:-30}"
LOG_DIR="${LOG_DIR:-${BUILD_DIR}/run-logs}"

find_binary() {
  local base_dir="$1"
  local name="$2"
  local result
  result="$(find "${base_dir}" -type f \( -name "${name}" -o -name "${name}.exe" \) | head -n 1 || true)"
  if [[ -z "${result}" ]]; then
    return 1
  fi
  printf '%s\n' "${result}"
}

wait_for_tcp() {
  local host="$1"
  local port="$2"
  local retries="${3:-50}"
  local delay="${4:-0.2}"
  local i
  for ((i=0; i<retries; ++i)); do
    if command -v nc >/dev/null 2>&1; then
      if nc -z "${host}" "${port}" >/dev/null 2>&1; then
        return 0
      fi
    else
      if bash -lc "exec 3<>/dev/tcp/${host}/${port}" >/dev/null 2>&1; then
        return 0
      fi
    fi
    sleep "${delay}"
  done
  return 1
}

SIGNAL_BIN="$(find_binary "${BUILD_DIR}" signaling_server || true)"
PUSH_BIN="$(find_binary "${BUILD_DIR}" push_demo_sdk || true)"

if [[ -z "${SIGNAL_BIN}" ]]; then
  echo "[run] signaling_server not found under ${BUILD_DIR}" >&2
  echo "[run] build first: scripts/build_linux_arm64.sh" >&2
  exit 1
fi
if [[ -z "${PUSH_BIN}" ]]; then
  echo "[run] push_demo_sdk not found under ${BUILD_DIR}" >&2
  echo "[run] build first with service WebRTC apps enabled" >&2
  exit 1
fi

mkdir -p "${LOG_DIR}"
SIGNAL_LOG="${LOG_DIR}/signaling_server.log"
PUSH_LOG="${LOG_DIR}/push_demo_sdk.log"

SERVER_PID=""
cleanup() {
  local exit_code=$?
  if [[ -n "${SERVER_PID}" ]] && kill -0 "${SERVER_PID}" >/dev/null 2>&1; then
    kill "${SERVER_PID}" >/dev/null 2>&1 || true
    wait "${SERVER_PID}" >/dev/null 2>&1 || true
  fi
  exit "${exit_code}"
}
trap cleanup EXIT INT TERM

echo "[run] signaling : ${SIGNAL_BIN}"
echo "[run] push demo : ${PUSH_BIN}"
echo "[run] signal url: ${SIGNAL_URL}"
echo "[run] device id : ${DEVICE_ID}"
echo "[run] frame cfg : ${WIDTH}x${HEIGHT}@${FPS}"
echo "[run] logs      : ${LOG_DIR}"

"${SIGNAL_BIN}" "${SIGNAL_PORT}" "${SIGNAL_THREADS}" >"${SIGNAL_LOG}" 2>&1 &
SERVER_PID=$!

if ! wait_for_tcp "${SIGNAL_HOST}" "${SIGNAL_PORT}" 50 0.2; then
  echo "[run] signaling server failed to listen on ${SIGNAL_URL}" >&2
  echo "[run] see log: ${SIGNAL_LOG}" >&2
  exit 1
fi

echo "[run] signaling server ready"
echo "[run] tail -f ${SIGNAL_LOG}"
echo "[run] push log  ${PUSH_LOG}"

"${PUSH_BIN}" "${SIGNAL_URL}" "${DEVICE_ID}" "${WIDTH}" "${HEIGHT}" "${FPS}" | tee "${PUSH_LOG}"

#!/usr/bin/env bash
# 同步采集 E2E 日志 + 系统调度证据（CPU/线程/负载）。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${ROOT}/build/e2e_systrace_$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT_DIR"

CAMERA="${1:-}"
CAP_INTERVAL="${E2E_CAPTURE_INTERVAL_SEC:-1}"
EXTRA_ENV="${E2E_EXTRA_ENV:-}"

start_bg() {
  local name="$1"; shift
  if command -v "$1" >/dev/null 2>&1; then
    "$@" >"${OUT_DIR}/${name}.log" 2>&1 &
    echo $!
  else
    echo ""
  fi
}

PIDS=()
for cmd in "vmstat vmstat ${CAP_INTERVAL}" "mpstat mpstat -P ALL ${CAP_INTERVAL}" "pidstat pidstat -rud -h ${CAP_INTERVAL}"; do
  set -- $cmd
  pid="$(start_bg "$1" "$2" ${@:3})"
  [ -n "$pid" ] && PIDS+=("$pid")
done
if command -v top >/dev/null 2>&1; then
  top -b -d "$CAP_INTERVAL" >"${OUT_DIR}/top.log" 2>&1 &
  PIDS+=("$!")
fi

cleanup() { for p in "${PIDS[@]:-}"; do kill "$p" 2>/dev/null || true; done; }
trap cleanup EXIT INT TERM

set +e
if [ -n "$EXTRA_ENV" ]; then
  bash -lc "cd \"$ROOT\" && $EXTRA_ENV ./scripts/pull.sh --e2e ${CAMERA}" >"${OUT_DIR}/e2e_run.log" 2>&1
  RC=$?
else
  (cd "$ROOT" && ./scripts/pull.sh --e2e "$CAMERA") >"${OUT_DIR}/e2e_run.log" 2>&1
  RC=$?
fi
set -e

cleanup
trap - EXIT INT TERM
cp -f "${ROOT}/build/e2e_last_push.log" "${OUT_DIR}/e2e_last_push.log" 2>/dev/null || true
cp -f "${ROOT}/build/e2e_last_pull.log" "${OUT_DIR}/e2e_last_pull.log" 2>/dev/null || true

echo "[systrace] e2e exit code: $RC"
echo "[systrace] output: $OUT_DIR"
echo "next:"
echo "  python3 tools/parse_e2e_latency.py \"${OUT_DIR}/e2e_last_push.log\" \"${OUT_DIR}/e2e_last_pull.log\""
echo "  python3 tools/explain_e2e_outliers.py \"${OUT_DIR}/e2e_last_push.log\" \"${OUT_DIR}/e2e_last_pull.log\" --top 10"
exit "$RC"

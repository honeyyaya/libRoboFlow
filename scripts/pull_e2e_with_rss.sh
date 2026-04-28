#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"
LOG_DIR="${ROOT}/build"
mkdir -p "$LOG_DIR"

PUSH_LOG="${LOG_DIR}/e2e_push.log"
PULL_LOG="${LOG_DIR}/e2e_pull.log"
RSS_LOG="${LOG_DIR}/e2e_pull_rss.log"

"$SCRIPT_DIR/push.sh" >"$PUSH_LOG" 2>&1 &
PUSH_PID=$!
sleep 2

"$SCRIPT_DIR/pull.sh" "${1:-}" "${2:-}" >"$PULL_LOG" 2>&1 &
PULL_PID=$!

"$SCRIPT_DIR/rss_watch.sh" "$PULL_PID" "${RSS_INTERVAL_SEC:-1}" >"$RSS_LOG" 2>&1 &
RSS_PID=$!

wait "$PULL_PID" || true
kill "$RSS_PID" 2>/dev/null || true
kill "$PUSH_PID" 2>/dev/null || true
wait "$PUSH_PID" 2>/dev/null || true

echo "logs:"
echo "  push: $PUSH_LOG"
echo "  pull: $PULL_LOG"
echo "  rss : $RSS_LOG"

#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <pid> [interval_sec]"
  exit 1
fi

PID="$1"
INTERVAL="${2:-1}"

echo "watch pid=$PID interval=${INTERVAL}s"
while kill -0 "$PID" 2>/dev/null; do
  RSS_KB="$(awk '/VmRSS/ {print $2}' "/proc/${PID}/status" 2>/dev/null || echo 0)"
  echo "$(date +%T) rss_kb=${RSS_KB}"
  sleep "$INTERVAL"
done

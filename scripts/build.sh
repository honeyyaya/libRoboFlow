#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${ROOT}/build"

cmake -S "$ROOT" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DRFLOW_BUILD_APPS=ON \
  -DRFLOW_BUILD_STANDALONE_SERVICE_DEMOS=ON \
  -DRFLOW_SERVICE_ENABLE_WEBRTC_IMPL=ON \
  -DRFLOW_ENABLE_ROCKCHIP_MPP=ON

cmake --build "$BUILD_DIR" -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

echo "OK:"
echo "  ${BUILD_DIR}/apps/signaling_server"
echo "  ${BUILD_DIR}/apps/push_demo"
echo "  ${BUILD_DIR}/apps/pull_demo"
echo "  ${BUILD_DIR}/apps/mpp_h264_smoke"

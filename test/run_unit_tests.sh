#!/usr/bin/env bash
# 跑 libRoboFlow 单元测试。
#   - 假设 build.sh 已执行（或外部已 cmake -DRFLOW_BUILD_TESTS=ON 配过）；
#   - 等价于 (cd build && ctest --output-on-failure)，外加少量友好输出。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"

if [[ ! -d "$BUILD_DIR" ]]; then
  echo "FAIL: build dir not found at $BUILD_DIR; run apps/build.sh first." >&2
  exit 1
fi

if ! ls "$BUILD_DIR"/test/test_* >/dev/null 2>&1; then
  echo "FAIL: no test binaries under $BUILD_DIR/test; build with -DRFLOW_BUILD_TESTS=ON" >&2
  exit 1
fi

cmake --build "$BUILD_DIR" --target $(ls "$BUILD_DIR"/test | grep -E '^test_' | tr '\n' ' ') \
  -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" >/dev/null

cd "$BUILD_DIR" && exec ctest --output-on-failure

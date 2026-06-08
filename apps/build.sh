#!/usr/bin/env bash
#
# libRoboFlow 全量构建入口（Linux arm64 / Rockchip）。
# 默认 Debug（保留符号、关闭优化），启用 client + service + apps + tests + libwebrtc + Rockchip MPP。
#
# 注：src/common 的 ABI handle 字段直接持有 webrtc::scoped_refptr，无法在 webrtc=OFF
# 下成功编译；因此本脚本不提供 "tests-only" 之类剔除 webrtc 的最小回归开关。
# 想跑单测：./apps/build.sh && cd build && ctest --output-on-failure。
#
# 可选开关：
#   --release       以 Release 模式编译（-O3、strip 友好）
#   --debug         同默认，显式指定 Debug（保留符号、关闭优化，适合 gdb）
#   --werror        叠加 -Werror + RFLOW_ENABLE_STRUCTURE_CHECK，CI 守卫场景
#   -h / --help     打印用法

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${ROOT}/build"

BUILD_TYPE="Debug"
WERROR=0

usage() {
    cat <<EOF
Usage: $(basename "$0") [--release] [--debug] [--werror]

Default: Debug; client+service+apps+tests+libwebrtc+rockchip-mpp.

  --release       build with CMAKE_BUILD_TYPE=Release
  --debug         build with CMAKE_BUILD_TYPE=Debug (default)
  --werror        treat warnings as errors and run src/ structure check (CI guard)
  -h, --help      show this help
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --release)      BUILD_TYPE="Release"; shift ;;
        --debug)        BUILD_TYPE="Debug";   shift ;;
        --werror)       WERROR=1;             shift ;;
        -h|--help)      usage; exit 0 ;;
        *)              echo "unknown flag: $1" >&2; usage; exit 1 ;;
    esac
done

CMAKE_ARGS=(
    -S "$ROOT"
    -B "$BUILD_DIR"
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DRFLOW_BUILD_TESTS=ON
    -DRFLOW_BUILD_APPS=ON
    -DRFLOW_CLIENT_ENABLE_WEBRTC_IMPL=ON
    -DRFLOW_SERVICE_ENABLE_WEBRTC_IMPL=ON
    -DRFLOW_ENABLE_ROCKCHIP_MPP=ON
)

if [[ $WERROR -eq 1 ]]; then
    CMAKE_ARGS+=(
        -DRFLOW_ENABLE_WERROR=ON
        -DRFLOW_ENABLE_STRUCTURE_CHECK=ON
    )
fi

echo "[build] type=$BUILD_TYPE werror=$WERROR build=$BUILD_DIR"

cmake "${CMAKE_ARGS[@]}"
cmake --build "$BUILD_DIR" -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

echo "[build] done"
echo "  ${BUILD_DIR}/apps/signaling_server"
echo "  ${BUILD_DIR}/apps/push_demo_sdk"
echo "  ${BUILD_DIR}/apps/pull_demo_sdk"
echo "  cd ${BUILD_DIR} && ctest --output-on-failure   # run unit tests"

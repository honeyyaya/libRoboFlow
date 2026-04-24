#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/linux-arm64}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
GENERATOR="${GENERATOR:-Ninja}"

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "[build] this script targets Linux hosts only" >&2
  exit 1
fi

ARCH="$(uname -m)"
if [[ "${ARCH}" != "aarch64" && "${ARCH}" != "arm64" ]]; then
  echo "[build] warning: host arch is ${ARCH}, expected aarch64/arm64" >&2
  echo "[build] continue only if you are intentionally cross-configuring with your own toolchain" >&2
fi

if ! command -v cmake >/dev/null 2>&1; then
  echo "[build] cmake not found" >&2
  exit 1
fi

if [[ "${GENERATOR}" == "Ninja" ]] && ! command -v ninja >/dev/null 2>&1; then
  echo "[build] ninja not found, fallback to Unix Makefiles" >&2
  GENERATOR="Unix Makefiles"
fi

mkdir -p "${BUILD_DIR}"

echo "[build] root      : ${ROOT_DIR}"
echo "[build] build dir : ${BUILD_DIR}"
echo "[build] type      : ${BUILD_TYPE}"
echo "[build] generator : ${GENERATOR}"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -G "${GENERATOR}" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DRFLOW_BUILD_CLIENT=ON \
  -DRFLOW_BUILD_SERVICE=ON \
  -DRFLOW_BUILD_APPS=ON \
  -DRFLOW_BUILD_SHARED=ON \
  -DRFLOW_CLIENT_ENABLE_WEBRTC_IMPL=ON \
  -DRFLOW_SERVICE_ENABLE_WEBRTC_IMPL=ON \
  -DRFLOW_ENABLE_ROCKCHIP_MPP="${RFLOW_ENABLE_ROCKCHIP_MPP:-OFF}" \
  ${CMAKE_EXTRA_ARGS:-}

cmake --build "${BUILD_DIR}" --parallel "${BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"

echo "[build] done"
echo "[build] try:"
echo "  ${BUILD_DIR}/apps/signaling_server"
echo "  ${BUILD_DIR}/apps/push_demo_sdk"
echo "  ${BUILD_DIR}/apps/pull_demo_sdk"

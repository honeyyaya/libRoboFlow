#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"
CONFIG="${CONFIG_FILE:-$ROOT/src/service/impl/config/streams.conf}"
APP_DIR="${RFLOW_APPS_BIN:-$ROOT/build/apps}"
PULL_BIN="${APP_DIR}/pull_demo"
PUSH_BIN="${APP_DIR}/push_demo"
SIGNAL_BIN="${APP_DIR}/signaling_server"
TOOLS_DIR="${ROOT}/tools"

cfg_get() {
  local key="$1" def="$2"
  [[ ! -f "$CONFIG" ]] && echo "$def" && return
  local line
  line="$(grep -E "^[[:space:]]*${key}=" "$CONFIG" | tail -1)" || true
  [[ -z "$line" ]] && echo "$def" && return
  echo "${line#*=}" | sed 's/#.*//' | xargs
}

apply_rflow_lowlat_profile() {
  local profile="${1:-aggressive}"
  case "$profile" in
    aggressive)
      export RFLOW_ZERO_PLAYOUT_MIN_PACING_MS="${RFLOW_ZERO_PLAYOUT_MIN_PACING_MS:-0}"
      export RFLOW_MAX_DECODE_QUEUE_SIZE="${RFLOW_MAX_DECODE_QUEUE_SIZE:-4}"
      ;;
    balanced)
      export RFLOW_ZERO_PLAYOUT_MIN_PACING_MS="${RFLOW_ZERO_PLAYOUT_MIN_PACING_MS:-1}"
      export RFLOW_MAX_DECODE_QUEUE_SIZE="${RFLOW_MAX_DECODE_QUEUE_SIZE:-6}"
      ;;
    stable)
      export RFLOW_ZERO_PLAYOUT_MIN_PACING_MS="${RFLOW_ZERO_PLAYOUT_MIN_PACING_MS:-2}"
      export RFLOW_MAX_DECODE_QUEUE_SIZE="${RFLOW_MAX_DECODE_QUEUE_SIZE:-8}"
      ;;
    steady)
      export RFLOW_ZERO_PLAYOUT_MIN_PACING_MS="${RFLOW_ZERO_PLAYOUT_MIN_PACING_MS:-2}"
      export RFLOW_MAX_DECODE_QUEUE_SIZE="${RFLOW_MAX_DECODE_QUEUE_SIZE:-10}"
      ;;
    off|none)
      ;;
    *)
      echo "Unknown profile: $profile (use aggressive|balanced|stable|steady|off)" >&2
      return 2
      ;;
  esac
}

run_e2e() {
  local SIGNALING="${1:-127.0.0.1:8765}"
  local STREAM_ID="${2:-livestream}"
  local CAMERA="${3:-/dev/video0}"
  local FRAMES="${E2E_FRAMES:-400}"
  local TIMEOUT="${E2E_PULL_TIMEOUT_SEC:-150}"
  local PUSH_READY_SEC="${E2E_PUSH_READY_SEC:-30}"
  local LOG_DIR="${ROOT}/build"
  local PUSH_LOG="${LOG_DIR}/e2e_last_push.log"
  local PULL_LOG="${LOG_DIR}/e2e_last_pull.log"
  local PULL_PROFILE="${E2E_LOWLAT_PROFILE:-steady}"
  local E2E_SKIP_FIRST_PAIRS="${E2E_SKIP_FIRST_PAIRS:-30}"
  mkdir -p "$LOG_DIR"

  for b in "$PULL_BIN" "$PUSH_BIN" "$SIGNAL_BIN"; do
    [[ -x "$b" ]] || { echo "缺少可执行文件: $b（先 ./scripts/build.sh）"; return 1; }
  done
  [[ -e "$CAMERA" ]] || { echo "摄像头不存在: $CAMERA" >&2; return 1; }

  apply_rflow_lowlat_profile "$PULL_PROFILE"
  export RFLOW_ENABLE_DECODE_QUEUE_GUARD="${RFLOW_ENABLE_DECODE_QUEUE_GUARD:-1}"
  export RFLOW_DECODE_QUEUE_GUARD_CAP="${RFLOW_DECODE_QUEUE_GUARD_CAP:-8}"
  export WEBRTC_E2E_LATENCY_TRACE=1
  export WEBRTC_LATENCY_TRACE="${WEBRTC_LATENCY_TRACE:-1}"
  export WEBRTC_MPP_H264_DEC_LOW_LATENCY="${WEBRTC_MPP_H264_DEC_LOW_LATENCY:-1}"

  local PUSH_PROFILE="${PUSH_LOWLATENCY_PROFILE:-stable}"
  local CFG_V4L_BUF CFG_V4L_POLL
  CFG_V4L_BUF="$(cfg_get V4L2_BUFFER_COUNT "")"
  CFG_V4L_POLL="$(cfg_get V4L2_POLL_TIMEOUT_MS "")"
  case "$PUSH_PROFILE" in
    aggressive)
      export WEBRTC_V4L2_BUFFER_COUNT="${WEBRTC_V4L2_BUFFER_COUNT:-${CFG_V4L_BUF:-2}}"
      export WEBRTC_V4L2_POLL_TIMEOUT_MS="${WEBRTC_V4L2_POLL_TIMEOUT_MS:-${CFG_V4L_POLL:-5}}"
      export WEBRTC_MJPEG_DEC_LOW_LATENCY="${WEBRTC_MJPEG_DEC_LOW_LATENCY:-1}"
      ;;
    balanced)
      export WEBRTC_V4L2_BUFFER_COUNT="${WEBRTC_V4L2_BUFFER_COUNT:-${CFG_V4L_BUF:-3}}"
      export WEBRTC_V4L2_POLL_TIMEOUT_MS="${WEBRTC_V4L2_POLL_TIMEOUT_MS:-${CFG_V4L_POLL:-8}}"
      export WEBRTC_MJPEG_DEC_LOW_LATENCY="${WEBRTC_MJPEG_DEC_LOW_LATENCY:-1}"
      ;;
    stable)
      export WEBRTC_V4L2_BUFFER_COUNT="${WEBRTC_V4L2_BUFFER_COUNT:-${CFG_V4L_BUF:-3}}"
      export WEBRTC_V4L2_POLL_TIMEOUT_MS="${WEBRTC_V4L2_POLL_TIMEOUT_MS:-${CFG_V4L_POLL:-5}}"
      export WEBRTC_MJPEG_DEC_LOW_LATENCY="${WEBRTC_MJPEG_DEC_LOW_LATENCY:-1}"
      ;;
    off|none)
      ;;
    *)
      echo "Unknown PUSH_LOWLATENCY_PROFILE=$PUSH_PROFILE (use aggressive|balanced|stable|off)" >&2
      return 2
      ;;
  esac

  local EXP_SIG="E2E-SIGNATURE pull_profile=${PULL_PROFILE} push_profile=${PUSH_PROFILE} "
  EXP_SIG+="pull_pacing_ms=${RFLOW_ZERO_PLAYOUT_MIN_PACING_MS:-default} "
  EXP_SIG+="pull_decode_queue=${RFLOW_MAX_DECODE_QUEUE_SIZE:-default} "
  EXP_SIG+="pull_guard_cap=${RFLOW_DECODE_QUEUE_GUARD_CAP:-default} "
  EXP_SIG+="push_v4l2_buf=${WEBRTC_V4L2_BUFFER_COUNT:-default} "
  EXP_SIG+="push_poll_timeout_ms=${WEBRTC_V4L2_POLL_TIMEOUT_MS:-default} "
  EXP_SIG+="push_mjpeg_lowlat=${WEBRTC_MJPEG_DEC_LOW_LATENCY:-default} "
  EXP_SIG+="frames=${FRAMES} timeout_s=${TIMEOUT} skip_first_pairs=${E2E_SKIP_FIRST_PAIRS}"

  local E2E_CPU_PIN_MODE="${E2E_CPU_PIN_MODE:-auto}"
  local E2E_SCHED_MODE="${E2E_SCHED_MODE:-auto}"
  local E2E_SCHED_PRIO="${E2E_SCHED_PRIO:-20}"
  local E2E_NICE_VALUE="${E2E_NICE_VALUE:-0}"
  local USE_TASKSET=0 USE_CHRT=0 USE_NICE=0 PUSH_CORE="" PULL_CORE="" SIG_CORE=""
  if [[ "$E2E_CPU_PIN_MODE" != "off" ]] && command -v taskset >/dev/null 2>&1; then
    local NCPU
    NCPU="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)"
    if [[ "$NCPU" -ge 4 ]]; then
      USE_TASKSET=1
      PUSH_CORE="${E2E_PUSH_CORE:-$((NCPU - 2))}"
      PULL_CORE="${E2E_PULL_CORE:-$((NCPU - 1))}"
      SIG_CORE="${E2E_SIGNALING_CORE:-$((NCPU - 3))}"
    fi
  fi
  if [[ "$E2E_SCHED_MODE" != "off" ]] && command -v chrt >/dev/null 2>&1; then
    if [[ "$E2E_SCHED_MODE" == "rr" || "$E2E_SCHED_MODE" == "auto" ]]; then
      if chrt -f "$E2E_SCHED_PRIO" true >/dev/null 2>&1; then
        USE_CHRT=1
      elif [[ "$E2E_SCHED_MODE" == "rr" ]]; then
        echo "E2E_SCHED_MODE=rr 但当前无 realtime 权限（需 root/CAP_SYS_NICE）" >&2
        return 2
      fi
    fi
  fi
  [[ "$USE_CHRT" -eq 0 && "$E2E_SCHED_MODE" == "auto" ]] && USE_NICE=1
  if [[ "$USE_NICE" -eq 1 ]]; then
    if ! [[ "$E2E_NICE_VALUE" =~ ^-?[0-9]+$ ]] || [[ "$E2E_NICE_VALUE" -lt -20 || "$E2E_NICE_VALUE" -gt 19 ]]; then
      E2E_NICE_VALUE=0
    fi
    if [[ "$E2E_NICE_VALUE" -lt 0 ]] && ! nice -n "$E2E_NICE_VALUE" true >/dev/null 2>&1; then
      echo "nice=$E2E_NICE_VALUE 无权限，自动降级到 nice=0（如需负 nice 请使用 root/CAP_SYS_NICE）"
      E2E_NICE_VALUE=0
    fi
  fi
  EXP_SIG+=" cpu_pin_mode=${E2E_CPU_PIN_MODE} push_core=${PUSH_CORE:-na} pull_core=${PULL_CORE:-na}"
  EXP_SIG+=" sched_mode=${E2E_SCHED_MODE} sched_prio=${E2E_SCHED_PRIO} use_chrt=${USE_CHRT} use_nice=${USE_NICE} nice=${E2E_NICE_VALUE}"
  echo "$EXP_SIG"

  local PORT="${SIGNALING##*:}"
  [[ "$PORT" =~ ^[0-9]+$ ]] || PORT=8765
  local PUSH_PID="" SIG_PID=""
  cleanup() {
    [[ -n "${PUSH_PID:-}" ]] && kill "$PUSH_PID" 2>/dev/null || true
    [[ -n "${SIG_PID:-}" ]] && kill "$SIG_PID" 2>/dev/null || true
  }
  trap cleanup EXIT INT TERM

  if [[ "$USE_TASKSET" -eq 1 && -n "$SIG_CORE" ]]; then
    taskset -c "$SIG_CORE" "$SIGNAL_BIN" "$PORT" &
  else
    "$SIGNAL_BIN" "$PORT" &
  fi
  SIG_PID=$!
  sleep 1

  local push_cmd=("$PUSH_BIN" "--config" "$CONFIG" "--signaling" "$SIGNALING" "$STREAM_ID" "$CAMERA")
  if [[ "$USE_TASKSET" -eq 1 ]]; then
    push_cmd=(taskset -c "$PUSH_CORE" "${push_cmd[@]}")
  fi
  if [[ "$USE_CHRT" -eq 1 ]]; then
    push_cmd=(chrt -f "$E2E_SCHED_PRIO" "${push_cmd[@]}")
  elif [[ "$USE_NICE" -eq 1 ]]; then
    push_cmd=(nice -n "$E2E_NICE_VALUE" "${push_cmd[@]}")
  fi
  "${push_cmd[@]}" >"$PUSH_LOG" 2>&1 &
  PUSH_PID=$!

  local deadline=$((SECONDS + PUSH_READY_SEC))
  while [[ "$SECONDS" -lt "$deadline" ]]; do
    if rg -n "CameraVideoTrackSource::Start failed|\\[Main\\] Start failed" "$PUSH_LOG" >/dev/null 2>&1; then
      echo "推流启动失败，见日志: $PUSH_LOG" >&2
      return 3
    fi
    if rg -n "\\[PushStreamer\\] Video capture started|\\[Main\\] Publisher started" "$PUSH_LOG" >/dev/null 2>&1; then
      break
    fi
    if ! kill -0 "$PUSH_PID" 2>/dev/null; then
      echo "推流进程提前退出，见日志: $PUSH_LOG" >&2
      return 3
    fi
    sleep 0.4
  done
  sleep "${E2E_AFTER_PUSH_READY_SLEEP_SEC:-3}"

  local E2E_UI_MODE="${E2E_UI_MODE:-0}"
  local USE_XVFB=0
  local pull_mode_args=()
  if [[ "$E2E_UI_MODE" == "1" || "$E2E_UI_MODE" == "true" || "$E2E_UI_MODE" == "yes" ]]; then
    pull_mode_args=(--frames "$FRAMES" --timeout-sec "$TIMEOUT")
    [[ -z "${DISPLAY:-}" ]] && command -v xvfb-run >/dev/null 2>&1 && USE_XVFB=1
  else
    pull_mode_args=(--headless --frames "$FRAMES" --timeout-sec "$TIMEOUT")
  fi
  local pull_cmd=("$PULL_BIN" "--config" "$CONFIG" "${pull_mode_args[@]}" "$SIGNALING" "$STREAM_ID")
  if [[ "$USE_XVFB" -eq 1 ]]; then
    pull_cmd=(xvfb-run -a "${pull_cmd[@]}")
  fi
  if [[ "$USE_TASKSET" -eq 1 ]]; then
    pull_cmd=(taskset -c "$PULL_CORE" "${pull_cmd[@]}")
  fi
  if [[ "$USE_CHRT" -eq 1 ]]; then
    pull_cmd=(chrt -f "$E2E_SCHED_PRIO" "${pull_cmd[@]}")
  elif [[ "$USE_NICE" -eq 1 ]]; then
    pull_cmd=(nice -n "$E2E_NICE_VALUE" "${pull_cmd[@]}")
  fi

  set +e
  timeout "$((TIMEOUT + 30))" "${pull_cmd[@]}" >"$PULL_LOG" 2>&1
  local PULL_RC=$?
  set -e
  if [[ "$E2E_UI_MODE" != "0" && "$PULL_RC" -eq 124 ]]; then
    PULL_RC=0
  fi

  kill "$PUSH_PID" 2>/dev/null || true
  kill "$SIG_PID" 2>/dev/null || true
  wait "$PUSH_PID" 2>/dev/null || true
  wait "$SIG_PID" 2>/dev/null || true

  echo "日志："
  echo "  push: $PUSH_LOG"
  echo "  pull: $PULL_LOG"
  [[ "$PULL_RC" -ne 0 ]] && { echo "拉流进程退出码: $PULL_RC"; return "$PULL_RC"; }

  if [[ -f "${TOOLS_DIR}/parse_e2e_latency.py" ]]; then
    echo ""
    echo "--- E2E parse ---"
    local parse_args=()
    [[ "$E2E_SKIP_FIRST_PAIRS" =~ ^[0-9]+$ ]] && [[ "$E2E_SKIP_FIRST_PAIRS" -gt 0 ]] && parse_args+=(--skip-first-pairs "$E2E_SKIP_FIRST_PAIRS")
    python3 "${TOOLS_DIR}/parse_e2e_latency.py" "${parse_args[@]}" "$PUSH_LOG" "$PULL_LOG" || true
  fi
  if [[ -f "${TOOLS_DIR}/summarize_push_pipeline.py" ]]; then
    echo ""
    echo "--- 推流端编码链统计 ---"
    python3 "${TOOLS_DIR}/summarize_push_pipeline.py" "$PUSH_LOG" || true
  fi
  if [[ -f "${TOOLS_DIR}/summarize_server_e2e_tx.py" ]]; then
    echo ""
    echo "--- 推流端逐帧 E2E_TX 分段 ---"
    python3 "${TOOLS_DIR}/summarize_server_e2e_tx.py" "$PUSH_LOG" || true
  fi
  if [[ -f "${TOOLS_DIR}/analyze_latency_report.py" ]]; then
    echo ""
    echo "--- 综合分析 ---"
    python3 "${TOOLS_DIR}/analyze_latency_report.py" "$PUSH_LOG" "$PULL_LOG" || true
  fi
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  echo "Usage:"
  echo "  ./scripts/pull.sh [--gui] [signaling_addr] [stream_id]"
  echo "  ./scripts/pull.sh --e2e [signaling] [stream] [camera]"
  echo ""
  echo "普通拉流：PULL_LOWLATENCY_PROFILE=aggressive|balanced|stable|steady|off（默认 aggressive）"
  echo "E2E 关键参数："
  echo "  E2E_LOWLAT_PROFILE=aggressive|balanced|stable|steady|off"
  echo "  E2E_CPU_PIN_MODE=auto|off + E2E_PUSH_CORE/E2E_PULL_CORE/E2E_SIGNALING_CORE"
  echo "  E2E_SCHED_MODE=auto|rr|off + E2E_SCHED_PRIO=1..99"
  echo "  E2E_UI_MODE=1|0  E2E_FRAMES E2E_PULL_TIMEOUT_SEC E2E_SKIP_FIRST_PAIRS"
  echo ""
  echo "说明：libRoboFlow 的 playout/decode queue 相关变量使用 RFLOW_* 命名。"
  exit 0
fi

if [[ "${1:-}" == "--e2e" ]]; then
  shift
  run_e2e "${1:-127.0.0.1:8765}" "${2:-livestream}" "${3:-/dev/video0}"
  exit $?
fi

[[ -x "$PULL_BIN" ]] || { echo "缺少可执行文件，请先运行 ./scripts/build.sh"; exit 1; }

PULL_LOWLATENCY_PROFILE="${PULL_LOWLATENCY_PROFILE:-aggressive}"
apply_rflow_lowlat_profile "$PULL_LOWLATENCY_PROFILE"
export RFLOW_ENABLE_DECODE_QUEUE_GUARD="${RFLOW_ENABLE_DECODE_QUEUE_GUARD:-1}"
export RFLOW_DECODE_QUEUE_GUARD_CAP="${RFLOW_DECODE_QUEUE_GUARD_CAP:-8}"
export WEBRTC_MPP_H264_DEC_LOW_LATENCY="${WEBRTC_MPP_H264_DEC_LOW_LATENCY:-1}"

HEADLESS="${HEADLESS:-1}"
if [[ "${1:-}" == "--gui" || "${1:-}" == "-w" ]]; then
  HEADLESS=0
  shift
fi
if [[ "$HEADLESS" != "1" && "$HEADLESS" != "true" && "$HEADLESS" != "yes" ]]; then
  export WEBRTC_PULL_UI_POLL_SLEEP_MS="${WEBRTC_PULL_UI_POLL_SLEEP_MS:-1}"
fi

SIGNALING="${1:-}"
STREAM_ID="${2:-}"
CMD=("$PULL_BIN" "--config" "$CONFIG")
if [[ "$HEADLESS" == "1" || "$HEADLESS" == "true" || "$HEADLESS" == "yes" ]]; then
  if [[ "${HEADLESS_FRAMES:-}" =~ ^[1-9][0-9]*$ ]]; then
    CMD+=("--headless" "--frames" "$HEADLESS_FRAMES" "--timeout-sec" "${HEADLESS_TIMEOUT:-120}")
  else
    CMD+=("--headless" "--frames" "0")
  fi
fi
[[ -n "$SIGNALING" ]] && CMD+=("$SIGNALING")
[[ -n "$STREAM_ID" ]] && CMD+=("$STREAM_ID")
exec "${CMD[@]}"

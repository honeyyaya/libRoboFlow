# apps/

libRoboFlow 自带的可执行入口与全量编译脚本。

## 二进制目标

| 二进制 | 用途 |
|---|---|
| `signaling_server` | 纯 POSIX TCP/epoll 信令服务器，零 webrtc 依赖；本地联调与 e2e 都先把它跑起来 |
| `push_demo_sdk` | 用 `librflow_svc` 公共 ABI 推流；Linux 默认 SDK 内建 V4L2 采集，支持 USB 相机热插拔自动恢复 |
| `pull_demo_sdk` | 用 `librflow_client` 公共 ABI 订阅远端流并打印帧统计 |

`apps/common/` 为 demo 私有代码（不进 SDK）：`demo_helpers`（推/拉共用）、`demo_camera_hotplug` + `demo_push_session`（仅 `push_demo_sdk`）。

## 编译

```bash
./apps/build.sh                 # 全量构建（SDK + demo + 单测），产物落 build/
./apps/build.sh --debug         # Debug：保留符号、关闭优化
./apps/build.sh --werror        # CI 守卫：-Werror + 分层结构检查
./apps/build.sh -h              # 看全部开关
```

跑单测：`./apps/build.sh && cd build && ctest --output-on-failure`。

只想出库（不要 demo / 单测），用 `scripts/build_linux_arm64.sh`。

## 本地联调

三个进程，三个终端：

```bash
# 1) 信令
./build/apps/signaling_server 8765

# 2) 推流（SDK 内建采集，640x480@30；相机路径见下文 push_demo_sdk）
./build/apps/push_demo_sdk 127.0.0.1:8765 demo_device 1280 720 60 0

# 3) 拉流
./build/apps/pull_demo_sdk 127.0.0.1:8765 demo_device 0
```

`pull_demo_sdk` 参数：`signal_url device_id stream_idx`。

### push_demo_sdk 启动说明

```text
./push_demo_sdk <signaling_url> [device_id] [width] [height] [fps] [stream_idx] [camera]
```

| 参数 | 默认 | 说明 |
|------|------|------|
| `signaling_url` | `127.0.0.1:8765` | 信令地址 |
| `device_id` | `demo_device` | 设备 ID，与拉流端一致 |
| `width` / `height` | `1280` / `720` | 采集与编码目标分辨率 |
| `fps` | `60` | 目标帧率 |
| `stream_idx` | `0` | 逻辑流索引 |
| `camera` | 见下 | **仅 Linux**：V4L2 采集节点，如 `/dev/video12` |

**Linux 相机路径优先级**（未传第 7 参数时）：

1. 环境变量 `RFLOW_PUSH_DEMO_CAMERA`（非空）
2. 否则 udev `capture` 节点按 **评分** 选择（USB + 名称含 Camera 优先；可用 `RFLOW_PUSH_DEMO_CAMERA_MATCH` 子串加权）
3. 若均不可用则回退 `/dev/video0` 并打印提示

**环境变量（可选）**

| 变量 | 说明 |
|------|------|
| `RFLOW_PUSH_DEMO_CAMERA` | 固定相机路径 |
| `RFLOW_PUSH_DEMO_CAMERA_MATCH` | 自动选相机时 ID_MODEL 子串匹配加分 |
| `RFLOW_PUSH_DEMO_MAX_FAILURES` | 连续推流/信令失败达 N 次后退出（exit=2）；0 或不设=不退出 |

信令 TCP 断开时 Publisher 会触发 `RFLOW_CONN_DISCONNECTED`，demo 先拆流再退避重连；仅在 `connect` 失败时才 `disconnect` 复位 SDK 状态机。

示例：

```bash
# 指定相机节点
./build/apps/push_demo_sdk 127.0.0.1:8765 demo_device 1280 720 60 0 /dev/video0

# 或用环境变量（可省略第 7 参数）
export RFLOW_PUSH_DEMO_CAMERA=/dev/video2
./build/apps/push_demo_sdk 127.0.0.1:8765 demo_device 1280 720 60 0
```

**USB 热插拔（Linux）**：demo 使用 **libudev** 监听 `video4linux` 的 `add`/`remove`。拔掉配置节点后 `stop_stream` + `destroy_stream`；同一物理相机（`ID_SERIAL` / devpath 身份）插到**新** `/dev/videoN` 时会 `devnode migrated` 并自动跟路径重建推流。稳定约 1.5s 且可 `open` 后再 `start_stream`。

编译依赖：Linux 上需 `libudev`（如 `libudev-dev` / `pkg-config --libs libudev`）。

相关日志关键字：

```text
[demo][udev] libudev monitor video4linux devnode=/dev/video0
[demo][udev] camera removed: /dev/video0
[demo][udev] camera added: /dev/video0
[demo] stream restarted
```

注意：

- libudev 不可用时改为每 2s 探测设备节点是否存在；udev 正常时推流中每 5s 核对一次防漏事件。
- 重建推流前仍会 `open` 确认可采集；推流进行中仅用节点存在性判断掉线，避免与 SDK 抢设备。
- 若换了一台**不同**相机（序列号变了），不会自动跟过去；请改参数/环境变量或 udev symlink。
- 非 Linux 平台使用 `video_device_index=0`，无 udev 监控。

## 一键 e2e 冒烟（手敲版本）

```bash
PORT=8765
./build/apps/signaling_server $PORT &
SIG_PID=$!
sleep 0.5

./build/apps/push_demo_sdk 127.0.0.1:$PORT demo_device 640 480 30 0 >/tmp/push.log 2>&1 &
PUSH_PID=$!
sleep 5

timeout 20 ./build/apps/pull_demo_sdk 127.0.0.1:$PORT demo_device 0 2>&1 | tee /tmp/pull.log
grep -q 'frame#1' /tmp/pull.log && echo "[ok] e2e green" || echo "[fail] no frames"

kill $PUSH_PID $SIG_PID 2>/dev/null
```

## 接入业务方

直接抄 `push_demo_sdk.cpp` / `pull_demo_sdk.cpp` 的调用顺序即可——它们只用公共 C ABI，没有任何对 `service/impl` 内部头文件的依赖。

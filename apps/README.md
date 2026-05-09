# apps/

libRoboFlow 自带的可执行入口与全量编译脚本。

## 二进制目标

| 二进制 | 用途 |
|---|---|
| `signaling_server` | 纯 POSIX TCP/epoll 信令服务器，零 webrtc 依赖；本地联调与 e2e 都先把它跑起来 |
| `push_demo_sdk` | 用 `librflow_svc` 公共 ABI 推合成 I420（或外部 V4L2 相机）的最小可运行示例 |
| `pull_demo_sdk` | 用 `librflow_client` 公共 ABI 订阅远端流并打印帧统计 |

`common/demo_helpers.{h,cpp}` 是上述两个 SDK demo 共享的私有 helper（SIGINT/SIGTERM 处理、连接/流状态打印、相机路径解析），不进 SDK，也不被外部使用。

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

# 2) 推流（合成图像，640x480@30；想用真实相机改 RFLOW_PUSH_DEMO_CAMERA=/dev/videoN）
./build/apps/push_demo_sdk 127.0.0.1:8765 demo_device 640 480 30 0

# 3) 拉流
./build/apps/pull_demo_sdk 127.0.0.1:8765 demo_device 0
```

`push_demo_sdk` 的 6 个位置参数依次为 `signal_url device_id width height fps stream_idx`；`pull_demo_sdk` 是 `signal_url device_id stream_idx`。

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

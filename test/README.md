# test/

libRoboFlow 单元测试（GoogleTest）。

## 跑测

前置：先用 `apps/build.sh` 构建（默认会带上 `RFLOW_BUILD_TESTS=ON`），产物落 `build/`。

```bash
./test/run_unit_tests.sh
```

等价于 `cd build && ctest --output-on-failure`，外加构建期补全所有 `test_*` 目标。

## 目录

```
test/
├── CMakeLists.txt           # 按 .cpp 一个 target 的方式接入 GTest
├── run_unit_tests.sh        # ctest 友好封装
├── unit/                    # 单元测试源（test_<被测模块>.cpp）
├── demo_client_init.c       # ABI 兼容性烟囱（C 代码 link librflow_client）
└── demo_service_init.c      # 同上 client/service ABI
```

新增单测：在 `unit/` 下加 `test_<module>.cpp`，然后到 `test/CMakeLists.txt` 末尾用 `add_rflow_unit_test(...)` 注册。所测源直接编进单测目标（不复用 OBJECT 库），避免被 service WebRTC 链接关系绑死。

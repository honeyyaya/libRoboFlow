# tools/

开发期辅助脚本，不打进任何运行时产物。

| 脚本 | 谁在用 | 用途 |
|---|---|---|
| `check_structure_rules.py` | CMake (`-DRFLOW_ENABLE_STRUCTURE_CHECK=ON`) | 校验 `src/` 的分层规则（include 方向、命名空间约束等），CI 必跑 |
| `parse_e2e_latency.py` | 手工 | 配对推/拉日志里的 `[E2E_TX]`/`[E2E_RX]`，输出全链路 p50/p95/p99 |
| `analyze_latency_report.py` | 手工 | 综合摘要 + 延迟来源说明，定位主要瓶颈段 |

打 trace 日志的开关由环境变量控制（见 `docs/RUNTIME_KNOBS.md`）：

```bash
export RFLOW_E2E_LATENCY_TRACE=1
./build/apps/push_demo_sdk ... 2>push.log &
./build/apps/pull_demo_sdk ... 2>pull.log
python3 tools/parse_e2e_latency.py push.log pull.log
python3 tools/analyze_latency_report.py push.log pull.log
```

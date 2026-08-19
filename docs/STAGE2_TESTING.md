# 阶段 2 验证说明

阶段 2 按 S2.1-S2.6 分项验收。本文同时说明当前已经存在的自动化入口和后续任务必须补齐的验证证据。

## 1. 统一命令

在已配置 `QT_ROOT`、`VCPKG_ROOT` 和 MSVC x64 环境后执行：

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug --output-on-failure
```

发布配置使用对应的 `windows-msvc-release` preset。仓库封装入口为：

```powershell
./scripts/Invoke-ModelHarborBuild.ps1 -Action all -Preset windows-msvc-debug
./scripts/Invoke-ModelHarborBuild.ps1 -Action all -Preset windows-msvc-release
```

## 2. S2.1 数据库与迁移

CTest 用例 `modelharbor.persistence` 覆盖：

- 空库按顺序迁移到 schema 2，并检查全部基础表。
- 只应用 schema 1、写入数据、关闭后升级到 schema 2，原数据保持不变。
- 已发布迁移内容改变、版本不连续和迁移元数据不一致时拒绝继续。
- 故障迁移中的建表和写入整体回滚，版本号不前移。
- 外键、WAL、`synchronous=NORMAL`、`user_version` 和 `integrity_check`。
- Repository 参数绑定、正常提交和 RAII 回滚。
- 独立进程在未提交事务中退出后的 WAL 恢复。
- Online Backup 快照和重复备份的原子替换。

`modelharbor.ipc.integration` 还会使用临时数据目录启动真实网关，确认网关完成迁移并在 ping 中报告 `database_schema_version = 2`，随后验证关闭和同目录重启。

人工审阅入口：

1. 检查 [数据库基线](DATABASE_SCHEMA.md) 的表分组、删除策略和状态字段。
2. 检查 `resources/migrations/0001_initial.sql` 与 `0002_import_jobs.sql`。
3. 检查 `tests/persistence_test.cpp` 中的升级、回滚、恢复和快照断言。

## 3. S2.2-S2.6 验证矩阵

| 任务 | 自动化重点 | 用户验收入口 |
| --- | --- | --- |
| S2.2 秘密与本地 Key | DPAPI 往返与篡改、HMAC 稳定性、本地 Key 创建/校验/轮换/撤销、日志/数据库明文扫描 | 查看掩码、Key 只展示一次、撤销后立即失效 |
| S2.3 导入读取器 | `sub2api-data` v1、旧 bundle、Codex 对象/数组/JSONL、BOM/Unicode/畸形输入、未知字段 | 逐种去敏文件查看识别格式、版本和逐条解析结果 |
| S2.4 规范化与预检 | 字段映射、代理引用、确定性去重键、数值边界、四种预检分类 | 检查可导入/异常/已存在/无效的解释和掩码 |
| S2.5 导入任务 | 全批回滚/跳过坏条目、只提交选中项、取消、继续、重复导入幂等、无孤立代理 | 查看任务进度、逐条结果、取消与继续后的最终数量 |
| S2.6 账号池 UI | Model/View 大数据、异步任务、完整页面状态、隐私模式、批量操作 | 检查 1100×700/1280×800/1920×1080 和 100%/150%/200% DPI |

阶段级验收还要执行测试数据敏感词扫描，并抽查数据库、日志和导出物中没有夹具明文凭据。S2.2-S2.6 未进入各自验收点前，本表只作为必须达到的验证计划。


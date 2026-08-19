# ModelHarbor 数据库基线

本文记录阶段 2 的 SQLite 持久化边界，供迁移审阅和后续 S2.2-S2.6 开发使用。当前 schema 版本为 `2`。

## 1. 所有权与文件位置

- 只有 `modelharbor-gateway` 打开、迁移和写入数据库；Desktop 继续通过 IPC 调用用例。
- 默认数据库位于网关的 `QStandardPaths::AppLocalDataLocation/modelharbor.db`。
- 测试和诊断可通过 `--data-dir DIRECTORY` 指定隔离目录；该参数不改变回环监听边界。
- 每次连接启用 `foreign_keys=ON`、WAL、`busy_timeout=5000`、`synchronous=NORMAL` 和内存临时表。

## 2. 迁移版本

| 版本 | 文件 | 内容 |
| --- | --- | --- |
| 1 | `resources/migrations/0001_initial.sql` | 站点、秘密引用、代理、凭据、渠道、模型、路由、本地 Key、健康、请求、统计、指纹、价格和成本试验基础表 |
| 2 | `resources/migrations/0002_import_jobs.sql` | 渠道状态事件、导入任务、导入条目及查询索引 |

构建时从上述 SQL 文件生成内嵌迁移内容，因此发布包启动不依赖外部 SQL 文件。测试会比较 SQL 文件与内嵌版本的校验值，避免两份内容漂移。

`schema_migrations` 保存版本、名称、内容校验值和 UTC 应用时间，并与 `PRAGMA user_version` 双向核对。已经应用的迁移内容发生变化、版本不连续或数据库版本高于程序支持版本时，网关会停止初始化，而不是猜测性修复。

## 3. 表分组

| 分组 | 表 |
| --- | --- |
| 配置与资源 | `app_settings`、`sites`、`proxies`、`account_groups`、`tags` |
| 凭据与渠道 | `secrets`、`credentials`、`credential_tags`、`channels` |
| 模型与路由 | `logical_models`、`channel_models`、`routes`、`route_members`、`route_cursors` |
| 本地访问 | `local_api_keys` |
| 健康与可观测性 | `health_checks`、`request_records`、`daily_usage`、`channel_state_events` |
| 诊断与成本 | `fingerprint_runs`、`pricing_tables`、`cost_experiments` |
| 导入流水线 | `import_jobs`、`import_job_items` |

## 4. 固定数据规则

- 主键使用 SQLite `INTEGER PRIMARY KEY`；通过 IPC 返回时转换为字符串。
- 持久化时间统一为 UTC 毫秒，字段以 `_utc_ms` 结尾。
- 金额保存为整数微美元，倍率保存为百万分比整数，不使用 `double` 累计。
- Token 桶分列保存；未知 Token 保持 `NULL`。
- `secrets` 只为 S2.2 预留密文列和版本元数据，业务表只保存 secret 引用。
- 凭据运行状态和导入状态分别保存，导入落库不自动等于 `schedulable`。
- 外键删除策略按数据语义使用 `CASCADE`、`SET NULL` 或 `RESTRICT`，连接初始化后强制启用外键检查。
- Repository 只使用参数绑定；数据库错误携带结构化 SQLite 错误码和操作上下文，不拼接参数值。

## 5. 事务、恢复与快照

- `Transaction` 默认使用 `BEGIN IMMEDIATE`，通过 RAII 在未提交路径执行回滚。
- 每个迁移在独立事务中执行；SQL、迁移记录和 `user_version` 同时提交或同时回滚。
- 崩溃恢复测试通过独立 helper 在存在未提交事务时直接结束进程，再重开 WAL 数据库，检查已提交数据、未提交数据和 `integrity_check`。
- 快照使用 SQLite Online Backup API 写入同目录临时文件，完成后通过 Windows 原子替换发布目标文件；快照可独立打开并执行完整性检查。
- 普通快照未来只保存 DPAPI 密文。跨 Windows 用户迁移的便携加密备份仍属于后续能力。


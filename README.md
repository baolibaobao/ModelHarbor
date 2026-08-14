# ModelHarbor

> 正式名称：ModelHarbor（中文名：模港）。当前状态：**阶段 1：工程骨架与技术验证**。

## 名称建议

已选定发布名为 **ModelHarbor（中文名：模港）**：它表达多站点聚合、账号停泊和模型路由港口的定位。代码仓库、IPC 方法和首版 API 前缀统一使用 `ModelHarbor`。

候选名称（尚未做商标和域名排查）：

| 名称 | 中文名 | 定位印象 |
| --- | --- | --- |
| `ModelDock` | 模枢 | 模型停靠、账号池和本地控制台 |
| `RelayDesk` | 中转台 | 强调本地中转与桌面管理 |
| `ModelHarbor` | 模港 | 已选定，强调多站点聚合和账号停泊 |
| `ModelDeck` | 模型甲板 | 强调集中管理、试跑和诊断 |
| `ModelPort` | 模港口 | 强调统一 API 出入口 |
| `ModelStation` | 模型站 | 稳重、直观，适合桌面软件 |
| `RelayHub` | 中转枢 | 强调渠道汇聚与自动切换 |
| `ProviderDock` | 供应商坞 | 强调 Provider 和渠道管理 |
| `RoutePilot` | 路由领航 | 强调调度、轮询和故障转移 |
| `RouteMesh` | 路由网 | 强调多渠道编排和模型映射 |
| `KeyMesh` | 钥网 | 强调 API Key 与订阅账号池 |
| `ChannelMesh` | 渠道网 | 强调渠道聚合与健康状态 |
| `ModelSwitch` | 模型切换台 | 强调自动切换和快速试跑 |
| `API Harbor` | API 港 | 强调多站点 API 集合 |
| `ModelVault` | 模型库 | 强调凭据、模型和统计集中管理 |
| `模枢` | 模枢 | 中文候选，短而直接 |
| `聚模台` | 聚模台 | 直白表达模型聚合与操作台 |
| `模港` | 模港 | 已选定，突出站点和账号池汇聚 |
| `模链` | 模链 | 强调模型、渠道和路由连接 |
| `聚钥台` | 聚钥台 | 强调 Key 池和账号管理 |

正式名称已确定为 `ModelHarbor / 模港`；其余名称保留为未来功能、主题或组件的候选名。

ModelHarbor 是一个面向 Windows 的本地 AI API 聚合与诊断工具。它把多个站点、API Key 和订阅账号池统一到一个本地 OpenAI-compatible 地址，并提供模型发现、连通性检测、请求试跑、能力指纹、倍率核算、使用统计、健康路由和 Sub2API 号池导入。

默认本地入口：

```text
http://127.0.0.1:8317/v1
```

客户端只需要配置 ModelHarbor 生成的本地 Key。上游 Key、OAuth 凭据和代理信息保存在本机，并由 Windows DPAPI 加密。

## 第一版目标

- 管理站点、凭据、渠道、模型映射和本地访问 Key。
- 聚合 `GET /v1/models`。
- 代理 `POST /v1/chat/completions`，覆盖流式与非流式响应。
- 记录成功率、首个有效内容时间、总耗时、Token、缓存 Token、估算费用与实扣证据。
- 支持按优先级选组；同优先级可启用严格轮询或平滑加权轮询。
- 支持健康检查、熔断、冷却和有限故障转移。
- 支持参考 New API 的渠道自动禁用、后台探测和自动启用，并保留手动停用优先级。
- 提供模型试跑、输出结构校验和能力指纹测试。
- 提供分平台账号池、分组、标签、配额状态和批量账号管理。
- 支持 OAuth、Token/JSON、API Key 和本地文件导入入口；导入 Sub2API 官方备份结构与常见 Codex 会话 JSON/JSONL，并在导入前预览、校验和去重。
- 提供按模型、渠道、账号和时间范围聚合的本地使用统计。

## 有意控制的边界

第一版是单机个人工具，不建设公开 SaaS 所需的用户系统、充值支付、分销、云端数据库和多节点控制面。首版协议边界明确为 `GET /v1/models` 与 `POST /v1/chat/completions`（流式和非流式）；OpenAI Responses 不在首版实现，放入后续兼容阶段，并单独记录请求/响应映射规则。Anthropic Messages、Gemini 原生协议、图片、音频、Embedding 和 Realtime 也放入后续兼容阶段。

“模型满血测试”在产品中称为**能力指纹**。它输出测试证据、能力得分和映射异常提示，不把单次提示词结果包装成确定的模型身份结论。

## 核心术语

| 术语 | 含义 |
| --- | --- |
| 站点 | 上游服务的 Base URL 与服务类型 |
| 凭据 | API Key、OAuth Token 或 Sub2API 导入的认证数据 |
| 渠道 | 站点 + 凭据 + 模型映射 + 路由参数，可被实际调度 |
| 账号池 | 同一服务类型下可轮换、限流和冷却的多条凭据 |
| 逻辑模型 | ModelHarbor 对本地客户端暴露的模型名 |
| 上游模型 | 某个渠道实际请求使用的模型名 |
| 路由 | 逻辑模型对应的候选渠道集合及调度策略 |

## 推荐技术基线

- C++20
- Qt 6.11.1，Qt Widgets 原生桌面 UI
- CMake + Ninja，Windows x64，MSVC 为发布工具链
- Boost.Asio/Beast 作为本地 HTTP 入口
- libcurl multi 作为上游异步 HTTP 客户端
- SQLite WAL 作为本地数据库
- Windows DPAPI 保护敏感字段
- Qt Test + CTest 作为统一测试入口

首版固定使用 Qt Widgets，以保持纯 C++、稳定的桌面表格体验和较低资源占用。表现层变更属于架构决定，后续必须先更新文档或 ADR 再实施。

## 文档审阅顺序

1. [产品规格](docs/PRODUCT_SPEC.md)
2. [技术架构](docs/ARCHITECTURE.md)
3. [开发流程](docs/DEVELOPMENT_PLAN.md)
4. [协作约束](AGENTS.md)

## 开发阶段门禁

阶段 0 已完成审阅。后续按 [开发流程](docs/DEVELOPMENT_PLAN.md) 的用户门禁推进：每个细分任务必须先收到明确的“进行 Sx.y”指令，完成后等待用户验收，只有验收通过才进入下一项；当前不自动推进后续任务，也不提前实现数据库、账号运行时适配器或业务路由。

参考产品只用于梳理工作流：`ai-relay-manager` 的站点、模型、健康、实验、备份和统计页面；New API 的渠道、计量和自动禁用概念；Sub2API 的账号池、导入与调度概念；[Cockpit Tools](https://github.com/jlcodes99/cockpit-tools) 的本地账号池、分平台添加账号、批量预检和后台任务交互。项目采用独立实现，不复制这些项目的源码。

## 许可证

本项目默认采用 [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/deed.zh-hans)（署名-非商业性使用-相同方式共享），完整说明见 [LICENSE.md](LICENSE.md)。

- 个人学习、研究和非商业场景可以使用与修改，但必须保留署名并遵循相同方式共享。
- 企业内部商业目的、对外商业服务、付费产品集成、二次分售等商业场景须先取得作者书面商业授权。
- 商业授权由作者单独提供书面条款与报价，联系方式：`1305508372@qq.com`；第三方依赖继续遵循其各自许可证。

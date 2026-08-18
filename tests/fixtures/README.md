# 测试夹具

此目录只保存虚构、脱敏且可提交到仓库的测试数据。真实 API Key、Token、Cookie、邮箱和代理密码不得进入夹具。

网络行为由 `tests/support/fake_upstream.*` 编程控制，当前支持普通 JSON、SSE、慢 SSE、断流、401、429、5xx 和畸形正文。后续阶段的导入、数据库和 Provider 夹具按模块建立子目录。

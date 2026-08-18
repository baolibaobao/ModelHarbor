# 阶段 1 构建与测试

## 统一入口

在 PowerShell 中执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File scripts\Invoke-ModelHarborBuild.ps1 `
  -Action all `
  -QtRoot F:\Qt\6.11.1\msvc2022_64 `
  -VcpkgRoot F:\Qt\Tools\vcpkg
```

该命令使用 `windows-msvc-debug` preset，依次完成全新配置、构建和 CTest。标准 CMake 命令仍为：

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug --output-on-failure
```

Release 使用相同入口：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File scripts\Invoke-ModelHarborBuild.ps1 `
  -Action all `
  -Preset windows-msvc-release `
  -QtRoot F:\Qt\6.11.1\msvc2022_64 `
  -VcpkgRoot F:\Qt\Tools\vcpkg
```

## 代码质量

```powershell
scripts\Invoke-CodeQuality.ps1 -Action format-check
scripts\Invoke-CodeQuality.ps1 -Action format
scripts\Invoke-CodeQuality.ps1 -Action tidy
```

`format-check` 与 `format` 自动寻找 Qt Creator 附带的 clang-format；`tidy` 使用构建目录中的 `compile_commands.json`，也可以显式传入工具路径。

## 测试边界

- 全部网络测试只连接 `127.0.0.1` 上的仓库内 fake upstream。
- `modelharbor.http.stream.integration` 验证 1 MiB SSE 增量转发、客户端取消和上游断流。
- `modelharbor.test.support` 验证虚拟时钟、确定性随机源、临时数据目录，以及 401、429、5xx、慢流、断流和畸形响应计划。
- `modelharbor.desktop.smoke` 使用 `QT_QPA_PLATFORM=offscreen` 创建主窗体并检查导航和最小尺寸。
- UI 人工验收覆盖 1100×700、1280×800、1920×1080，以及 100%、150%、200% 缩放；自动 smoke test 只固定结构与基线尺寸，不替代截图检查。

阶段 1 不访问真实上游，不包含 SQLite、DPAPI、Provider adapter、业务路由或 Sub2API 解析。

## Release 打包骨架

Release 构建通过后执行：

```powershell
scripts\New-Stage1Package.ps1 -QtRoot F:\Qt\6.11.1\msvc2022_64
```

脚本在 `dist/modelharbor-stage1/` 生成双进程、Qt 运行库、vcpkg 动态库、项目许可证和自动汇总的 `THIRD_PARTY_NOTICES.md`。当前仅作为阶段 1 可启动包验证；安装器、升级、卸载和正式许可证固定属于阶段 8。

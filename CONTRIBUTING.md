# 参与开发

先阅读 README、docs/architecture.md 和 PRIVACY.md。

- Windows 10/11；支持 C++17 的 MinGW 或 Visual Studio。
- 运行 scripts/build.ps1，或使用 README 中的 CMake/CTest 命令。
- 修复额度解析、登录隔离或进程通信时，请添加能复现问题的脱敏用例。
- 默认测试只能使用 mock_codex，不能依赖真实凭据、网络或付费调用。
- UI 修改需检查默认收起/展开、中文、不同缩放和未知数据的状态。
- Issue / PR 中只提交脱敏错误、版本与重现步骤；不要提交账号目录或授权 URL。
- 不引入遥测、自动兑换重置卡或模型调用，除非有明确设计和用户授权。

提交 PR 时说明行为变化、验证方式和已知限制。Windows CI 应通过后再合并。

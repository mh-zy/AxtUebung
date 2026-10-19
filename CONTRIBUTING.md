# 贡献指南

感谢参与 Axt 刷题。提交 Issue 或 Pull Request 前，请先确认问题属于本项目范围，并尽量提供可复现步骤。

## 开发环境

- C++17；
- CMake 3.22+；
- Ninja；
- Windows：Visual Studio C++ 桌面开发工具；
- Android：JDK、Android SDK、NDK `30.0.16248370`；
- Android Gradle Plugin `9.4.1`、Gradle `9.8.0`、compile SDK 37、min SDK 24。

## 构建与测试

Windows：

```powershell
powershell -ExecutionPolicy Bypass -File tools\build-windows.ps1 -RunTests -KillRunning
```

Android：

```powershell
$env:JAVA_HOME = '<JDK path>'
$env:ANDROID_HOME = '<Android SDK path>'
powershell -ExecutionPolicy Bypass -File tools\build-android.ps1
```

涉及触摸、软键盘、SAF、导入导出或页面布局的修改，请在 Android 设备或模拟器上实际验证，不要只验证 Windows。

## 代码约定

- 核心业务逻辑放在 `core/`，不要引入平台 API；
- UI 只负责把状态画出来和把操作转成会话调用；
- 平台相关能力放在 `platform/`；
- 新行为尽量补核心测试；
- 保持源码 UTF-8，注释可使用中文；
- 不新增联网、统计或第三方 SDK，除非先讨论隐私和许可证影响；
- 修改题库格式时同步更新 `docs/题库格式.md` 和往返测试；
- 修改版本时同步更新 `core/include/quiz/app/Version.h`、`app/build.gradle.kts` 的 `versionName` 与 `versionCode`。

## 提交内容

- 不提交 `build/`、`.vs/`、`out/`、`dist/`、`backups/`、`*.bak-*`、APK、日志和本地题库；
- 不提交 `local.properties`、keystore、密码或签名密钥；
- 不提交用户答题进度和个人数据；
- 新增第三方代码、字体、图标或题库内容时，说明来源和许可证，并更新 `THIRD_PARTY_NOTICES.md` 与 `LICENSES/`。

## Pull Request

建议一个 PR 只处理一个主题，并说明：

- 修改目的；
- 主要实现；
- 验证平台与步骤；
- 已知限制；
- 是否影响题库格式、进度格式或隐私行为。

提交即表示你有权提供该贡献，并同意以项目 MIT License 授权。

## 行为

请保持技术讨论具体、尊重事实。可以批评代码和方案，不要攻击个人。涉及隐私、安全、版权或第三方内容的争议，以停止合并、先核实来源为默认处理方式。

## 联系方式

- 开发者：mh-zy（@gcw_7wG3NXCK）
- 邮箱：jytc4399@163.com
- 仓库 Issue：待公开仓库创建后填写

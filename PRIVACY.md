# 隐私与数据说明 / Privacy and Data Notice

最后更新：2026-10-24

适用范围：题斩 / AxeQuiz（兜底名 AxtUebung）1.0.x 及后续明确引用本说明的版本

## 中文

### 1. 结论

题斩是一个离线、本地优先的刷题工具。

- 应用本身不连接互联网；
- 没有账号、登录、云同步、广告、支付或订阅；
- 不接入统计、崩溃上报、推送或其他会主动上传数据的第三方 SDK；
- 开发者不会通过应用自动收集、出售或共享个人信息。

当前 Android 构建没有申请 `INTERNET` 权限。导入、导出和剪贴板操作只在用户主动触发时发生。

设置页的反馈入口只在用户主动操作时打开系统浏览器或将 Issue 地址写入剪贴板，应用本身不直接访问网络；是否提交、提交哪些内容由用户自行决定。

### 2. 本地保存的数据

应用可能在用户设备的应用私有目录中保存：

- 用户导入或创建的题库；
- 答题进度、对错次数、收藏、标签和答题时间；
- 应用设置、主题和语言选择；
- 用户添加的提示词；
- 用户通过系统文件选择器导入或导出的文件。

这些数据用于提供刷题、统计、恢复设置和导出功能，不会由应用自动发送给开发者或第三方。

### 3. 系统文件选择器

导入和导出使用 Android Storage Access Framework 或 Windows 系统文件对话框。应用只访问用户明确选择的文件或位置。Android 构建不申请广泛的外部存储读写权限。

### 4. Android 系统备份

当前 Android Manifest 设置了 `android:allowBackup="false"`，应用私有数据不纳入 Android 系统备份。卸载应用或清除应用数据后，本地题库、进度和设置不会由系统自动恢复。请使用应用内导出功能自行备份重要数据。

### 5. 日志

应用可能在 Windows 控制台或 Android logcat 中输出运行日志。日志仅保留在用户设备上，不会由应用自动上传。用户主动反馈问题时，可以自行复制并提供相关日志；请先检查其中是否包含不想公开的题库名称、文件路径或其他信息。

### 6. 数据删除

- 可在应用内删除题库、清除题库进度或清除数据；
- 卸载 Android 应用会删除应用私有目录中的本地数据；
- Windows 版数据位于程序目录下的 `data/`，可由用户直接备份或删除。

卸载或清除数据前，请先使用应用内的导出功能备份重要题库和进度。

### 7. 儿童、广告与跨境传输

本应用不面向广告投放，不建立用户画像，不提供账号或社交功能，也不会由应用主动向境外服务器传输个人信息。

### 8. 联系方式

开发者：mh-zy（@gcw_7wG3NXCK）  
AtomGit Issues：https://atomgit.com/gcw_7wG3NXCK/AxtUebing/issues
GitHub Issues：https://github.com/mh-zy/AxtUebung/issues

请在公开 Issue 中只描述一般性问题，不要附带个人信息、题库原文或其他敏感数据。

---

## English summary

AxeQuiz is a local-first, offline quiz application.

- The app itself does not access the internet.
- There are no accounts, cloud sync, ads, payments, analytics, or crash-upload SDKs.
- Quiz banks, progress, settings, themes, prompts, and imported/exported files are stored locally or handled through user-initiated system file dialogs.
- The app does not automatically collect, sell, or share personal information with the developer or third parties.
- Feedback controls only hand an Issue URL to the system browser or clipboard after a user action. The app itself does not access the network.
- Android system backup is disabled through `android:allowBackup="false"`. Local data is not restored automatically after uninstalling or clearing the app. Use the in-app export function to keep backups.
- Logs remain on the local device unless the user chooses to copy and share them.

For privacy questions or corrections, submit an issue without including private data:

- AtomGit: https://atomgit.com/gcw_7wG3NXCK/AxtUebing/issues
- GitHub: https://github.com/mh-zy/AxtUebung/issues

# 第三方软件与资源许可

最后更新：2026-10-24

题斩 / AxeQuiz 本体的源代码和原创资源采用 MIT 许可证，见根目录 `LICENSE`。以下第三方组件继续适用其各自许可证。发布源码或二进制时，应同时保留本文件和 `LICENSES/` 中的对应许可文本。

## 运行时组件

| 组件 | 用途 | 许可证 | 许可文本 |
| --- | --- | --- | --- |
| Dear ImGui | 界面绘制 | MIT | `LICENSES/Dear-ImGui-MIT.txt` |
| stb（随 ImGui 内置） | 字体/文本编辑等辅助代码 | Public Domain，部分可选 MIT | `LICENSES/stb-MIT.txt` |
| stb_image 2.30 | 首页背景 PNG/JPEG/BMP 解码 | Public Domain / MIT | `LICENSES/stb-MIT.txt` |
| Kotlin stdlib 2.2.10 | Android 运行时依赖 | Apache License 2.0 | `LICENSES/Apache-2.0.txt` |
| JetBrains annotations 13.0 | Kotlin 运行时注解 | Apache License 2.0 | `LICENSES/Apache-2.0.txt` |
| Android NDK / NativeActivity / libc++ | Android 原生构建与运行支持 | Apache License 2.0 及适用例外 | `LICENSES/Apache-2.0.txt`、`LICENSES/Android-NDK-NOTICE.txt` |

## 字体

| 资源 | 版权/来源 | 许可证 | 许可文本 |
| --- | --- | --- | --- |
| Noto Sans SC Regular | Copyright 2012 Google Inc. | SIL Open Font License 1.1 | `LICENSES/NotoSansSC-OFL.txt` |
| LXGW Marker Gothic Regular（霞鹜漫黑） | LXGW Marker Gothic project，基于 Tanugo | SIL Open Font License 1.1 | `LICENSES/LXGWMarkerGothic-OFL.txt` |

正文字体与首页标题字体均位于 `assets/fonts/`。Noto Sans SC 的原始许可同时保留在该目录的 `OFL.txt`；LXGW Marker Gothic 只随项目保留顶层规范化 TTF 和对应许可证。

## 图标

| 图标集 | 本项目中的识别方式 | 许可证 | 许可文本 |
| --- | --- | --- | --- |
| Tabler Icons | 5 个 SVG，`class` 含 `icons-tabler` | MIT | `LICENSES/Tabler-Icons-MIT.txt` |
| Lucide / Lucide 派生 | 9 个 SVG；其中 `m-file-x.svg` 含项目自绘修改 | ISC | `LICENSES/Lucide-ISC.txt` |

其余图标为项目自绘资源，随项目按 MIT License 授权。逐文件来源见 `docs/图标来源清单.md`。

## 构建工具

Gradle Wrapper、Android Gradle Plugin、CMake、Ninja、Visual Studio 和 Android SDK 主要作为构建工具使用。其中 Gradle 使用 Apache License 2.0；其他工具按其各自许可证执行。公开二进制发行版应保留相应许可信息。

## 题库与提示词

程序代码和题库格式的许可证，不自动覆盖仓库内示例题库、提示词或其他文本内容的版权。发布前应确认这些内容有权随项目公开。

## 修订

发现遗漏或错误时，请通过以下方式联系：

- 开发者：mh-zy（@gcw_7wG3NXCK）
- AtomGit Issues：https://atomgit.com/gcw_7wG3NXCK/AxtUebing/issues
- GitHub Issues：https://github.com/mh-zy/AxtUebung/issues

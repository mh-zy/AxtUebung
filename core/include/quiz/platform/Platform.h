#pragma once

#include <string>
#include <vector>

// ============================================================================
// 平台接口：core / ui 需要的全部“系统能力”都从这里取
// ----------------------------------------------------------------------------
// 每个平台各实现一份：platform/windows、platform/android。
// 这样 core 与 ui 里不会出现任何 Windows / Android 专有 API，双端共用一份代码。
// ============================================================================
namespace quiz::platform {

/// 平台名，例如 "Windows" / "Android"，显示在界面上
const char* name();

/// 日志：level 0=信息 1=警告 2=错误
void writeLog(int level, const char* message);

/// 单调时钟（秒），用于计时
double monotonicSeconds();

/// 本地时间字符串："2026-10-02 15:04:05"
std::string nowString();

/// 紧凑时间戳："20261002-150405"，用于导出文件命名
std::string nowStampCompact();

/// 可写数据目录（末尾不带分隔符）。Windows：exe 旁的 data/；Android：应用内部存储
std::string dataDirectory();

/// 列出资源目录（打包进程序的那一份）里指定后缀的文件名
/// Windows：<exe>/assets/<dir>；Android：APK 的 assets/<dir>
std::vector<std::string> listAssetFiles(const std::string& dir, const std::string& suffix);

/// 列出数据目录（用户可写）里指定后缀的文件名
std::vector<std::string> listDataFiles(const std::string& dir, const std::string& suffix);

/// 读取资源文本（assets/<name>）
bool readAssetText(const std::string& name, std::string& outText);

/// 读取普通文件
bool readTextFile(const std::string& path, std::string& outText);

/// 写普通文件（父目录不存在时自动创建）
bool writeTextFile(const std::string& path, const std::string& text);

/// 文件是否存在
bool fileExists(const std::string& path);

/// 删除文件（不存在也算失败，调用方自己决定怎么提示）
bool removeFile(const std::string& path);

/// 拼接路径（自动处理分隔符）
std::string joinPath(const std::string& directory, const std::string& name);

/// 一种「界面比例」预设：code 是「宽:高」，width/height 是建议的客户区像素尺寸
struct AspectPreset {
    std::string code;
    int width = 0;
    int height = 0;
};

/// 本平台提供的界面比例预设（Windows 与 Android 的清单不一样）
std::vector<AspectPreset> aspectPresets();

/// 请求把窗口客户区调整成指定像素尺寸：真正调整会推迟到「下一帧开始前」，
/// 避免在 ImGui 帧中途改窗口；平台不支持时返回 false（Android 的窗口由系统决定）
bool setWindowClientSize(int width, int height);

/// 平台层每帧开始前调用：把上面登记的窗口尺寸真正应用掉（没有登记就什么都不做）
void applyPendingWindowResize();

/// 把文本写入系统剪贴板；平台不支持时返回 false
/// （不用 ImGui::SetClipboardText：那个依赖后端注册函数，Android 上会静默失败，
///   界面就没法提示“复制失败”）
bool setClipboardText(const std::string& text);

/// 是否有物理键盘：触屏端没有。用来隐藏「按 Ctrl+C」这类只对桌面有意义的提示
bool hasHardwareKeyboard();

/// 系统字体候选路径（按优先级），用于渲染中文；找不到时界面会退回内置字体
std::vector<std::string> fontCandidates();

/// 品牌标题专用字体路径；未打包或不可读时返回空串，UI 会自动退回正文字体
std::string titleFontPath();

/// 打开系统的「选择文件」对话框（Windows 的资源管理器 / Android 的文档选择器）
/// ----------------------------------------------------------------------------
/// 拆成 begin / take 两步、且都不阻塞调用线程，是因为 Android 的选择器是**另一个 Activity**：
/// 系统把它盖上来时会停掉本应用，而 native_app_glue 的 onPause / onStop 会阻塞主线程直到
/// 宿主线程把命令处理完（android_app_set_activity_state 里的 pthread_cond_wait）——
/// 渲染线程要是卡在这里等结果，主线程就卡在 onPause 里（实测 5 秒即 ANR，踩过）。
/// 用法：begin 发起，之后每帧调一次 take，take 返回 false 表示「还没回来」。
/// @param extensions 允许的后缀，例如 {"quiz","txt"}（带不带点都行）；空 = 不过滤
/// @param allowMultiple 允不允许一次选多个
/// @return begin 返回 false 表示对话框没打开（调用方提示失败）
bool beginPickFiles(const std::string& title, const std::vector<std::string>& extensions,
                    bool allowMultiple);

/// 查询上面那次「选择文件」的结果。
/// @return false = 还没有结果（继续等）；true = 有结果了：
///         outCancelled = true 表示用户取消；否则 outPaths 里是选中的文件
///         （outPaths 为空且没取消 = 读取失败，日志里有原因）
bool takePickedFiles(std::vector<std::string>& outPaths, bool& outCancelled);

/// 打开系统的「保存文件」对话框；返回 true 表示用户选了路径
/// @param defaultPath 建议的完整路径（含文件名），用户可以改
/// @param extensions 允许的后缀（同上）；空 = 不过滤
bool beginPickSavePath(const std::string& title, const std::string& defaultPath,
                       const std::vector<std::string>& extensions);

/// 查询上面那次「保存文件」的结果。
/// @return false = 还没有结果；true = 有结果了：
///         outCancelled = true 表示用户取消；否则 outPath 是 native 该往里写内容的文件
///         （Android 上是应用内的临时文件，写完必须调 commitPickedSave 交出去）
bool takePickedSavePath(std::string& outPath, bool& outCancelled);

/// 把刚写好的导出文件交到用户在「保存」对话框里选中的位置。
/// ----------------------------------------------------------------------------
/// 桌面端：takePickedSavePath 返回的就是真实路径，内容早就落在那里了 → 这里没有第二步，
///         只把 localPath 原样抄进 outDisplayPath。
/// Android：SAF 是「先选位置、后写内容」——content:// URI 用 fopen 写不进去，所以那边给的是
///         应用内的临时文件；内容写好后必须再调这个函数，由 Java 复制进用户选中的文档。
/// @param localPath 导出内容已经写好的本地文件
/// @param outDisplayPath 成功时写入「给用户看的位置」（Windows：localPath；Android：Download/xxx.quiz）
/// @return false = 没能写进用户选的位置
bool commitPickedSave(const std::string& localPath, std::string& outDisplayPath);
}  // namespace quiz::platform

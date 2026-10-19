#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>   // GetOpenFileNameW / GetSaveFileNameW（导入导出的文件对话框）

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

#include "quiz/platform/Platform.h"

namespace quiz::platform {

namespace {

/// 宽字符 ⇄ UTF-8（定义在下面；这几个函数先用，所以先声明）
std::wstring toWide(const std::string& text);
std::string toUtf8(const std::wstring& text);

/// 别用 GetModuleFileNameA：它返回的是**当前 ANSI 代码页**的路径，
/// 程序装在中文目录下会变成乱码，之后所有基于它的路径（数据目录、assets）都会跟着废掉
std::string executableDirectory() {
    wchar_t buffer[MAX_PATH] = {0};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0) {
        return std::string(".");
    }
    const std::wstring path(buffer, length);
    const size_t slash = path.find_last_of(L"\\/");
    return toUtf8(slash == std::wstring::npos ? std::wstring(L".") : path.substr(0, slash));
}

/// 资源目录：优先 exe 旁的 assets/，其次当前工作目录的 assets/（直接从工程目录运行时）
std::string assetDirectory() {
    const std::string besideExe = joinPath(executableDirectory(), "assets");
    std::error_code error;
    if (std::filesystem::is_directory(std::filesystem::path(toWide(besideExe)), error)) {
        return besideExe;
    }
    return "assets";
}

/// 判断文件名是否以指定后缀结尾（这里放一份最小实现，避免平台层依赖 core）
bool hasSuffix(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<std::string> listFiles(const std::string& dir, const std::string& suffix) {
    std::vector<std::string> names;
    std::error_code error;
    // 目录也按宽字符打开：UTF-8 的窄路径在 Windows 上会被按 ANSI 代码页解释
    const std::filesystem::path wideDir(toWide(dir));
    if (!std::filesystem::exists(wideDir, error)) {
        return names;
    }
    for (const auto& entry : std::filesystem::directory_iterator(wideDir, error)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        // 文件名要按「宽字符 → UTF-8」转：Windows 下 path::string() 走的是**当前 ANSI 代码页**
        // （不是 UTF-8），中文文件名会变成乱码 —— 题库卡片显示成花码，删除题库也会因为
        // 文件名对不上而失败（同一个 bug 的两个症状，都实测过）
        const std::string name = toUtf8(entry.path().filename().wstring());
        if (suffix.empty() || hasSuffix(name, suffix)) {
            names.push_back(name);
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

// 待应用的窗口尺寸（0 = 没有请求）：见 Platform.h 里 setWindowClientSize 的说明
int g_pendingWindowWidth = 0;
int g_pendingWindowHeight = 0;

struct WindowLookup {
    DWORD processId = 0;
    HWND handle = nullptr;
};

BOOL CALLBACK findProcessWindow(HWND handle, LPARAM param) {
    auto* lookup = reinterpret_cast<WindowLookup*>(param);
    DWORD pid = 0;
    GetWindowThreadProcessId(handle, &pid);
    if (pid != lookup->processId || !IsWindowVisible(handle)) {
        return TRUE;
    }
    if (GetWindow(handle, GW_OWNER) != nullptr) {
        return TRUE;   // 附属窗口（对话框之类）不要
    }
    char className[128] = {0};
    GetClassNameA(handle, className, sizeof(className));
    if (std::string(className) == "ConsoleWindowClass") {
        return TRUE;   // 控制台窗口不要（无头模式只有它）
    }
    lookup->handle = handle;
    return FALSE;
}

/// UTF-8 → UTF-16（对话框要宽字符）
std::wstring toWide(const std::string& text) {
    if (text.empty()) {
        return std::wstring();
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                           nullptr, 0);
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

/// UTF-16 → UTF-8
std::string toUtf8(const std::wstring& text) {
    if (text.empty()) {
        return std::string();
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                           nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), length,
                        nullptr, nullptr);
    return out;
}

/// 对话框的父窗口：没有它对话框会飘在桌面、也不跟随我们的窗口
HWND currentWindow() {
    WindowLookup lookup;
    lookup.processId = GetCurrentProcessId();
    EnumWindows(findProcessWindow, reinterpret_cast<LPARAM>(&lookup));
    return lookup.handle;
}

std::string formatTime(const char* format) {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    char buffer[64] = {0};
    std::strftime(buffer, sizeof(buffer), format, &local);
    return std::string(buffer);
}

}  // namespace

const char* name() {
    return "Windows";
}

void writeLog(int level, const char* message) {
    const char* prefix = level == 2 ? "[错误] " : (level == 1 ? "[警告] " : "[信息] ");
    char line[768];
    std::snprintf(line, sizeof(line), "%s%s\n", prefix, message);
    std::fputs(line, stdout);
    std::fflush(stdout);
    OutputDebugStringA(line);
}

double monotonicSeconds() {
    static LARGE_INTEGER frequency = [] {
        LARGE_INTEGER value;
        QueryPerformanceFrequency(&value);
        return value;
    }();
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart);
}

std::string nowString() {
    return formatTime("%Y-%m-%d %H:%M:%S");
}

std::string nowStampCompact() {
    return formatTime("%Y%m%d-%H%M%S");
}

std::string dataDirectory() {
    return joinPath(executableDirectory(), "data");
}

std::vector<std::string> listAssetFiles(const std::string& dir, const std::string& suffix) {
    return listFiles(joinPath(assetDirectory(), dir), suffix);
}

std::vector<std::string> listDataFiles(const std::string& dir, const std::string& suffix) {
    return listFiles(joinPath(dataDirectory(), dir), suffix);
}

bool readAssetText(const std::string& name, std::string& outText) {
    return readTextFile(joinPath(assetDirectory(), name), outText);
}

bool readTextFile(const std::string& path, std::string& outText) {
    // 必须用宽字符版 fopen：CRT 的 fopen 会把路径按 ANSI 代码页解释，中文文件名直接打不开
    FILE* file = _wfopen(toWide(path).c_str(), L"rb");
    if (file == nullptr) {
        return false;
    }
    std::string content;
    char buffer[1024];
    size_t read = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        content.append(buffer, read);
    }
    std::fclose(file);
    outText = content;
    return true;
}

bool writeTextFile(const std::string& path, const std::string& text) {
    std::error_code error;
    const std::filesystem::path filePath(toWide(path));
    if (filePath.has_parent_path()) {
        std::filesystem::create_directories(filePath.parent_path(), error);
    }
    FILE* file = _wfopen(filePath.c_str(), L"wb");
    if (file == nullptr) {
        return false;
    }
    const size_t written = text.empty() ? 0 : std::fwrite(text.data(), 1, text.size(), file);
    std::fclose(file);
    return written == text.size();
}

bool fileExists(const std::string& path) {
    std::error_code error;
    return std::filesystem::exists(std::filesystem::path(toWide(path)), error);
}

bool removeFile(const std::string& path) {
    std::error_code error;
    const bool removed = std::filesystem::remove(std::filesystem::path(toWide(path)), error);
    return removed && !error;
}

std::string joinPath(const std::string& directory, const std::string& name) {
    if (directory.empty()) {
        return name;
    }
    if (name.empty()) {
        return directory;
    }
    const char last = directory[directory.size() - 1];
    if (last == '/' || last == '\\') {
        return directory + name;
    }
    return directory + "/" + name;
}

std::vector<AspectPreset> aspectPresets() {
    // 需求里写的是「前 y 后 x」，这里换算成 宽:高
    return {
        {"4:3", 1024, 768},    // 3:4（前 y 后 x）
        {"16:9", 1280, 720},   // 9:16
        {"9:16", 720, 1280},   // 16:9
        {"9:20", 720, 1600},   // 20:9
    };
}

bool setWindowClientSize(int width, int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }

    WindowLookup lookup;
    lookup.processId = GetCurrentProcessId();
    EnumWindows(findProcessWindow, reinterpret_cast<LPARAM>(&lookup));
    if (lookup.handle == nullptr) {
        return false;   // 无头模式没有窗口，直接当不支持
    }

    // 只登记请求：真正的 SetWindowPos 交给 applyPendingWindowResize()，
    // 它在「一帧开始之前」执行，避免帧中途触发 WM_SIZE 重建帧缓冲。
    g_pendingWindowWidth = width;
    g_pendingWindowHeight = height;
    return true;
}

void applyPendingWindowResize() {
    if (g_pendingWindowWidth <= 0 || g_pendingWindowHeight <= 0) {
        return;
    }
    int width = g_pendingWindowWidth;
    int height = g_pendingWindowHeight;
    g_pendingWindowWidth = 0;
    g_pendingWindowHeight = 0;

    WindowLookup lookup;
    lookup.processId = GetCurrentProcessId();
    EnumWindows(findProcessWindow, reinterpret_cast<LPARAM>(&lookup));
    if (lookup.handle == nullptr) {
        return;
    }

    const DWORD style = static_cast<DWORD>(GetWindowLongPtrA(lookup.handle, GWL_STYLE));
    const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrA(lookup.handle, GWL_EXSTYLE));

    RECT rect{0, 0, width, height};
    AdjustWindowRectEx(&rect, style, FALSE, exStyle);
    int totalWidth = rect.right - rect.left;
    int totalHeight = rect.bottom - rect.top;

    // 屏幕放不下就等比缩小，免得窗口跑到屏幕外面、标题栏点不到
    RECT work{};
    if (SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0)) {
        const int availableWidth = (work.right - work.left) - 16;
        const int availableHeight = (work.bottom - work.top) - 16;
        if (totalWidth > availableWidth || totalHeight > availableHeight) {
            const double ratio = std::min(static_cast<double>(availableWidth) / totalWidth,
                                          static_cast<double>(availableHeight) / totalHeight);
            width = std::max(160, static_cast<int>(width * ratio));
            height = std::max(160, static_cast<int>(height * ratio));
            rect = RECT{0, 0, width, height};
            AdjustWindowRectEx(&rect, style, FALSE, exStyle);
            totalWidth = rect.right - rect.left;
            totalHeight = rect.bottom - rect.top;
        }
    }

    SetWindowPos(lookup.handle, nullptr, 0, 0, totalWidth, totalHeight,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

bool setClipboardText(const std::string& text) {
    // Windows 剪贴板用宽字符，先把 UTF-8 转成 UTF-16
    const int wideLength = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                               nullptr, 0);
    if (wideLength <= 0) {
        return false;
    }
    std::wstring wide(static_cast<size_t>(wideLength), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), wide.data(), wideLength);

    if (OpenClipboard(nullptr) == FALSE) {
        return false;
    }
    bool ok = false;
    if (EmptyClipboard() != FALSE) {
        const size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (memory != nullptr) {
            if (void* target = GlobalLock(memory)) {
                // 用 c_str() 指针拷贝（连同结尾的 L(0)）：用迭代器写 end() + 1 属于越界，
                // 调试版的 checked iterator 会直接断言 —— 就是把程序卡住的那个坑。
                std::copy(wide.c_str(), wide.c_str() + wide.size() + 1, static_cast<wchar_t*>(target));
                GlobalUnlock(memory);
                if (SetClipboardData(CF_UNICODETEXT, memory) != nullptr) {
                    ok = true;   // 内存所有权已交给系统，这里不能再释放
                } else {
                    GlobalFree(memory);
                }
            } else {
                GlobalFree(memory);
            }
        }
    }
    CloseClipboard();
    return ok;
}

bool hasHardwareKeyboard() {
    return true;
}

// ---------------------------------------------------------------------------
// 系统文件对话框（Windows：GetOpenFileName / GetSaveFileName）
// ---------------------------------------------------------------------------
// 这两个对话框是模态的：begin 调下去就在等用户，返回时结果已经有了，take 下一帧取走。
// 和 Android 一样拆成 begin / take，是为了两端共用同一套 ui 代码。
// ---------------------------------------------------------------------------
namespace {

struct PendingDialogState {
    bool waiting = false;                 // true = 有一次调用还没被 take 走
    bool cancelled = true;                // 用户取消（默认按取消算）
    std::vector<std::string> files;       // beginPickFiles 的结果
    std::string savePath;                 // beginPickSavePath 的结果
};

PendingDialogState g_pendingDialog;

}  // namespace

bool beginPickFiles(const std::string& title, const std::vector<std::string>& extensions,
                    bool allowMultiple) {
    g_pendingDialog = PendingDialogState{};
    g_pendingDialog.waiting = true;
    // 过滤器：aapt 风格的 "名字\0通配\0名字\0通配\0\0"（这里用 Windows 的写法）
    std::string filter = "题库文件 (";
    std::string patterns;
    for (const std::string& ext : extensions) {
        const std::string bare = ext.size() > 0 && ext[0] == '.' ? ext.substr(1) : ext;
        if (patterns.empty()) {
            filter += "*." + bare;
        } else {
            filter += ", *." + bare;
        }
        patterns += "*." + bare + ";";
    }
    filter += ")";
    filter.push_back('\0');
    if (patterns.empty()) {
        filter += "*.*";
    } else {
        filter += patterns;
    }
    filter.push_back('\0');
    filter += "所有文件 (*.*)";
    filter.push_back('\0');
    filter += "*.*";
    filter.push_back('\0');
    filter.push_back('\0');

    const DWORD kBufferChars = 64 * 1024;   // 多选时是一串文件名的缓冲
    std::vector<wchar_t> buffer(kBufferChars, L'\0');
    // 注意：这两个 wstring 必须先落成局部变量 —— 直接写 toWide(...).c_str() 会悬空
    const std::wstring filterWide = toWide(filter);
    const std::wstring titleWide = toWide(title);
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = currentWindow();
    dialog.lpstrFilter = filterWide.c_str();
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = kBufferChars;
    dialog.lpstrTitle = titleWide.c_str();
    dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (allowMultiple) {
        dialog.Flags |= OFN_ALLOWMULTISELECT;
    }
    if (GetOpenFileNameW(&dialog) == FALSE) {
        return true;   // 用户取消：cancelled 保持默认的 true
    }
    // 单选：直接是一整个路径；多选：先是目录，然后是若干文件名，最后双空
    std::wstring first(buffer.data());
    const wchar_t* p = buffer.data() + first.size() + 1;
    if (allowMultiple && *p != L'\0') {
        while (*p != L'\0') {
            const std::wstring name(p);
            g_pendingDialog.files.push_back(toUtf8(first + L"\\" + name));
            p += name.size() + 1;
        }
    } else {
        g_pendingDialog.files.push_back(toUtf8(first));
    }
    g_pendingDialog.cancelled = g_pendingDialog.files.empty();
    return true;
}

bool takePickedFiles(std::vector<std::string>& outPaths, bool& outCancelled) {
    outPaths.clear();
    outCancelled = true;
    if (!g_pendingDialog.waiting) {
        return true;   // 没有进行中的调用：按取消处理
    }
    outPaths = g_pendingDialog.files;
    outCancelled = g_pendingDialog.cancelled;
    g_pendingDialog = PendingDialogState{};
    return true;
}

bool beginPickSavePath(const std::string& title, const std::string& defaultPath,
                       const std::vector<std::string>& extensions) {
    g_pendingDialog = PendingDialogState{};
    g_pendingDialog.waiting = true;
    std::string filter = "题库文件 (";
    std::string patterns;
    for (const std::string& ext : extensions) {
        const std::string bare = ext.size() > 0 && ext[0] == '.' ? ext.substr(1) : ext;
        if (patterns.empty()) {
            filter += "*." + bare;
        } else {
            filter += ", *." + bare;
        }
        patterns += "*." + bare + ";";
    }
    filter += ")";
    filter.push_back('\0');
    if (patterns.empty()) {
        filter += "*.*";
    } else {
        filter += patterns;
    }
    filter.push_back('\0');
    filter += "所有文件 (*.*)";
    filter.push_back('\0');
    filter += "*.*";
    filter.push_back('\0');
    filter.push_back('\0');

    std::vector<wchar_t> buffer(4096, L'\0');
    const std::wstring defaultWide = toWide(defaultPath);
    const size_t copyCount = std::min(defaultWide.size(), buffer.size() - 1);
    std::memcpy(buffer.data(), defaultWide.data(), copyCount * sizeof(wchar_t));

    const std::wstring titleWide = toWide(title);
    const std::wstring filterWide = toWide(filter);
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = currentWindow();
    dialog.lpstrFilter = filterWide.c_str();
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrTitle = titleWide.c_str();
    dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (GetSaveFileNameW(&dialog) == FALSE) {
        return true;   // 用户取消：cancelled 保持默认的 true
    }
    g_pendingDialog.savePath = toUtf8(std::wstring(buffer.data()));
    g_pendingDialog.cancelled = g_pendingDialog.savePath.empty();
    return true;
}

bool takePickedSavePath(std::string& outPath, bool& outCancelled) {
    outPath.clear();
    outCancelled = true;
    if (!g_pendingDialog.waiting) {
        return true;   // 没有进行中的调用：按取消处理
    }
    outPath = g_pendingDialog.savePath;
    outCancelled = g_pendingDialog.cancelled;
    g_pendingDialog = PendingDialogState{};
    return true;
}

bool commitPickedSave(const std::string& localPath, std::string& outDisplayPath) {
    // Windows 的「保存」对话框返回的就是真实路径，内容已经写在那儿了，这里没有第二步。
    // 留这个空实现是为了让 ui 层两端共用同一段代码。
    outDisplayPath = localPath;
    return true;
}
std::vector<std::string> fontCandidates() {
    std::vector<std::string> out;
    // 打包进来的字体优先：和 Android 端用同一份，两端观感一致
    out.push_back(joinPath(joinPath(assetDirectory(), "fonts"), "NotoSansSC-Regular.ttf"));
    // 再依次试常见系统中文字体（微软雅黑 → 黑体 → 等线 → 宋体）
    out.push_back("C:/Windows/Fonts/msyh.ttc");
    out.push_back("C:/Windows/Fonts/msyhbd.ttc");
    out.push_back("C:/Windows/Fonts/simhei.ttf");
    out.push_back("C:/Windows/Fonts/Deng.ttf");
    out.push_back("C:/Windows/Fonts/simsun.ttc");
    return out;
}

}  // namespace quiz::platform

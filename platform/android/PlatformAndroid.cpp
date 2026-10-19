// ============================================================================
// Android 平台实现：日志、时钟、文件、资源、字体路径
// ============================================================================
#include <android/log.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <jni.h>   // 调 Java 的输入法桥
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"

#include "AndroidPlatform.h"
#include "quiz/render/ImGuiRenderer.h"
#include "quiz/platform/Platform.h"

namespace {

constexpr const char* kLogTag = "AxtQuiz";

AAssetManager* g_assetManager = nullptr;
ANativeActivity* g_activity = nullptr;
std::string g_dataDirectory;
std::atomic<bool> g_backRequested{false};

bool hasSuffix(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/// 递归创建目录（等价于 mkdir -p）
void makeDirectories(const std::string& path) {
    std::string current;
    for (size_t i = 0; i < path.size(); ++i) {
        current.push_back(path[i]);
        if (path[i] == '/' && current.size() > 1) {
            mkdir(current.c_str(), 0755);
        }
    }
    mkdir(path.c_str(), 0755);
}

std::string formatTime(const char* format) {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char buffer[64] = {0};
    std::strftime(buffer, sizeof(buffer), format, &local);
    return std::string(buffer);
}

/// 取路径里的文件名部分（'/' 与 '\' 都认）
std::string fileNameOf(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

/// 日志（本文件里到处要用，包一层省得每次写全名）
void logLine(int level, const std::string& message) {
    quiz::platform::writeLog(level, message.c_str());
}

/// 一次 JNI 调用的上下文：把当前线程挂到 JVM 上、拿到 Activity 的类，退出时自动收拾干净。
/// native 的渲染线程是纯 C 线程，没有挂在 JVM 上，所以调用前必须先 Attach。
class ActivityCall {
public:
    ActivityCall() {
        if (g_activity == nullptr || g_activity->clazz == nullptr || g_activity->vm == nullptr) {
            return;
        }
        if (g_activity->vm->GetEnv(reinterpret_cast<void**>(&env_), JNI_VERSION_1_6) != JNI_OK) {
            if (g_activity->vm->AttachCurrentThread(&env_, nullptr) != JNI_OK) {
                env_ = nullptr;
                return;
            }
            attached_ = true;
        }
        cls_ = env_->GetObjectClass(g_activity->clazz);
        if (cls_ == nullptr) {
            return;
        }
        ready_ = true;
    }

    ~ActivityCall() {
        if (env_ == nullptr) {
            return;
        }
        if (cls_ != nullptr) {
            env_->DeleteLocalRef(cls_);
        }
        if (env_->ExceptionCheck()) {
            env_->ExceptionClear();
        }
        if (attached_) {
            g_activity->vm->DetachCurrentThread();
        }
    }

    ActivityCall(const ActivityCall&) = delete;
    ActivityCall& operator=(const ActivityCall&) = delete;

    bool ready() const { return ready_; }
    JNIEnv* env() const { return env_; }
    jclass cls() const { return cls_; }

    /// Java 侧抛的异常：先把堆栈打进 logcat 再清掉。
    /// 以前这里 ExceptionClear 一吞，出问题在日志里什么都看不见（踩过）。
    void reportException(const char* what) {
        if (env_ == nullptr || !env_->ExceptionCheck()) {
            return;
        }
        env_->ExceptionDescribe();
        env_->ExceptionClear();
        logLine(2, std::string("调用 Java 失败：") + what);
    }

private:
    JNIEnv* env_ = nullptr;
    jclass cls_ = nullptr;
    bool attached_ = false;
    bool ready_ = false;
};

/// 取出 jstring 的 UTF-8 内容（用完立即释放）
std::string takeUtf8(JNIEnv* env, jstring text) {
    std::string out;
    if (text == nullptr) {
        return out;
    }
    const char* utf8 = env->GetStringUTFChars(text, nullptr);
    if (utf8 != nullptr) {
        out = utf8;
        env->ReleaseStringUTFChars(text, utf8);
    }
    return out;
}

// Java 侧 pollPickerState 的返回值（见 MainActivity.java）
constexpr int kPickStateNone = 0;      // 没有对话框
constexpr int kPickStateWaiting = 1;   // 还在等用户
constexpr int kPickStateReady = 2;     // 结果到了

/// 让 Java 弹出「打开文件」对话框（立即返回）
bool callJavaStartPickDocuments(bool allowMultiple) {
    ActivityCall call;
    if (!call.ready()) {
        logLine(2, "打不开系统文档选择器：拿不到 Activity");
        return false;
    }
    JNIEnv* env = call.env();
    jmethodID method = env->GetMethodID(call.cls(), "startPickDocuments", "(Z)Z");
    if (method == nullptr) {
        call.reportException("GetMethodID startPickDocuments");
        return false;
    }
    const jboolean ok = env->CallBooleanMethod(g_activity->clazz, method,
                                               allowMultiple ? JNI_TRUE : JNI_FALSE);
    if (env->ExceptionCheck()) {
        call.reportException("startPickDocuments");
        return false;
    }
    return ok == JNI_TRUE;
}

/// 让 Java 弹出「保存文件」对话框（立即返回）
bool callJavaStartPickSaveTarget(const std::string& suggestedName) {
    ActivityCall call;
    if (!call.ready()) {
        logLine(2, "打不开系统保存对话框：拿不到 Activity");
        return false;
    }
    JNIEnv* env = call.env();
    jmethodID method = env->GetMethodID(
        call.cls(), "startPickSaveTarget", "(Ljava/lang/String;Ljava/lang/String;)Z");
    if (method == nullptr) {
        call.reportException("GetMethodID startPickSaveTarget");
        return false;
    }
    jstring name = env->NewStringUTF(suggestedName.c_str());
    // MIME 用 octet-stream：实测 text/plain 会让系统「保存」对话框给名字补上 .txt
    // （xxx.quiz → xxx.quiz.txt）；octet-stream 则原样保留我们建议的名字
    jstring mime = env->NewStringUTF("application/octet-stream");
    const jboolean ok = env->CallBooleanMethod(g_activity->clazz, method, name, mime);
    env->DeleteLocalRef(name);
    env->DeleteLocalRef(mime);
    if (env->ExceptionCheck()) {
        call.reportException("startPickSaveTarget");
        return false;
    }
    return ok == JNI_TRUE;
}

/// 问 Java：对话框有结果了吗（0/1/2，见 MainActivity.java）
int callJavaPollPickerState() {
    ActivityCall call;
    if (!call.ready()) {
        return kPickStateNone;
    }
    JNIEnv* env = call.env();
    jmethodID method = env->GetMethodID(call.cls(), "pollPickerState", "()I");
    if (method == nullptr) {
        call.reportException("GetMethodID pollPickerState");
        return kPickStateNone;
    }
    const jint state = env->CallIntMethod(g_activity->clazz, method);
    if (env->ExceptionCheck()) {
        call.reportException("pollPickerState");
        return kPickStateNone;
    }
    return static_cast<int>(state);
}

/// 取走「打开文件」的结果：Java 会把选中的文件复制到 cacheDir 下
/// @return 拿到结果了没有（false = 拿不到结果，调用方按失败处理）
bool callJavaTakePickedDocuments(const std::string& cacheDir, std::vector<std::string>& outPaths,
                                 bool& outCancelled) {
    ActivityCall call;
    if (!call.ready()) {
        logLine(2, "取「打开文件」结果失败：拿不到 Activity");
        return false;
    }
    JNIEnv* env = call.env();
    jmethodID method = env->GetMethodID(call.cls(), "takePickedDocumentPaths",
                                        "(Ljava/lang/String;)[Ljava/lang/String;");
    if (method == nullptr) {
        call.reportException("GetMethodID takePickedDocumentPaths");
        return false;
    }
    jstring cache = env->NewStringUTF(cacheDir.c_str());
    auto* result = static_cast<jobjectArray>(env->CallObjectMethod(g_activity->clazz, method, cache));
    env->DeleteLocalRef(cache);
    if (env->ExceptionCheck()) {
        call.reportException("takePickedDocumentPaths");
        return false;
    }
    if (result == nullptr) {
        outCancelled = true;   // 用户取消
        return true;
    }
    const jsize count = env->GetArrayLength(result);
    for (jsize i = 0; i < count; ++i) {
        auto* item = static_cast<jstring>(env->GetObjectArrayElement(result, i));
        const std::string path = takeUtf8(env, item);
        if (item != nullptr) {
            env->DeleteLocalRef(item);
        }
        if (!path.empty()) {
            outPaths.push_back(path);
        }
    }
    env->DeleteLocalRef(result);
    return true;
}

/// 取走「保存文件」的结果：Java 给出一个应用内的临时文件路径（空串 = 拿不到位置）
bool callJavaTakePickedSaveTarget(const std::string& scratchDir, std::string& outPath,
                                  bool& outCancelled) {
    ActivityCall call;
    if (!call.ready()) {
        logLine(2, "取「保存文件」结果失败：拿不到 Activity");
        return false;
    }
    JNIEnv* env = call.env();
    jmethodID method = env->GetMethodID(call.cls(), "takePickedSaveTarget",
                                        "(Ljava/lang/String;)Ljava/lang/String;");
    if (method == nullptr) {
        call.reportException("GetMethodID takePickedSaveTarget");
        return false;
    }
    jstring dir = env->NewStringUTF(scratchDir.c_str());
    auto* result = static_cast<jstring>(env->CallObjectMethod(g_activity->clazz, method, dir));
    env->DeleteLocalRef(dir);
    if (env->ExceptionCheck()) {
        call.reportException("takePickedSaveTarget");
        return false;
    }
    if (result == nullptr) {
        outCancelled = true;   // 用户取消
        return true;
    }
    outPath = takeUtf8(env, result);
    env->DeleteLocalRef(result);
    return true;
}

/// 把本地文件复制进用户选中的文档；成功返回「给用户看的位置」，失败返回空串
std::string callJavaCommitSave(const std::string& localPath) {
    ActivityCall call;
    if (!call.ready()) {
        logLine(2, "提交导出结果失败：拿不到 Activity");
        return std::string();
    }
    JNIEnv* env = call.env();
    jmethodID method = env->GetMethodID(call.cls(), "commitSaveTarget",
                                        "(Ljava/lang/String;)Ljava/lang/String;");
    if (method == nullptr) {
        call.reportException("GetMethodID commitSaveTarget");
        return std::string();
    }
    jstring path = env->NewStringUTF(localPath.c_str());
    auto* result = static_cast<jstring>(env->CallObjectMethod(g_activity->clazz, method, path));
    env->DeleteLocalRef(path);
    if (env->ExceptionCheck()) {
        call.reportException("commitSaveTarget");
        return std::string();
    }
    if (result == nullptr) {
        return std::string();
    }
    const std::string display = takeUtf8(env, result);
    env->DeleteLocalRef(result);
    return display;
}
}  // namespace

namespace quiz::platform::android {

void setAndroidContext(AAssetManager* assetManager, ANativeActivity* activity) {
    g_assetManager = assetManager;
    g_activity = activity;
    if (activity != nullptr && activity->internalDataPath != nullptr) {
        g_dataDirectory = activity->internalDataPath;
        makeDirectories(g_dataDirectory);
    }
}

/// 调 Activity 上的无参方法（showKeyboard / hideKeyboard）
void callActivityVoid(const char* method) {
    if (g_activity == nullptr || g_activity->clazz == nullptr || g_activity->vm == nullptr) {
        return;
    }
    JNIEnv* env = nullptr;
    bool attached = false;
    if (g_activity->vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        if (g_activity->vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            return;
        }
        attached = true;
    }
    jclass cls = env->GetObjectClass(g_activity->clazz);
    if (cls != nullptr) {
        jmethodID id = env->GetMethodID(cls, method, "()V");
        if (id != nullptr) {
            env->CallVoidMethod(g_activity->clazz, id);
        }
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        env->DeleteLocalRef(cls);
    }
    if (attached) {
        g_activity->vm->DetachCurrentThread();
    }
}

void syncSoftKeyboard(bool wantTextInput) {
    static bool shown = false;
    static int retries = 0;
    static double requestedAt = 0.0;
    if (g_activity == nullptr) {
        return;
    }
    if (wantTextInput != shown) {
        shown = wantTextInput;
        retries = 0;
        requestedAt = monotonicSeconds();
        if (wantTextInput) {
            writeLog(0, "输入框获得焦点：请求弹出软键盘");
            callActivityVoid("showKeyboard");
        } else {
            writeLog(0, "输入框失去焦点：收起软键盘");
            callActivityVoid("hideKeyboard");
        }
        return;
    }
    // 有些机型第一次请求会被忽略（窗口/输入法还没就绪），隔 0.25 秒补几次
    if (wantTextInput && retries < 1 && monotonicSeconds() - requestedAt > 0.25) {
        ++retries;
        const std::string retryMessage = "再补一次弹出软键盘的请求（第 " + std::to_string(retries) + " 次）";
        writeLog(0, retryMessage.c_str());
        callActivityVoid("showKeyboard");
    }
}

void requestBack() {
    g_backRequested.store(true, std::memory_order_release);
}

bool takeBackRequest() {
    return g_backRequested.exchange(false, std::memory_order_acq_rel);
}

void requestExit() {
    if (g_activity != nullptr) {
        ANativeActivity_finish(g_activity);
    }
}

}  // namespace quiz::platform::android

namespace quiz::platform {

const char* name() {
    return "Android";
}

void writeLog(int level, const char* message) {
    const int priority = level == 2 ? ANDROID_LOG_ERROR : (level == 1 ? ANDROID_LOG_WARN : ANDROID_LOG_INFO);
    __android_log_print(priority, kLogTag, "%s", message);
}

double monotonicSeconds() {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<double>(now.tv_sec) + static_cast<double>(now.tv_nsec) / 1e9;
}

std::string nowString() {
    return formatTime("%Y-%m-%d %H:%M:%S");
}

std::string nowStampCompact() {
    return formatTime("%Y%m%d-%H%M%S");
}

std::string dataDirectory() {
    return g_dataDirectory.empty() ? std::string(".") : g_dataDirectory;
}

std::vector<std::string> listAssetFiles(const std::string& dir, const std::string& suffix) {
    std::vector<std::string> names;
    if (g_assetManager == nullptr) {
        return names;
    }
    AAssetDir* assetDir = AAssetManager_openDir(g_assetManager, dir.c_str());
    if (assetDir == nullptr) {
        return names;
    }
    const char* fileName = nullptr;
    while ((fileName = AAssetDir_getNextFileName(assetDir)) != nullptr) {
        if (suffix.empty() || hasSuffix(fileName, suffix)) {
            names.emplace_back(fileName);
        }
    }
    AAssetDir_close(assetDir);
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<std::string> listDataFiles(const std::string& dir, const std::string& suffix) {
    std::vector<std::string> names;
    const std::string full = joinPath(dataDirectory(), dir);
    // 目录不存在时直接返回空（第一次运行时就是这种情况）
    if (access(full.c_str(), F_OK) != 0) {
        return names;
    }
    // 用 popen 列目录不太优雅，这里直接尝试读取目录项（POSIX 方式）
    DIR* handle = opendir(full.c_str());
    if (handle == nullptr) {
        return names;
    }
    while (dirent* entry = readdir(handle)) {
        const std::string entryName = entry->d_name;
        if (entryName == "." || entryName == "..") {
            continue;
        }
        if (suffix.empty() || hasSuffix(entryName, suffix)) {
            names.push_back(entryName);
        }
    }
    closedir(handle);
    std::sort(names.begin(), names.end());
    return names;
}

bool readAssetText(const std::string& name, std::string& outText) {
    if (g_assetManager == nullptr) {
        return false;
    }
    AAsset* asset = AAssetManager_open(g_assetManager, name.c_str(), AASSET_MODE_BUFFER);
    if (asset == nullptr) {
        return false;
    }
    const void* data = AAsset_getBuffer(asset);
    const off_t length = AAsset_getLength(asset);
    if (data != nullptr && length > 0) {
        outText.assign(static_cast<const char*>(data), static_cast<size_t>(length));
    } else {
        outText.clear();
    }
    AAsset_close(asset);
    return true;
}

bool readTextFile(const std::string& path, std::string& outText) {
    FILE* file = std::fopen(path.c_str(), "rb");
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
    const size_t slash = path.find_last_of('/');
    if (slash != std::string::npos) {
        makeDirectories(path.substr(0, slash));
    }
    FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return false;
    }
    const size_t written = text.empty() ? 0 : std::fwrite(text.data(), 1, text.size(), file);
    std::fclose(file);
    return written == text.size();
}

bool fileExists(const std::string& path) {
    return access(path.c_str(), F_OK) == 0;
}

bool removeFile(const std::string& path) {
    return std::remove(path.c_str()) == 0;
}

std::string joinPath(const std::string& directory, const std::string& name) {
    if (directory.empty()) {
        return name;
    }
    if (name.empty()) {
        return directory;
    }
    if (directory.back() == '/') {
        return directory + name;
    }
    return directory + "/" + name;
}

std::vector<AspectPreset> aspectPresets() {
    // 需求里写的是「前 y 后 x」，这里换算成 宽:高
    return {
        {"6:4", 1200, 800},    // 4:6（前 y 后 x）
        {"9:16", 720, 1280},   // 16:9
        {"9:20", 720, 1600},   // 20:9
    };
}

bool setWindowClientSize(int /*width*/, int /*height*/) {
    // Android 的窗口尺寸由系统决定，这里只做界面留边，不去动窗口
    return false;
}

void applyPendingWindowResize() {
    // Android 不调整窗口，留空即可（接口两端保持一致）
}

bool setClipboardText(const std::string& text) {
    // 走 Java 的 ClipboardManager（设置页「提示词」的复制按钮用）。
    ActivityCall call;
    if (!call.ready()) {
        return false;
    }
    jmethodID method = call.env()->GetMethodID(call.cls(), "setClipboard", "(Ljava/lang/String;)Z");
    if (method == nullptr) {
        call.reportException("GetMethodID setClipboard");
        return false;
    }
    jstring value = call.env()->NewStringUTF(text.c_str());
    if (value == nullptr) {
        call.reportException("NewStringUTF setClipboard");
        return false;
    }
    const jboolean ok = call.env()->CallBooleanMethod(g_activity->clazz, method, value);
    call.env()->DeleteLocalRef(value);
    if (call.env()->ExceptionCheck()) {
        call.reportException("setClipboard");
        return false;
    }
    return ok == JNI_TRUE;
}

// 文件选择器：Android 走 SAF（ACTION_OPEN_DOCUMENT / ACTION_CREATE_DOCUMENT）
// ---------------------------------------------------------------------------
// 和桌面端有两处不一样，都在这里消化掉，ui 层不用关心：
//   1) SAF 给的是 content:// URI，fopen 打不开 → 由 Java 负责搬运：导入方向把选中的
//      文件拷进 data/import_picked/，导出方向把 native 写好的临时文件拷进用户选中的文档；
//   2) 对话框是另一个 Activity，结果要等 onActivityResult → 这里拆成 begin / take 两步，
//      中间渲染线程照常跑（不然系统停窗口时 native_app_glue 的 onPause 会把主线程卡住）。
// ---------------------------------------------------------------------------
bool beginPickFiles(const std::string& title, const std::vector<std::string>& extensions,
                    bool allowMultiple) {
    (void)extensions;   // SAF 按 MIME 过滤；题库是纯文本，Java 侧放开成 */*
    const std::string message = "导入：请用户在系统文档选择器里挑文件（" + title + "）";
    writeLog(0, message.c_str());
    return callJavaStartPickDocuments(allowMultiple);
}

bool takePickedFiles(std::vector<std::string>& outPaths, bool& outCancelled) {
    outPaths.clear();
    outCancelled = false;
    const int state = callJavaPollPickerState();
    if (state == kPickStateWaiting) {
        return false;   // 用户还在挑
    }
    if (state == kPickStateNone) {
        outCancelled = true;   // 对话框没开起来：按取消处理（日志里有原因）
        return true;
    }
    // 复制到应用内部目录，native 才读得到 —— importBankFiles 因此不用改
    const std::string cacheDir = joinPath(dataDirectory(), "import_picked");
    if (!callJavaTakePickedDocuments(cacheDir, outPaths, outCancelled)) {
        outCancelled = true;
        return true;
    }
    if (outCancelled) {
        writeLog(0, "导入：用户取消了选择");
        return true;
    }
    if (outPaths.empty()) {
        writeLog(2, "导入：选中的文件一个都没能复制出来（Java 侧的异常见上一条日志）");
        return true;
    }
    const std::string message = "导入：已取回 " + std::to_string(outPaths.size()) + " 个文件";
    writeLog(0, message.c_str());
    return true;
}

bool beginPickSavePath(const std::string& title, const std::string& defaultPath,
                       const std::vector<std::string>& extensions) {
    (void)extensions;   // 同上：Android 按 MIME 过滤
    const std::string message = "导出：请用户在系统保存对话框里选位置（" + title + "）";
    writeLog(0, message.c_str());
    return callJavaStartPickSaveTarget(fileNameOf(defaultPath));
}

bool takePickedSavePath(std::string& outPath, bool& outCancelled) {
    outPath.clear();
    outCancelled = false;
    const int state = callJavaPollPickerState();
    if (state == kPickStateWaiting) {
        return false;
    }
    if (state == kPickStateNone) {
        outCancelled = true;
        return true;
    }
    // 交给 native 写的只能是应用内部路径：SAF 的目标要等内容写好再提交（见 commitPickedSave）
    const std::string scratchDir = joinPath(dataDirectory(), "export_scratch");
    if (!callJavaTakePickedSaveTarget(scratchDir, outPath, outCancelled)) {
        outCancelled = true;
        return true;
    }
    if (outCancelled) {
        writeLog(0, "导出：用户取消了保存");
        return true;
    }
    if (outPath.empty()) {
        writeLog(2, "导出：拿不到可写的临时文件（Java 侧的异常见上一条日志）");
        return true;
    }
    const std::string message = "导出：位置已定，内容先写进临时文件 " + outPath;
    writeLog(0, message.c_str());
    return true;
}

bool commitPickedSave(const std::string& localPath, std::string& outDisplayPath) {
    outDisplayPath.clear();
    const std::string display = callJavaCommitSave(localPath);
    if (display.empty()) {
        writeLog(2, "导出：把内容写进用户选中的文档失败（Java 侧的异常见上一条日志）");
        return false;
    }
    outDisplayPath = display;
    const std::string message = "导出：已写入用户选中的位置 " + display;
    writeLog(0, message.c_str());
    return true;
}
bool hasHardwareKeyboard() {
    return false;   // 触屏端没有键盘
}

std::vector<std::string> fontCandidates() {
    std::vector<std::string> out;
    // 1) 打包进 APK 的字体优先（assets/fonts/*.ttf|*.ttc）。
    //    ImGui 要一个真实文件路径，所以先解到 data/fonts/ 下；解过一次就不再解。
    for (const std::string& suffix : {std::string(".ttf"), std::string(".ttc")}) {
        for (const std::string& name : listAssetFiles("fonts", suffix)) {
            const std::string target = joinPath(joinPath(dataDirectory(), "fonts"), name);
            if (!fileExists(target)) {
                std::string bytes;
                if (!readAssetText("fonts/" + name, bytes) || bytes.empty() ||
                    !writeTextFile(target, bytes)) {
                    writeLog(1, "内置字体解压失败（不影响使用，只是中文会显示成方块）");
                    continue;
                }
                const std::string message = "已解出内置字体：" + target;
                writeLog(0, message.c_str());
            }
            out.push_back(target);
        }
    }
    // 2) 再依次试系统字体：现代 Android 只带 CFF 轮廓的 Noto CJK，stb 读不了，
    //    所以这一组通常都会失败（界面会退回内置英文字体）
    const std::vector<std::string> systemFonts = {
        "/system/fonts/NotoSansCJK-Regular.ttc",
        "/system/fonts/NotoSansSC-Regular.otf",
        "/system/fonts/NotoSansCJKsc-Regular.otf",
        "/system/fonts/DroidSansFallbackFull.ttf",
        "/system/fonts/DroidSansFallback.ttf",
        "/system/fonts/NotoSansCJK-VF.otf.ttc",
    };
    for (const std::string& path : systemFonts) {
        out.push_back(path);
    }
    return out;
}

}  // namespace quiz::platform
// ---------------------------------------------------------------------------
// Java ↔ native：输入法送来的文字与按键（见 app/src/main/java/.../MainActivity.java）
// ---------------------------------------------------------------------------
extern "C" JNIEXPORT void JNICALL Java_com_axt_quiz_MainActivity_nativeOnText(JNIEnv* env, jclass,
                                                                             jstring text) {
    if (text == nullptr) {
        return;
    }
    const char* utf8 = env->GetStringUTFChars(text, nullptr);
    if (utf8 != nullptr) {
        quiz::render::addTextUtf8(utf8);
        env->ReleaseStringUTFChars(text, utf8);
    }
}

extern "C" JNIEXPORT void JNICALL Java_com_axt_quiz_MainActivity_nativeRequestBack(JNIEnv*,
                                                                                   jclass) {
    quiz::platform::android::requestBack();
}

extern "C" JNIEXPORT void JNICALL Java_com_axt_quiz_MainActivity_nativeOnKey(JNIEnv*, jclass,
                                                                            jint keyCode) {
    if (keyCode == 8) {
        quiz::render::addKeyEvent(ImGuiKey_Backspace, true);
        quiz::render::addKeyEvent(ImGuiKey_Backspace, false);
    } else if (keyCode == 13) {
        quiz::render::addKeyEvent(ImGuiKey_Enter, true);
        quiz::render::addKeyEvent(ImGuiKey_Enter, false);
    }
}

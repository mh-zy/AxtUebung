// ============================================================================
// Android 入口：NativeActivity + android_native_app_glue
// ----------------------------------------------------------------------------
// 特点：
//   * 纯 C++，不需要写任何 Java/Kotlin（清单里声明 android.app.NativeActivity 即可）
//   * 画面用软件渲染：把 ImGui 画到的像素缓冲直接写进窗口缓冲区
//   * 只在有交互时重绘，静止时阻塞等待事件 → 省电
// ============================================================================
#include <android_native_app_glue.h>
#include <android/native_activity.h>
#include <android/window.h>   // AWINDOW_FLAG_KEEP_SCREEN_ON / ANativeActivity_setWindowFlags
#include <unistd.h>

#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#include "imgui.h"

#include "AndroidPlatform.h"
#include "quiz/app/AppContext.h"
#include "quiz/platform/Platform.h"
#include "quiz/render/ImGuiRenderer.h"
#include "quiz/ui/UiApp.h"
#include "quiz/ui/UiScroll.h"
#include "quiz/util/FrameStats.h"
#include "quiz/util/Log.h"

namespace {

/// 内部渲染分辨率的长边上限：越小越省 CPU，越大文字越清晰。
/// 刷题应用以文字为主，取 900 兼顾清晰度与流畅度。
constexpr int kMaxRenderSide = 1120;   // 手机上更清晰（静止不重绘，多花点 CPU 可以接受）
constexpr double kTargetFrameSeconds = 1.0 / 60.0;
constexpr double kKeepRenderingSeconds = 0.4;

struct AppState {
    quiz::AppContext context;
    quiz::ui::UiApp ui{context};
    std::vector<uint32_t> pixels;
    int width = 0;
    int height = 0;
    ANativeWindow* window = nullptr;
    bool rendererReady = false;
    bool windowReady = false;
    bool animating = false;
    float touchScale = 1.0f;
    double lastFrameTime = 0.0;
    double lastInputTime = 0.0;
    double lastRepaintTime = 0.0;   // 上一次**真正重绘**的时刻（考试中按它补 1fps）
    bool firstFrame = true;
    /// 帧率统计：每 60 帧往 logcat 打一行（adb logcat -s AxtQuiz:V 可以看到）
    quiz::FrameStats frameStats{60};
};

AppState* stateOf(struct android_app* androidApp) {
    return static_cast<AppState*>(androidApp->userData);
}

// ---------------------------------------------------------------------------
// 窗口建立 / 尺寸变化：决定内部渲染分辨率并准备好像素缓冲
// ---------------------------------------------------------------------------
void setupWindow(AppState* state) {
    if (state->window == nullptr) {
        return;
    }
    const int surfaceWidth = ANativeWindow_getWidth(state->window);
    const int surfaceHeight = ANativeWindow_getHeight(state->window);
    if (surfaceWidth <= 0 || surfaceHeight <= 0) {
        return;
    }

    const int longest = surfaceWidth > surfaceHeight ? surfaceWidth : surfaceHeight;
    float scale = 1.0f;
    if (longest > kMaxRenderSide) {
        scale = static_cast<float>(kMaxRenderSide) / static_cast<float>(longest);
    }
    int renderWidth = static_cast<int>(surfaceWidth * scale);
    int renderHeight = static_cast<int>(surfaceHeight * scale);
    if (renderWidth < 1) renderWidth = 1;
    if (renderHeight < 1) renderHeight = 1;

    ANativeWindow_setBuffersGeometry(state->window, renderWidth, renderHeight, WINDOW_FORMAT_RGBA_8888);

    state->width = renderWidth;
    state->height = renderHeight;
    state->pixels.assign(static_cast<size_t>(renderWidth) * renderHeight, 0xFF101418u);
    state->touchScale = static_cast<float>(surfaceWidth) / static_cast<float>(renderWidth);

    if (!state->rendererReady) {
        quiz::render::initialize(renderWidth, renderHeight, 1.0f);
        state->rendererReady = true;
        state->context.initialize();
    }
    state->windowReady = true;
    state->firstFrame = true;
    quiz::logInfo("窗口就绪：屏幕 %dx%d，内部渲染 %dx%d（触摸缩放 %.2f）", surfaceWidth, surfaceHeight,
                  renderWidth, renderHeight, state->touchScale);
}

// ---------------------------------------------------------------------------
// 把像素缓冲贴到屏幕
// ---------------------------------------------------------------------------
void presentFrame(AppState* state) {
    if (!state->windowReady || state->window == nullptr || state->pixels.empty()) {
        return;
    }
    ANativeWindow_Buffer buffer{};
    if (ANativeWindow_lock(state->window, &buffer, nullptr) != 0) {
        return;  // Surface 可能已被销毁，下一帧再试
    }
    const int copyWidth = buffer.width < state->width ? buffer.width : state->width;
    auto* destination = static_cast<uint32_t*>(buffer.bits);
    const uint32_t* source = state->pixels.data();
    for (int y = 0; y < buffer.height && y < state->height; ++y) {
        std::memcpy(destination + static_cast<size_t>(y) * buffer.stride,
                    source + static_cast<size_t>(y) * state->width,
                    static_cast<size_t>(copyWidth) * sizeof(uint32_t));
    }
    ANativeWindow_unlockAndPost(state->window);
}

// ---------------------------------------------------------------------------
// 输入
// ---------------------------------------------------------------------------
/// 软键盘送来的可打印字符（AKEYCODE_A…Z / 0…9 / 常用符号）
char printableKeyChar(int32_t keyCode, bool shift) {
    if (keyCode >= AKEYCODE_A && keyCode <= AKEYCODE_Z) {
        return static_cast<char>((shift ? 'A' : 'a') + (keyCode - AKEYCODE_A));
    }
    if (keyCode >= AKEYCODE_0 && keyCode <= AKEYCODE_9) {
        return static_cast<char>('0' + (keyCode - AKEYCODE_0));
    }
    switch (keyCode) {
        case AKEYCODE_SPACE: return ' ';
        case AKEYCODE_COMMA: return shift ? '<' : ',';
        case AKEYCODE_PERIOD: return shift ? '>' : '.';
        case AKEYCODE_MINUS: return shift ? '_' : '-';
        case AKEYCODE_EQUALS: return shift ? '+' : '=';
        case AKEYCODE_SLASH: return shift ? '?' : '/';
        case AKEYCODE_SEMICOLON: return shift ? ':' : ';';
        case AKEYCODE_APOSTROPHE: return shift ? '"' : '\'';
        default: return 0;
    }
}

ImGuiKey translateKeyCode(int32_t keyCode) {
    switch (keyCode) {
        case AKEYCODE_SPACE: return ImGuiKey_Space;
        case AKEYCODE_ENTER: return ImGuiKey_Enter;
        case AKEYCODE_ESCAPE: return ImGuiKey_Escape;
        case AKEYCODE_DEL: return ImGuiKey_Backspace;
        case AKEYCODE_DPAD_UP: return ImGuiKey_UpArrow;
        case AKEYCODE_DPAD_DOWN: return ImGuiKey_DownArrow;
        case AKEYCODE_DPAD_LEFT: return ImGuiKey_LeftArrow;
        case AKEYCODE_DPAD_RIGHT: return ImGuiKey_RightArrow;
        default: return ImGuiKey_None;
    }
}

/// 「返回」：页面导航由 UiApp 统一处理；只有首页二次返回才退出。
void handleBack(AppState* state) {
    if (state->ui.handleBack()) {
        state->firstFrame = true;
        return;
    }
    state->context.saveProgress();
    quiz::platform::android::requestExit();
}

int32_t handleInput(struct android_app* androidApp, AInputEvent* event) {
    AppState* state = stateOf(androidApp);
    const int32_t type = AInputEvent_getType(event);

    if (type == AINPUT_EVENT_TYPE_MOTION) {
        const int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        const size_t pointerCount = AMotionEvent_getPointerCount(event);
        bool isDown = false;
        bool isUp = false;
        switch (action) {
            case AMOTION_EVENT_ACTION_DOWN:
            case AMOTION_EVENT_ACTION_POINTER_DOWN:
                isDown = true;
                break;
            case AMOTION_EVENT_ACTION_UP:
            case AMOTION_EVENT_ACTION_POINTER_UP:
                isUp = true;
                break;
            case AMOTION_EVENT_ACTION_CANCEL:
                quiz::render::addPointerEvent({0.0f, 0.0f, false, true});
                return 1;
            default:
                break;  // MOVE
        }

        // 只取第一根手指驱动 UI（ImGui 的鼠标接口就是单指针模型）。
        // Android 触摸坐标属于 surface 像素，而 ImGui / 渲染使用的是逻辑尺寸；
        // touchScale 是 surfaceWidth / renderWidth。若不先除掉它，高分屏上手指坐标
        // 会整体放大，命中框偏右下，滚动也会拖到错误的窗口。
        // DOWN / UP 走 addPointerEvent 保留边沿，MOVE 走 addPointerMove 只保留最新位置。
        const float x = AMotionEvent_getX(event, 0) / state->touchScale;
        const float y = AMotionEvent_getY(event, 0) / state->touchScale;
        if (isDown || isUp) {
            quiz::render::addPointerEvent({x, y, isDown, true});
        } else {
            quiz::render::addPointerMove(x, y, true);
        }
        (void)pointerCount;
        state->lastInputTime = quiz::platform::monotonicSeconds();
        return 1;
    }

    if (type == AINPUT_EVENT_TYPE_KEY) {
        const int32_t keyCode = AKeyEvent_getKeyCode(event);
        const int32_t action = AKeyEvent_getAction(event);
        const bool pressed = action == AKEY_EVENT_ACTION_DOWN;
        if (keyCode == AKEYCODE_BACK && pressed) {
            quiz::platform::android::requestBack();
            return 1;
        }
        // 软键盘送来的可打印字符：自己按 keyCode 映射（NDK 的 C 接口没有 getUnicodeChar）。
        // 英文 / 数字 / 常用符号够用；中文输入法「整段提交」的文字拿不到 —— 那需要
        // Java 层的 InputConnection，等接 SAF 时一起补。
        if (pressed) {
            const bool shiftKey = (AKeyEvent_getMetaState(event) & AMETA_SHIFT_ON) != 0;
            const char typed = printableKeyChar(keyCode, shiftKey);
            if (typed != 0) {
                quiz::render::addTextInput(static_cast<unsigned int>(static_cast<unsigned char>(typed)));
            }
        }
        const ImGuiKey key = translateKeyCode(keyCode);
        if (key != ImGuiKey_None) {
            quiz::render::addKeyEvent(key, pressed);
        }
        state->lastInputTime = quiz::platform::monotonicSeconds();
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 生命周期
// ---------------------------------------------------------------------------
void handleCommand(struct android_app* androidApp, int32_t command) {
    AppState* state = stateOf(androidApp);
    switch (command) {
        case APP_CMD_INIT_WINDOW:
            state->window = androidApp->window;
            setupWindow(state);
            break;
        case APP_CMD_TERM_WINDOW:
            state->window = nullptr;
            state->windowReady = false;
            break;
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONFIG_CHANGED:
        case APP_CMD_WINDOW_REDRAW_NEEDED:
            if (state->window != nullptr) {
                setupWindow(state);
            }
            break;
        case APP_CMD_GAINED_FOCUS:
            state->animating = true;
            state->lastFrameTime = quiz::platform::monotonicSeconds();
            state->firstFrame = true;
            break;
        case APP_CMD_LOST_FOCUS:
            state->animating = false;
            state->context.saveProgress();   // 切到后台时保存进度
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// 一帧
// ---------------------------------------------------------------------------
void renderOneFrame(AppState* state, float delta, bool active) {
    const double t0 = quiz::platform::monotonicSeconds();
    if (state->ui.needsFontRebuild()) {
        state->ui.applyFontRebuild();   // 必须在 beginFrame 之前
    }
    quiz::render::beginFrame(state->width, state->height, delta);
    const double t1 = quiz::platform::monotonicSeconds();
    state->ui.draw(delta);
    const double t2 = quiz::platform::monotonicSeconds();
    quiz::render::endFrameAndRender(state->pixels.data(), state->width, state->height, active);
    const double t3 = quiz::platform::monotonicSeconds();
    if (active) {
        presentFrame(state);
    }
    const double t4 = quiz::platform::monotonicSeconds();

    // 每帧都记录，active 表示这一帧是否真的重绘过（空闲帧不计入耗时平均）
    quiz::FrameBreakdown breakdown;
    breakdown.beginSeconds = t1 - t0;
    breakdown.drawSeconds = t2 - t1;
    breakdown.renderSeconds = t3 - t2;
    breakdown.presentSeconds = t4 - t3;
    state->frameStats.addFrame(delta > 0.0 ? delta : (t4 - t0), breakdown, active);

    // Android 上只在“本窗口确实重绘过”时才打日志，避免空闲时刷屏 logcat
    quiz::FrameStats::Snapshot snapshot;
    if (state->frameStats.consumeReport(snapshot) && snapshot.renderedFrames > 0) {
        quiz::logInfo("%s", quiz::FrameStats::formatReport(snapshot, "帧率").c_str());
    }
}

}  // namespace

// ===========================================================================
// Android 入口（相当于 Windows 的 main）
// ===========================================================================
void android_main(struct android_app* androidApp) {
    // 这里不能用函数内 static 的 AppState。
    // 同一个进程里 Activity 可能被销毁再重建（用户按返回退出后再点图标、系统回收 Activity、
    // 开发者选项里勾了「不保留活动」等），android_main 会被再调一次 —— 而 static 会带着上一次
    // 的 rendererReady=true 进来，可 ImGui 上下文在退出时已经 shutdown 掉了，于是 setupWindow
    // 不会再 initialize()，第一次 ImGui::GetIO() 就断言失败（实测 SIGABRT，tombstone 里能看到
    // 「No current context」）。每次进来重建一份，状态永远是干净的。
    static std::unique_ptr<AppState> stateHolder;
    stateHolder = std::make_unique<AppState>();
    AppState* state = stateHolder.get();

    androidApp->userData = state;
    androidApp->onAppCmd = handleCommand;
    androidApp->onInputEvent = handleInput;

    quiz::platform::android::setAndroidContext(androidApp->activity->assetManager, androidApp->activity);
    // 刷题时不要让屏幕自动熄灭
    ANativeActivity_setWindowFlags(androidApp->activity, AWINDOW_FLAG_KEEP_SCREEN_ON, 0);
    quiz::logInfo("android_main 开始：数据目录 = %s", quiz::platform::dataDirectory().c_str());

    state->lastFrameTime = quiz::platform::monotonicSeconds();
    state->lastInputTime = state->lastFrameTime;
    double lastBackHandledAt = -1.0;

    while (true) {
        int events = 0;
        struct android_poll_source* source = nullptr;
        // 没有交互时阻塞等待（-1），有交互后 0 秒超时以保持流畅；考试中改成 250ms 轮询一次，
        // 好让倒计时至少 1fps 地更新。超时必须**每次重算**：GAINED_FOCUS 就在这批命令里时，
        // 一次算死会让内层 while 继续拿 -1 阻塞下去，首帧永远画不出来（今天踩过）。
        while (ALooper_pollOnce(state->animating ? 0 : (state->context.examActive() ? 250 : -1),
                                nullptr, &events, reinterpret_cast<void**>(&source)) >= 0) {
            if (source != nullptr) {
                source->process(androidApp, source);
            }
            if (androidApp->destroyRequested != 0) {
                state->context.saveProgress();
                quiz::render::shutdown();
                quiz::logInfo("应用退出");
                return;
            }
        }

        if (quiz::platform::android::takeBackRequest()) {
            // 第二层消抖：即使 Java 端或系统迟到的重复请求已经入队，
            // 也只允许 350ms 内真正执行一次页面后退。
            const double backNow = quiz::platform::monotonicSeconds();
            if (lastBackHandledAt < 0.0 || backNow - lastBackHandledAt >= 0.35) {
                lastBackHandledAt = backNow;
                handleBack(state);
            }
        }

        if (!state->windowReady) {
            continue;
        }

        const double now = quiz::platform::monotonicSeconds();
        // 考试中：距上次**真正重绘**超过 1 秒就补一帧（倒计时至少 1fps）。
        // 注意不能拿 lastFrameTime 算 —— 它每轮都更新（空闲帧也算），永远差不到 1 秒。
        const bool examTick =
            state->context.examActive() && (now - state->lastRepaintTime) >= 1.0;
        if (!state->animating && !examTick) {
            continue;
        }

        float delta = static_cast<float>(now - state->lastFrameTime);
        state->lastFrameTime = now;

        // 从系统对话框（SAF 的打开 / 保存）回来、或者任何长时间停顿之后，帧间隔可能是几十秒。
        // 提示条停留时间、自动跳转倒计时、帧率统计都按 delta 走，所以这里按一帧算，
        // 免得它们被一个大数直接跳过去。
        if (delta > 0.25f) {
            delta = static_cast<float>(kTargetFrameSeconds);
            state->firstFrame = true;   // 顺便补一次完整重绘（回来后画面一定是最新的）
        }

        // 松手惯性可能超过 lastInput 的 0.4 秒重绘窗口，必须显式保持呈现。
        const bool scrollAnimating = quiz::ui::isScrollAnimationActive();
        const bool active = state->firstFrame || examTick || scrollAnimating ||
                            (now - state->lastInputTime) < kKeepRenderingSeconds;
        if (active) {
            state->lastRepaintTime = now;
        }
        state->firstFrame = false;

        // 跟随 ImGui 的输入焦点：点进输入框就拉起系统软键盘，离开就收起
        quiz::platform::android::syncSoftKeyboard(ImGui::GetIO().WantTextInput);
        renderOneFrame(state, delta, active);

        const double elapsed = quiz::platform::monotonicSeconds() - now;
        if (elapsed < kTargetFrameSeconds) {
            usleep(static_cast<useconds_t>((kTargetFrameSeconds - elapsed) * 1e6));
        }
    }
}

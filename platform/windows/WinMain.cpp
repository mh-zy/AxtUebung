// ============================================================================
// Windows 入口：开窗、贴图、把鼠标键盘喂给 ImGui，驱动 UI 与逻辑
// ----------------------------------------------------------------------------
// 运行方式：
//   1) 正常模式：AxtQuiz.exe
//        - 默认每 60 帧往控制台打一行帧率（含各阶段耗时），--no-fps 可关闭
//        - --fps 30 可以改成每 30 帧报一次
//   2) 无头截图：AxtQuiz.exe --shot out.ppm --screen quiz --size 900x1200
//   3) 性能基准：AxtQuiz.exe --bench 300 --size 900x1200
//   4) 题库校验：AxtQuiz.exe --validate 题库.quiz
//   5) 导出题库：AxtQuiz.exe --export bank|wrong|marked
// ============================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX   // 不定义 min/max 宏，避免和 std::min/std::max 打架
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>
#include <string>

#include "quiz/app/AppContext.h"
#include "quiz/io/ExamBuilder.h"
#include "quiz/platform/Platform.h"
#include "quiz/render/ImGuiRenderer.h"
#include "quiz/render/SvgIcon.h"
#include "quiz/ui/UiApp.h"
#include "quiz/ui/UiScroll.h"
#include "quiz/util/FrameStats.h"
#include "quiz/util/Log.h"

namespace {

constexpr wchar_t kWindowClass[] = L"AxtQuizWindowClass";
constexpr wchar_t kWindowTitle[] = L"Axt 刷题（C++ / ImGui / Windows + Android 同一份核心）";
constexpr int kDefaultWidth = 960;
constexpr int kDefaultHeight = 720;
constexpr double kTargetFrameSeconds = 1.0 / 60.0;
/// 最后一次输入之后继续重绘这么久（覆盖按钮悬停/按下的动画）
constexpr double kKeepRenderingSeconds = 0.4;

quiz::AppContext g_context;
quiz::ui::UiApp g_ui(g_context);
std::vector<uint32_t> g_pixels;
int g_width = kDefaultWidth;
int g_height = kDefaultHeight;
bool g_running = true;
bool g_mouseDown = false;
double g_lastInputSeconds = 0.0;

// ---- 贴屏用的 DIB 段（只创建一次，之后每帧 memcpy + BitBlt）----
// 之前用 StretchDIBits 直接把像素缓冲交给 GDI：在 960x720 下实测每帧要 33 ms，
// 因为 GDI 会逐像素做格式转换。改成“内存 DC + DIB 段 + BitBlt”后只剩一次内存拷贝。
HBITMAP g_dibBitmap = nullptr;
void* g_dibBits = nullptr;
HDC g_memoryDc = nullptr;
int g_dibWidth = 0;
int g_dibHeight = 0;

void releasePresentSurface() {
    if (g_memoryDc != nullptr) {
        DeleteDC(g_memoryDc);
        g_memoryDc = nullptr;
    }
    if (g_dibBitmap != nullptr) {
        DeleteObject(g_dibBitmap);
        g_dibBitmap = nullptr;
    }
    g_dibBits = nullptr;
    g_dibWidth = 0;
    g_dibHeight = 0;
}

void ensurePresentSurface(HDC referenceDc, int width, int height) {
    if (g_dibBitmap != nullptr && g_dibWidth == width && g_dibHeight == height) {
        return;
    }
    releasePresentSurface();

    BITMAPV4HEADER header{};
    header.bV4Size = sizeof(BITMAPV4HEADER);
    header.bV4Width = width;
    header.bV4Height = -height;   // 负数 = 自上而下，与我们的像素缓冲一致
    header.bV4Planes = 1;
    header.bV4BitCount = 32;
    header.bV4V4Compression = BI_BITFIELDS;
    header.bV4RedMask = 0x000000FF;
    header.bV4GreenMask = 0x0000FF00;
    header.bV4BlueMask = 0x00FF0000;
    header.bV4AlphaMask = 0xFF000000;

    void* bits = nullptr;
    g_dibBitmap = CreateDIBSection(referenceDc, reinterpret_cast<const BITMAPINFO*>(&header),
                                   DIB_RGB_COLORS, &bits, nullptr, 0);
    if (g_dibBitmap == nullptr || bits == nullptr) {
        releasePresentSurface();
        return;
    }
    g_dibBits = bits;
    g_memoryDc = CreateCompatibleDC(referenceDc);
    if (g_memoryDc == nullptr) {
        releasePresentSurface();
        return;
    }
    SelectObject(g_memoryDc, g_dibBitmap);
    g_dibWidth = width;
    g_dibHeight = height;
}

void resetFramebuffer(int width, int height) {
    g_width = width > 0 ? width : 1;
    g_height = height > 0 ? height : 1;
    g_pixels.assign(static_cast<size_t>(g_width) * g_height, 0xFF101418u);
    releasePresentSurface();   // 尺寸变了，贴屏用的 DIB 段要重建
}

void presentToWindow(HWND window) {
    HDC dc = GetDC(window);
    if (dc == nullptr || g_pixels.empty()) {
        return;
    }
    ensurePresentSurface(dc, g_width, g_height);
    if (g_dibBits != nullptr && g_memoryDc != nullptr) {
        // 一次内存拷贝 + 一次位块传送：比 StretchDIBits 逐像素转换快一个数量级
        std::memcpy(g_dibBits, g_pixels.data(),
                    static_cast<size_t>(g_width) * static_cast<size_t>(g_height) * sizeof(uint32_t));
        BitBlt(dc, 0, 0, g_width, g_height, g_memoryDc, 0, 0, SRCCOPY);
    }
    ReleaseDC(window, dc);
}

ImGuiKey translateKey(WPARAM key) {
    switch (key) {
        case VK_SPACE: return ImGuiKey_Space;
        case VK_RETURN: return ImGuiKey_Enter;
        case VK_ESCAPE: return ImGuiKey_Escape;
        case VK_TAB: return ImGuiKey_Tab;
        case VK_BACK: return ImGuiKey_Backspace;
        case VK_LEFT: return ImGuiKey_LeftArrow;
        case VK_RIGHT: return ImGuiKey_RightArrow;
        case VK_UP: return ImGuiKey_UpArrow;
        case VK_DOWN: return ImGuiKey_DownArrow;
        default: return ImGuiKey_None;
    }
}

/// 处理「返回」：页面导航由 UiApp 统一处理；首页二次返回才退出。
void handleBack() {
    if (g_ui.handleBack()) {
        return;
    }
    g_context.saveProgress();
    g_running = false;
    PostQuitMessage(0);
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_SIZE:
            resetFramebuffer(LOWORD(lParam), HIWORD(lParam));
            g_lastInputSeconds = quiz::platform::monotonicSeconds();
            return 0;
        case WM_MOUSEMOVE:
            quiz::render::addPointerMove(static_cast<float>(LOWORD(lParam)), static_cast<float>(HIWORD(lParam)), false);
            g_lastInputSeconds = quiz::platform::monotonicSeconds();
            return 0;
        case WM_LBUTTONDOWN:
            g_mouseDown = true;
            SetCapture(window);
            quiz::render::addPointerEvent({static_cast<float>(LOWORD(lParam)),
                                           static_cast<float>(HIWORD(lParam)), true, false});
            g_lastInputSeconds = quiz::platform::monotonicSeconds();
            return 0;
        case WM_LBUTTONUP:
            g_mouseDown = false;
            ReleaseCapture();
            quiz::render::addPointerEvent({static_cast<float>(LOWORD(lParam)),
                                           static_cast<float>(HIWORD(lParam)), false, false});
            g_lastInputSeconds = quiz::platform::monotonicSeconds();
            return 0;
        case WM_MOUSEWHEEL:
            quiz::render::addScroll(static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / 120.0f);
            g_lastInputSeconds = quiz::platform::monotonicSeconds();
            return 0;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            const ImGuiKey key = translateKey(wParam);
            if (key != ImGuiKey_None) {
                quiz::render::addKeyEvent(key, true);
            }
            if (wParam == VK_ESCAPE) {
                handleBack();
            }
            g_lastInputSeconds = quiz::platform::monotonicSeconds();
            return 0;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            const ImGuiKey key = translateKey(wParam);
            if (key != ImGuiKey_None) {
                quiz::render::addKeyEvent(key, false);
            }
            return 0;
        }
        case WM_CHAR:
            quiz::render::addTextInput(static_cast<unsigned int>(wParam));
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            BeginPaint(window, &paint);
            EndPaint(window, &paint);
            presentToWindow(window);
            return 0;
        }
        case WM_DESTROY:
            g_running = false;
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void enableDpiAwareness() {
    using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    HMODULE user32 = LoadLibraryA("user32.dll");
    if (user32 == nullptr) {
        return;
    }
    auto setContext = reinterpret_cast<SetProcessDpiAwarenessContextFn>(
        GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (setContext != nullptr) {
        setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
}

bool initializeApplication(int width, int height) {
    resetFramebuffer(width, height);
    if (!quiz::render::initialize(g_width, g_height, 1.0f)) {
        quiz::logError("ImGui 初始化失败");
        return false;
    }
    if (!g_context.initialize()) {
        quiz::logWarn("没有找到任何题库，界面会提示放置题库的位置");
    }
    return true;
}

// ---------------------------------------------------------------------------
// 正常模式：开窗跑主循环，并按固定间隔在控制台输出帧率
// ---------------------------------------------------------------------------
/// @param forceFrames 大于 0 时进入“窗口基准”模式：强制每帧都重绘，跑满这么多帧后自动退出
/// @param windowWidth/windowHeight 窗口客户区大小（默认 960x720，可用 --size 指定）
int runWindow(bool fpsEnabled, int fpsIntervalFrames, int forceFrames, int windowWidth, int windowHeight) {
    enableDpiAwareness();
    if (!initializeApplication(windowWidth, windowHeight)) {
        return 1;
    }
    const int clientWidth = g_width;
    const int clientHeight = g_height;

    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWindowClass;
    if (RegisterClassExW(&windowClass) == 0) {
        quiz::logError("注册窗口类失败");
        return 1;
    }

    RECT rect{0, 0, clientWidth, clientHeight};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    HWND window = CreateWindowExW(0, kWindowClass, kWindowTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                  CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
                                  nullptr, nullptr, instance, nullptr);
    if (window == nullptr) {
        quiz::logError("创建窗口失败");
        return 1;
    }
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    quiz::FrameStats frameStats(fpsIntervalFrames);
    if (fpsEnabled) {
        quiz::logInfo("已开启帧率输出：每 %d 帧打印一次（用 --no-fps 关闭，用 --fps 30 调整间隔）",
                      fpsIntervalFrames);
    }

    double lastTime = quiz::platform::monotonicSeconds();
    double lastExamRepaint = lastTime;   // 考试中每秒强制重绘一次（倒计时）
    g_lastInputSeconds = lastTime;
    bool firstFrame = true;
    int renderedFrames = 0;
    if (forceFrames > 0) {
        quiz::logInfo("窗口基准模式：强制重绘 %d 帧后自动退出（尺寸 %dx%d）", forceFrames, g_width, g_height);
    }

    while (g_running) {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                g_running = false;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!g_running) {
            break;
        }

        const double now = quiz::platform::monotonicSeconds();
        const float delta = static_cast<float>(now - lastTime);
        lastTime = now;

        // 空闲时不重新光栅化（界面没变化就不用重画，省 CPU/省电）；
        // 考试中倒计时每秒都在走 → 至少 1fps 重绘一次
        const bool examTick = g_context.examActive() && (now - lastExamRepaint) >= 1.0;
        if (examTick) {
            lastExamRepaint = now;
        }
        // 惯性期间即使“最近一次输入”已经超过 0.4 秒，也必须继续呈现，否则最后的
        // 衰减段只更新 ScrollY、屏幕不动。
        const bool scrollAnimating = quiz::ui::isScrollAnimationActive();
        const bool active = forceFrames > 0 || firstFrame || examTick || scrollAnimating ||
                            (now - g_lastInputSeconds) < kKeepRenderingSeconds;
        firstFrame = false;

        // ---- 分阶段计时：begin → draw → 光栅化 → 贴屏 ----
        const double t0 = quiz::platform::monotonicSeconds();
        if (g_ui.needsFontRebuild()) {
            g_ui.applyFontRebuild();   // 必须在 NewFrame 之前
        }
        quiz::platform::applyPendingWindowResize();   // 有登记的窗口尺寸就现在应用（不能在帧中途改）
        quiz::render::beginFrame(g_width, g_height, delta);
        const double t1 = quiz::platform::monotonicSeconds();
        g_ui.draw(delta);
        const double t2 = quiz::platform::monotonicSeconds();
        quiz::render::endFrameAndRender(g_pixels.data(), g_width, g_height, active);
        const double t3 = quiz::platform::monotonicSeconds();
        if (active) {
            presentToWindow(window);
        }
        const double t4 = quiz::platform::monotonicSeconds();

        // 每一帧都记录（空闲帧也计入帧率），但用 active 标记它是否真的重绘过，
        // 这样报告里既能看帧率，也能看“60 帧里重绘了几帧”，不会被空闲帧稀释
        if (fpsEnabled) {
            quiz::FrameBreakdown breakdown;
            breakdown.beginSeconds = t1 - t0;
            breakdown.drawSeconds = t2 - t1;
            breakdown.renderSeconds = t3 - t2;
            breakdown.presentSeconds = t4 - t3;
            // 前两帧包含字体图集首次创建等一次性开销，跳过以免污染平均值
            if (active && renderedFrames >= 2) {
                frameStats.addFrame(delta > 0.0 ? delta : (t4 - t0), breakdown, true);
            }

            quiz::FrameStats::Snapshot snapshot;
            if (frameStats.consumeReport(snapshot)) {
                quiz::logInfo("%s", quiz::FrameStats::formatReport(snapshot, "帧率").c_str());
            }
        }
        if (active) {
            ++renderedFrames;
            if (forceFrames > 0 && renderedFrames >= forceFrames) {
                const quiz::FrameStats::Snapshot total = frameStats.lifetimeSnapshot();
                if (total.frames > 0) {
                    quiz::logInfo("%s", quiz::FrameStats::formatReport(total, "窗口基准总计").c_str());
                }
                g_running = false;
                break;
            }
        }

        const double frameEnd = quiz::platform::monotonicSeconds();
        const double elapsed = frameEnd - now;
        if (elapsed < kTargetFrameSeconds) {
            Sleep(static_cast<DWORD>((kTargetFrameSeconds - elapsed) * 1000.0));
        }
    }

    if (fpsEnabled) {
        const quiz::FrameStats::Snapshot total = frameStats.lifetimeSnapshot();
        if (total.frames > 0) {
            quiz::logInfo("%s", quiz::FrameStats::formatReport(total, "本次运行总计").c_str());
        }
    }

    g_context.saveProgress();
    releasePresentSurface();
    quiz::render::shutdown();
    quiz::logInfo("窗口关闭，程序退出");
    return 0;
}

// ---------------------------------------------------------------------------
// 基准模式：不打开窗口，连续渲染 N 帧并输出统计
// 这是给优化前后做对比用的：改代码 → 跑 --bench → 看数字变化
// ---------------------------------------------------------------------------
int runBench(int width, int height, int frames, const std::string& screen) {
    if (!initializeApplication(width, height)) {
        return 1;
    }
    g_context.openBank(0);
    if (screen == "quiz") {
        g_ui.setScreen(quiz::ui::Screen::Quiz);
    } else if (screen == "result") {
        g_ui.setScreen(quiz::ui::Screen::Result);
    } else if (screen == "settings") {
        g_ui.setScreen(quiz::ui::Screen::Settings);
    } else {
        g_ui.setScreen(quiz::ui::Screen::Home);
    }

    const double pixels = static_cast<double>(g_width) * static_cast<double>(g_height);
    std::printf("基准测试：%d 帧，%dx%d（%.1f 万像素/帧），页面 %s\n", frames, g_width, g_height,
                pixels / 10000.0, screen.c_str());

    quiz::FrameStats frameStats(frames > 0 ? frames : 1);
    const double fixedDelta = 1.0 / 60.0;
    for (int i = 0; i < frames; ++i) {
        const double t0 = quiz::platform::monotonicSeconds();
        if (g_ui.needsFontRebuild()) {
            g_ui.applyFontRebuild();
        }
        quiz::render::beginFrame(g_width, g_height, static_cast<float>(fixedDelta));
        const double t1 = quiz::platform::monotonicSeconds();
        g_ui.draw(static_cast<float>(fixedDelta));
        const double t2 = quiz::platform::monotonicSeconds();
        quiz::render::endFrameAndRender(g_pixels.data(), g_width, g_height, true);
        const double t3 = quiz::platform::monotonicSeconds();

        quiz::FrameBreakdown breakdown;
        breakdown.beginSeconds = t1 - t0;
        breakdown.drawSeconds = t2 - t1;
        breakdown.renderSeconds = t3 - t2;
        frameStats.addFrame(t3 - t0, breakdown);
    }

    const quiz::FrameStats::Snapshot snapshot = frameStats.lifetimeSnapshot();
    std::printf("\n==== 基准结果 ====\n");
    std::printf("%s\n", quiz::FrameStats::formatReport(snapshot, "平均").c_str());
    std::printf("说明：基准模式没有窗口，所以“贴屏”为 0；真实窗口模式下贴屏会占一部分时间。\n");
    std::printf("像素吞吐：约 %.1f M像素/秒（%.1f 万像素 ÷ 平均耗时）\n",
                snapshot.averageFrameMs > 0.0 ? (pixels / 1000000.0) / (snapshot.averageFrameMs / 1000.0) : 0.0,
                pixels / 10000.0);
    quiz::render::shutdown();
    return 0;
}

/// 把帧缓冲写出成 PPM 图片（最简单的无损图片格式，任何看图工具都能打开）
bool writePpm(const std::string& path, const std::vector<uint32_t>& pixels, int width, int height) {
    FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return false;
    }
    std::fprintf(file, "P6\n%d %d\n255\n", width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const uint32_t pixel = pixels[static_cast<size_t>(y) * width + x];
            const unsigned char rgb[3] = {
                static_cast<unsigned char>(pixel & 0xFF),
                static_cast<unsigned char>((pixel >> 8) & 0xFF),
                static_cast<unsigned char>((pixel >> 16) & 0xFF),
            };
            std::fwrite(rgb, 1, 3, file);
        }
    }
    std::fclose(file);
    return true;
}

/// @param clickX/clickY 大于等于 0 时，在渲染前注入一次真实的鼠标点击
///        （用来验证“点按钮能不能真的切页面”这类交互路径，无需人工点击）
int runShot(const std::string& outputPath, int width, int height, const std::string& screen,
            int bankIndex, int examIndex, int questionIndex, int answerIndex, int frames,
            const std::vector<std::pair<float, float>>& clicks, const std::string& typeText,
            bool typeEnter, const std::vector<int>& planOrder) {
    if (!initializeApplication(width, height)) {
        return 1;
    }
    if (screen == "quiz" || screen == "result") {
        g_context.openBank(bankIndex);
        if (!planOrder.empty()) {
            // 测试用：直接指定本轮练习顺序，并跳到计划里的第一题
            g_context.session().setOrder(planOrder);
            g_context.session().jumpToRoundStart();
        }
        if (questionIndex > 0) {
            g_context.session().jumpTo(questionIndex);
        }
        if (answerIndex >= 0) {
            // 造出「已作答」的状态，截图里能看到判分与解析
            quiz::QuizSession& session = g_context.session();
            session.selectOption(answerIndex);
            session.submit(quiz::platform::nowString());
            if (screen == "result") {
                session.next();
                session.selectOption(answerIndex);
                session.submit(quiz::platform::nowString());
            }
        }
        // 顺带把进度落盘，这样 --shot 也能验证「进度保存」这条链路
        g_context.saveProgress();
        g_ui.setScreen(screen == "quiz" ? quiz::ui::Screen::Quiz : quiz::ui::Screen::Result);
    } else if (screen == "home") {
        // 首页也用 --bank 指定题库（这样截图能看某个题库的卡片与按钮状态）
        g_context.openBank(bankIndex);
    } else if (screen == "exam" || screen == "examresult") {
        // 考试模式截图：开一场考试（--exam 选第几套卷），可选先作答再交卷
        g_context.openBank(bankIndex);
        std::string error;
        if (!g_context.startExam(examIndex, error)) {
            std::printf("开不了这场考试：%s\n", error.c_str());
        } else {
            if (questionIndex > 0) {
                g_context.session().jumpTo(questionIndex);   // 截图用：直接看卷面某一题
            }
            if (answerIndex >= 0) {
                g_context.session().selectOption(answerIndex);   // 只记草稿（考试中不判分）
            }
            if (screen == "examresult") {
                g_context.finishExam();
                g_ui.setScreen(quiz::ui::Screen::ExamResult);
            } else {
                g_ui.setScreen(quiz::ui::Screen::Quiz);
            }
        }
    } else if (screen == "settings") {
        g_ui.setScreen(quiz::ui::Screen::Settings);
    } else if (screen == "stats") {
        g_ui.setScreen(quiz::ui::Screen::Stats);
    } else if (screen == "finished") {
        g_ui.setScreen(quiz::ui::Screen::Finished);
    }

    const int totalFrames = frames > 0 ? frames : 6;
    for (int i = 0; i < totalFrames; ++i) {
        // 注入点击：前两帧先让 ImGui 建立“窗口 / 悬停”信息（第 0 帧时它还不知道鼠标在窗口上），
        // 第 2 帧按下、第 3 帧松开 —— 这才是 ImGui 眼里的完整一次点击。
        // 注入点击：前两帧先让 ImGui 建立「窗口 / 悬停」信息，然后按下、松开。
        // 多个点击（--click "x,y;x2,y2"）依次相隔 4 帧。
        for (size_t k = 0; k < clicks.size(); ++k) {
            const int downFrame = 2 + static_cast<int>(k) * 4;
            if (i == downFrame) {
                quiz::render::addPointerEvent({clicks[k].first, clicks[k].second, true, false});
            } else if (i == downFrame + 1) {
                quiz::render::addPointerEvent({clicks[k].first, clicks[k].second, false, false});
            }
        }
        // 注入键入：等点击都做完再喂字符（ImGui 下一帧统一处理），然后敲一次回车
        if (!typeText.empty()) {
            const int typeFrame = 4 + static_cast<int>(clicks.size()) * 4;
            if (i == typeFrame) {
                for (unsigned char ch : typeText) {
                    quiz::render::addTextInput(static_cast<unsigned int>(ch));
                }
            } else if (typeEnter && i == typeFrame + 2) {
                quiz::render::addKeyEvent(ImGuiKey_Enter, true);
            } else if (typeEnter && i == typeFrame + 3) {
                quiz::render::addKeyEvent(ImGuiKey_Enter, false);
            }
        }
        if (g_ui.needsFontRebuild()) {
            g_ui.applyFontRebuild();
        }
        quiz::render::beginFrame(g_width, g_height, 1.0f / 60.0f);
        g_ui.draw(1.0f / 60.0f);
        quiz::render::endFrameAndRender(g_pixels.data(), g_width, g_height, true);
    }

    const bool ok = writePpm(outputPath, g_pixels, g_width, g_height);
    const char* screenName = "home";
    if (g_ui.screen() == quiz::ui::Screen::Quiz) {
        screenName = "quiz";
    } else if (g_ui.screen() == quiz::ui::Screen::Result) {
        screenName = "result";
    } else if (g_ui.screen() == quiz::ui::Screen::Settings) {
        screenName = "settings";
    } else if (g_ui.screen() == quiz::ui::Screen::ExamResult) {
        screenName = "examresult";
    }
    quiz::render::shutdown();
    if (!ok) {
        quiz::logError("写入图片失败：%s", outputPath.c_str());
        return 1;
    }
    std::printf("已写入 %s（%dx%d）| 渲染结束时界面停在：%s\n", outputPath.c_str(), g_width, g_height,
                screenName);
    return 0;
}

/// 把一张 SVG 光栅化成图片，用来单独看图标画得对不对：
///   AxtQuiz.exe --icon assets\icons\download.svg out.ppm [--size 96x96] [--color 96DCB4]
/// 调试用：把系统「打开 / 保存」文件对话框弹出来（无头环境里截图验证用）
int runPickTest(const std::string& mode) {
    // 对话框现在是 begin / take 两步：桌面端 begin 里就是模态对话框，take 当场拿到结果
    if (mode == "save") {
        if (!quiz::platform::beginPickSavePath("Axt 刷题：导出到…",
                                               "D:\\AxtUebung\\build\\pick-test.quiz", {".quiz"})) {
            std::printf("保存对话框：弹不出来\n");
            return 1;
        }
        std::string out;
        bool cancelled = false;
        quiz::platform::takePickedSavePath(out, cancelled);
        std::printf("保存对话框：%s%s\n", cancelled ? "已取消" : "已选择",
                    cancelled ? "" : out.c_str());
        return cancelled ? 1 : 0;
    }
    if (!quiz::platform::beginPickFiles("Axt 刷题：选择题库文件", {"quiz", "txt", "text"}, true)) {
        std::printf("打开对话框：弹不出来\n");
        return 1;
    }
    std::vector<std::string> paths;
    bool cancelled = false;
    quiz::platform::takePickedFiles(paths, cancelled);
    std::printf("打开对话框：%s（选了 %d 个）\n", cancelled ? "已取消" : "已选择",
                static_cast<int>(paths.size()));
    for (const std::string& path : paths) {
        std::printf("  %s\n", path.c_str());
    }
    return cancelled ? 0 : 1;
}

int runIcon(const std::string& svgPath, const std::string& outPath, int size, unsigned int colorRgb,
            float inkFraction) {
    std::string text;
    if (!quiz::platform::readTextFile(svgPath, text) &&
        !quiz::platform::readAssetText(svgPath, text)) {
        std::printf("读不到 SVG：%s\n", svgPath.c_str());
        return 1;
    }
    quiz::render::svg::Image image;
    std::string error;
    const bool ok = inkFraction > 0.0f
                        ? quiz::render::svg::rasterizeFitted(text, size, inkFraction, colorRgb, image,
                                                             error)
                        : quiz::render::svg::rasterize(text, size, colorRgb, image, error);
    if (!ok) {
        std::printf("光栅化失败：%s\n", error.c_str());
        return 2;
    }
    // 纹理里是预乘 alpha；写到图片时还原成直通 alpha，方便肉眼看
    std::vector<uint32_t> pixels = image.pixels;
    for (uint32_t& pixel : pixels) {
        const uint32_t alpha = pixel >> 24;
        if (alpha == 0 || alpha == 255) {
            continue;
        }
        const uint32_t r = (pixel & 0xFF) * 255 / alpha;
        const uint32_t g = ((pixel >> 8) & 0xFF) * 255 / alpha;
        const uint32_t b = ((pixel >> 16) & 0xFF) * 255 / alpha;
        pixel = (r > 255 ? 255 : r) | ((g > 255 ? 255 : g) << 8) | ((b > 255 ? 255 : b) << 16) |
                (alpha << 24);
    }
    if (!writePpm(outPath, pixels, image.width, image.height)) {
        std::printf("写图失败：%s\n", outPath.c_str());
        return 3;
    }
    std::printf("已写入 %s（%dx%d）\n", outPath.c_str(), image.width, image.height);
    return 0;
}

void printUsage() {
    std::printf(
        "Axt 刷题（C++ 版）\n"
        "用法：\n"
        "  AxtQuiz.exe [--fps N | --no-fps]                打开窗口\n"
        "      默认每 60 帧在控制台打印一行帧率（含各阶段耗时）；\n"
        "      --fps 30 改成每 30 帧一次，--no-fps 关闭输出\n"
        "  AxtQuiz.exe --bench [N] [--size 900x1200] [--screen home|quiz|result|settings]\n"
        "      基准模式：不开窗口连续渲染 N 帧（默认 300），输出平均/最快/最慢与各阶段耗时\n"
        "  AxtQuiz.exe --shot out.ppm [--screen home|quiz|result|settings|stats|finished|exam|examresult]\n"
        "               [--size 900x1200] [--bank 0] [--exam 0]（exam/examresult 用第几套卷）\n"
        "               [--bank 0] [--question 2] [--answer 0] [--frames 3] [--click X,Y] [--type 2.5]\n"
        "               [--plan \"3,1,0,2\"]（测试用：指定本轮练习顺序）\n"
        "               （--click 支持多点，用分号隔开：--click \"100,200;300,400\"）\n"
        "                                                  无头渲染某个页面为图片\n"
        "  AxtQuiz.exe --validate <题库文件>                校验题库格式并打印问题\n"
        "  AxtQuiz.exe --paper <卷id> [--bank 0]           打印这套卷抽出来的题与分值\n"
        "               （卷id 写 any = 题库里第一套卷；卷里写了 seed 的话每次都一样）\n"
        "  AxtQuiz.exe --icon <图标.svg> <out.ppm> [--size 96x96] [--color RRGGBB] [--fit 0.62]\n"
        "      --fit：按「墨迹」归一化（最长边占 0.62），量图标大小用\n"
        "                                                  把 SVG 图标光栅化成图片（调试图标用）\n"
        "  AxtQuiz.exe --export bank|wrong|marked [--bank 0]\n"
        "                                                  导出题库 / 错题本 / 收藏题\n"
        "  AxtQuiz.exe --import <文件> [--import <文件>…]   把题库文件导入 data/banks\n"
        "  AxtQuiz.exe --pick-test open|save               调试：弹出系统文件对话框\n"
        "                                                  接受 .quiz / .txt / .text，导入后统一成 .quiz\n");
}

}  // namespace

int main(int argc, char** argv) {
    // 让控制台按 UTF-8 输出，中文日志才不会变成乱码
    SetConsoleOutputCP(CP_UTF8);

    std::string shotPath;
    std::string screen = "home";
    std::string typeText;
    std::string iconSvg;
    std::string iconOut;
    unsigned int iconColor = 0xFFFFFFu;
    float iconFit = 0.0f;   // --icon ... --fit 0.62：按墨迹归一化后光栅化
    std::string paperId;                    // --paper <卷id>：打印这套卷抽出来的题
    std::string validatePath;
    std::string exportKind;
    std::vector<std::string> importPaths;   // --import <文件>（可写多次）
    std::string pickTest;                   // --pick-test open|save（调试用）
    int width = 900;
    int height = 1200;
    int answerIndex = -1;
    int frames = 3;
    int bankIndex = 0;
    int questionIndex = 0;
    int examIndex = 0;                      // --exam N：用题库里的第几套卷
    int fpsInterval = 60;
    bool fpsEnabled = true;
    bool typeEnter = true;   // --no-enter：只打字不按回车（测输入框是否实时更新）
    int benchFrames = 0;
    int benchWindowFrames = 0;
    std::vector<std::pair<float, float>> clicks;   // --click "x,y;x2,y2"
    std::vector<int> planOrder;                    // --plan "3,1,0,2"（测试用：指定本轮顺序）

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "--shot") == 0 && i + 1 < argc) {
            shotPath = argv[++i];
        } else if (std::strcmp(arg, "--screen") == 0 && i + 1 < argc) {
            screen = argv[++i];
        } else if (std::strcmp(arg, "--size") == 0 && i + 1 < argc) {
            const char* size = argv[++i];
            const char* x = std::strchr(size, 'x');
            if (x != nullptr) {
                width = std::atoi(size);
                height = std::atoi(x + 1);
            }
        } else if (std::strcmp(arg, "--answer") == 0 && i + 1 < argc) {
            answerIndex = std::atoi(argv[++i]);
        } else if (std::strcmp(arg, "--frames") == 0 && i + 1 < argc) {
            frames = std::atoi(argv[++i]);
        } else if (std::strcmp(arg, "--paper") == 0 && i + 1 < argc) {
            paperId = argv[++i];
        } else if (std::strcmp(arg, "--exam") == 0 && i + 1 < argc) {
            examIndex = std::atoi(argv[++i]);
            if (examIndex < 0) {
                examIndex = 0;
            }
        } else if (std::strcmp(arg, "--validate") == 0 && i + 1 < argc) {
            validatePath = argv[++i];
        } else if (std::strcmp(arg, "--pick-test") == 0 && i + 1 < argc) {
            pickTest = argv[++i];
        } else if (std::strcmp(arg, "--import") == 0 && i + 1 < argc) {
            importPaths.emplace_back(argv[++i]);
        } else if (std::strcmp(arg, "--export") == 0 && i + 1 < argc) {
            exportKind = argv[++i];
        } else if (std::strcmp(arg, "--bank") == 0 && i + 1 < argc) {
            bankIndex = std::atoi(argv[++i]);
        } else if (std::strcmp(arg, "--question") == 0 && i + 1 < argc) {
            questionIndex = std::atoi(argv[++i]);
            if (questionIndex < 0) {
                questionIndex = 0;
            }
        } else if (std::strcmp(arg, "--plan") == 0 && i + 1 < argc) {
            // 测试用：--plan "3,1,0,2" 指定本轮练习顺序（逗号隔开，按原始题号）
            const std::string list = argv[++i];
            size_t begin = 0;
            while (begin <= list.size()) {
                const size_t end = list.find(',', begin);
                const std::string item =
                    list.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
                if (!item.empty()) {
                    planOrder.push_back(std::atoi(item.c_str()));
                }
                if (end == std::string::npos) {
                    break;
                }
                begin = end + 1;
            }
        } else if (std::strcmp(arg, "--fps") == 0) {
            fpsEnabled = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                fpsInterval = std::atoi(argv[++i]);
                if (fpsInterval <= 0) {
                    fpsInterval = 60;
                }
            }
        } else if (std::strcmp(arg, "--no-fps") == 0) {
            fpsEnabled = false;
        } else if (std::strcmp(arg, "--bench") == 0) {
            benchFrames = 300;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                benchFrames = std::atoi(argv[++i]);
                if (benchFrames <= 0) {
                    benchFrames = 300;
                }
            }
        } else if (std::strcmp(arg, "--bench-window") == 0) {
            benchWindowFrames = 120;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                benchWindowFrames = std::atoi(argv[++i]);
                if (benchWindowFrames <= 0) {
                    benchWindowFrames = 120;
                }
            }
        } else if (std::strcmp(arg, "--click") == 0 && i + 1 < argc) {
            // 支持多个点击，用分号隔开："x,y;x2,y2"
            const std::string list = argv[++i];
            size_t begin = 0;
            while (begin <= list.size()) {
                const size_t end = list.find(';', begin);
                const std::string item =
                    list.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
                const size_t comma = item.find(',');
                if (comma != std::string::npos) {
                    clicks.push_back({static_cast<float>(std::atof(item.substr(0, comma).c_str())),
                                      static_cast<float>(std::atof(item.substr(comma + 1).c_str()))});
                }
                if (end == std::string::npos) {
                    break;
                }
                begin = end + 1;
            }
        } else if (std::strcmp(arg, "--icon") == 0 && i + 2 < argc) {
            iconSvg = argv[++i];
            iconOut = argv[++i];
        } else if (std::strcmp(arg, "--color") == 0 && i + 1 < argc) {
            unsigned int rgb = 0;
            if (quiz::Settings::parseHexColor(argv[i + 1], rgb)) {
                iconColor = rgb;
            }
            ++i;
        } else if (std::strcmp(arg, "--fit") == 0 && i + 1 < argc) {
            iconFit = static_cast<float>(std::atof(argv[++i]));
        } else if (std::strcmp(arg, "--no-enter") == 0) {
            typeEnter = false;
        } else if (std::strcmp(arg, "--type") == 0 && i + 1 < argc) {
            typeText = argv[++i];
        } else if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
            printUsage();
            return 0;
        } else {
            std::printf("未知参数：%s\n", arg);
            printUsage();
            return 1;
        }
    }

    if (!iconSvg.empty()) {
        return runIcon(iconSvg, iconOut, width, iconColor, iconFit);
    }

    if (!paperId.empty()) {
        // 命令行抽题：把某套卷抽出来打印（校验 [exam] / [draw] 写对没写对）
        if (!g_context.initialize()) {
            std::printf("没有找到题库目录\n");
            return 1;
        }
        const std::vector<quiz::BankEntry>& banks = g_context.banks();
        if (bankIndex < 0 || bankIndex >= static_cast<int>(banks.size())) {
            std::printf("题库下标越界：%d（共 %d 个）\n", bankIndex, static_cast<int>(banks.size()));
            return 1;
        }
        const quiz::QuestionBank& bank = banks[static_cast<size_t>(bankIndex)].bank;
        const quiz::ExamSpec* exam =
            quiz::io::findExam(bank, paperId == "any" ? std::string() : paperId);
        if (exam == nullptr) {
            std::printf("题库「%s」里没有这套卷：%s\n", bank.meta.title.c_str(), paperId.c_str());
            return 1;
        }
        const quiz::io::ExamPaper paper = quiz::io::buildExamPaper(bank, *exam, exam->seed);
        std::printf("题库：%s ｜ 卷：%s（id = %s）\n", bank.meta.title.c_str(), paper.title.c_str(),
                    paper.examId.c_str());
        std::printf("限时 %d 分钟 ｜ 及格线 %d%% ｜ 满分 %d 分 ｜ 共 %d 题\n", paper.minutes,
                    paper.passPercent, paper.totalScore(), static_cast<int>(paper.items.size()));
        int no = 0;
        for (const quiz::io::ExamPaperItem& item : paper.items) {
            ++no;
            if (item.questionIndex < 0 ||
                item.questionIndex >= static_cast<int>(bank.questions.size())) {
                continue;
            }
            const quiz::Question& question = bank.questions[static_cast<size_t>(item.questionIndex)];
            std::string stem = question.stem;
            if (stem.size() > 34) {
                stem.resize(34);
                // 别把中文字符切成两半（UTF-8 续字节是 10xxxxxx）
                while (!stem.empty() && (static_cast<unsigned char>(stem.back()) & 0xC0) == 0x80) {
                    stem.pop_back();
                }
                stem += "…";
            }
            std::printf("  %2d. %-16s 难度 %d  %d 分  %s\n", no, question.id.c_str(),
                        question.difficulty, item.score, stem.c_str());
        }
        for (const quiz::io::Issue& issue : paper.issues) {
            std::printf("  ⚠ %s\n", issue.message.c_str());
        }
        return 0;
    }

    if (!validatePath.empty()) {
        // 命令行校验题库：写题库时非常有用（不需要打开界面）
        std::string text;
        if (!quiz::platform::readTextFile(validatePath, text)) {
            std::printf("读不到文件：%s\n", validatePath.c_str());
            return 1;
        }
        quiz::io::LoadResult result = quiz::io::loadBank(text, validatePath);
        std::printf("题库：%s（%d 题）\n", result.bank.meta.title.c_str(),
                    static_cast<int>(result.bank.questions.size()));
        if (result.issues.empty()) {
            std::printf("格式检查通过，没有发现问题。\n");
            return 0;
        }
        for (const auto& issue : result.issues) {
            if (issue.line > 0) {
                std::printf("  第 %d 行：%s\n", issue.line, issue.message.c_str());
            } else {
                std::printf("  %s\n", issue.message.c_str());
            }
        }
        return result.hasFatalError ? 2 : 0;
    }

    if (benchFrames > 0) {
        return runBench(width, height, benchFrames, screen);
    }

    if (!shotPath.empty()) {
        return runShot(shotPath, width, height, screen, bankIndex, examIndex, questionIndex,
                       answerIndex, frames, clicks, typeText, typeEnter, planOrder);
    }

    if (!pickTest.empty()) {
        return runPickTest(pickTest);
    }

    if (!importPaths.empty()) {
        // 命令行导入：不需要界面，脚本化 / 无头验证用
        if (!g_context.initialize()) {
            std::printf("没有找到题库目录\n");
            return 1;
        }
        std::vector<std::string> names;
        std::string error;
        const bool ok = g_context.importBankFiles(importPaths, names, error);
        for (const std::string& name : names) {
            std::printf("已导入：%s\n", name.c_str());
        }
        if (!ok) {
            std::printf("%s\n", error.c_str());
            return 2;
        }
        std::printf("导入后题库数：%d\n", static_cast<int>(g_context.banks().size()));
        return 0;
    }

    if (!exportKind.empty()) {
        // 命令行导出：便于脚本化（也可以用来验证导出结果是否符合固定格式）
        if (!g_context.initialize()) {
            std::printf("没有找到题库\n");
            return 1;
        }
        if (!g_context.openBank(bankIndex)) {
            std::printf("打不开题库索引 %d\n", bankIndex);
            return 1;
        }
        quiz::AppContext::ExportKind kind = quiz::AppContext::ExportKind::WholeBank;
        if (exportKind == "wrong") {
            kind = quiz::AppContext::ExportKind::WrongOnly;
        } else if (exportKind == "marked") {
            kind = quiz::AppContext::ExportKind::MarkedOnly;
        }
        std::string path;
        std::string error;
        if (!g_context.exportActive(kind, path, error)) {
            std::printf("导出失败：%s\n", error.c_str());
            return 1;
        }
        std::printf("已导出：%s\n", path.c_str());
        return 0;
    }
    // 窗口模式也支持 --size（默认 960x720）
    const int windowWidth = (width != 900 || height != 1200) ? width : kDefaultWidth;
    const int windowHeight = (width != 900 || height != 1200) ? height : kDefaultHeight;
    return runWindow(fpsEnabled, fpsInterval, benchWindowFrames, windowWidth, windowHeight);
}

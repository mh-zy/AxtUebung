#pragma once

#include <algorithm>
#include <cmath>

#include "imgui.h"
#include "imgui_internal.h"

// ============================================================================
// 滚动区手势：控件起手也能滚，松手后带惯性
// ----------------------------------------------------------------------------
// 关键点：按下时不能立刻裁决“控件赢”。手指按在按钮 / 输入框上时，ImGui 会先
// 把该 item 设成 ActiveId；如果之后明显纵向拖动，就要在控件处理这一帧之前取消
// ActiveId，把手势改成滚动。这样：
//   * 短按不动：按钮照常触发；
//   * 从按钮竖滑：按钮不触发，列表滚动；
//   * 在输入框横滑：保留选字；竖滑则滚动，输入框失焦（Android 软键盘随之收起）；
//   * 从原生滚动条起手：仍交给滚动条，不抢它的拖动。
//
// 实现分两段：
//   scrollGestureBeforeChildItems(): 在子窗口内容之前裁决方向并 ClearActiveID；
//   dragScrollInsideChild():        在内容之后按当前位置更新 ScrollY、启动惯性。
// ScrollDrag 构造 / 析构正好对应这两段；卡片由 cardBegin/cardEnd 手动调用。
//
// 惯性只在同一个目标 child 上应用，不会串到父列表或下一个页面。衰减公式：
//   v *= exp(-6 * dt)，|v| < 30px/s 或到顶 / 到底就停止。
// ============================================================================
namespace quiz::ui {

namespace scroll_detail {

enum class GesturePhase {
    Idle,
    Pending,
    Scroll,
    Control,
};

struct GestureState {
    GesturePhase phase = GesturePhase::Idle;
    ImGuiWindow* targetWindow = nullptr;
    ImVec2 downPos{};
    bool startedOnWidget = false;
    bool startedOnScrollbar = false;
    bool velocityKnown = false;
    float velocity = 0.0f;
    float idleSeconds = 0.0f;
    int clickFrame = -1;
    int releaseFrame = -1;
    int actionFrame = -1;

    bool inertiaActive = false;
    float inertiaVelocity = 0.0f;
    double inertiaStartTime = 0.0;
};

inline GestureState& gestureState() {
    static GestureState state;
    return state;
}

inline float frameDeltaSeconds() {
    return std::clamp(ImGui::GetIO().DeltaTime, 1.0f / 240.0f, 1.0f / 20.0f);
}

/// 滚动目标是否悬停：不能直接用 IsWindowHovered()。ImGui 在某个按钮拿到
/// ActiveId 后，IsWindowHovered() 默认会返回 false（避免滚轮等抢走控件输入），
/// 而这里恰恰要在“按钮已激活”的情况下识别用户从按钮上开始拖动。
inline bool scrollTargetIsHovered(ImGuiWindow* window) {
    if (window == nullptr || ImGui::GetCurrentContext() == nullptr) {
        return false;
    }
    ImGuiWindow* hovered = GImGui->HoveredWindow;
    return hovered != nullptr &&
           (hovered == window || ImGui::IsWindowChildOf(hovered, window, true));
}

inline bool pointerStartedOnScrollbar(ImGuiWindow* window) {
    if (window == nullptr) {
        return false;
    }
    const ImGuiID hovered = ImGui::GetHoveredID();
    if (hovered == 0) {
        return false;
    }
    return hovered == ImGui::GetWindowScrollbarID(window, ImGuiAxis_X) ||
           hovered == ImGui::GetWindowScrollbarID(window, ImGuiAxis_Y);
}

inline void stopInertia(GestureState& state) {
    state.inertiaActive = false;
    state.inertiaVelocity = 0.0f;
    state.targetWindow = nullptr;
}

inline void applyDragScroll(GestureState& state) {
    const ImGuiIO& io = ImGui::GetIO();
    const float dt = frameDeltaSeconds();
    const float dy = io.MouseDelta.y;
    const float before = ImGui::GetScrollY();
    const float maximum = ImGui::GetScrollMaxY();
    if (maximum > 0.0f) {
        ImGui::SetScrollY(std::clamp(before - dy, 0.0f, maximum));
    }

    // 用最近几十毫秒的移动估计松手速度。输入事件偶尔会合并移动，所以先限幅，
    // 再做一阶低通；第一帧直接取瞬时值，轻快甩动不会被 0 初值拖慢。
    if (std::fabs(dy) >= 0.05f) {
        const float instantaneous = std::clamp(-dy / dt, -4000.0f, 4000.0f);
        if (!state.velocityKnown) {
            state.velocity = instantaneous;
            state.velocityKnown = true;
        }
        else {
            const float alpha = 1.0f - std::exp(-dt / 0.06f);
            state.velocity += (instantaneous - state.velocity) * alpha;
        }
        state.idleSeconds = 0.0f;
    }
    else {
        state.idleSeconds += dt;
        if (state.idleSeconds > 0.08f) {
            state.velocityKnown = false;
            state.velocity = 0.0f;
        }
    }
}

inline void applyInertia(GestureState& state, ImGuiWindow* window, int frame) {
    if (!state.inertiaActive || state.targetWindow != window || state.actionFrame == frame) {
        return;
    }
    state.actionFrame = frame;
    const float dt = frameDeltaSeconds();
    const float before = ImGui::GetScrollY();
    const float maximum = ImGui::GetScrollMaxY();
    if (maximum <= 0.0f) {
        stopInertia(state);
        return;
    }

    const float next = std::clamp(before + state.inertiaVelocity * dt, 0.0f, maximum);
    ImGui::SetScrollY(next);
    state.inertiaVelocity *= std::exp(-6.0f * dt);

    if ((next <= 0.0f && state.inertiaVelocity < 0.0f) ||
        (next >= maximum && state.inertiaVelocity > 0.0f)) {
        stopInertia(state);
    }
    else if (std::fabs(state.inertiaVelocity) < 30.0f) {
        stopInertia(state);
    }
}

}  // namespace scroll_detail

/// 平台主循环据此在松手后继续保持呈现；否则惯性末段可能只在后台更新 ScrollY，
/// 屏幕却因为“最近 0.4 秒没有输入”停止重绘。
inline bool isScrollAnimationActive() {
    using namespace scroll_detail;
    GestureState& state = gestureState();
    if (!state.inertiaActive) {
        return false;
    }
    if (ImGui::IsMouseDown(0)) {
        stopInertia(state);
        return false;
    }
    if (ImGui::GetTime() - state.inertiaStartTime > 1.2) {
        stopInertia(state);
        return false;
    }
    return true;
}

/// 在子窗口内容之前执行：识别方向、决定是否取消控件 ActiveId。
/// 必须由 ScrollDrag 构造函数或 cardBegin() 调用。
inline void scrollGestureBeforeChildItems() {
    using namespace scroll_detail;
    GestureState& state = gestureState();
    const ImGuiIO& io = ImGui::GetIO();
    const int frame = ImGui::GetFrameCount();

    // 新的一次按下会终止旧的惯性；clickFrame 防止同一帧每个滚动区都重置一次。
    if (ImGui::IsMouseClicked(0) && state.clickFrame != frame) {
        if (state.inertiaActive) {
            stopInertia(state);
        }
        state = GestureState{};
        state.clickFrame = frame;
    }

    ImGuiWindow* const window = ImGui::GetCurrentWindow();
    if (state.targetWindow != window ||
        (!io.MouseDown[0] && !io.MouseReleased[0])) {
        return;
    }

    if (state.phase == GesturePhase::Pending) {
        const float dx = io.MousePos.x - state.downPos.x;
        const float dy = io.MousePos.y - state.downPos.y;
        const float absX = std::fabs(dx);
        const float absY = std::fabs(dy);

        if (state.startedOnScrollbar) {
            state.phase = GesturePhase::Control;
        }
        else if (absY >= 8.0f && absY > absX * 1.15f) {
            state.phase = GesturePhase::Scroll;
            if (state.startedOnWidget) {
                // 按钮 / 输入框已经在上一帧拿到 ActiveId；在它处理本次移动之前清掉，
                // 后续松手就不会触发按钮，也不会继续拖动输入框选区。
                ImGui::ClearActiveID();
            }
        }
        else if (state.startedOnWidget && absX >= 12.0f && absX > absY * 1.15f) {
            // 横向动作留给输入框选字、滑条、颜色控件等，一旦锁定直到松手。
            state.phase = GesturePhase::Control;
        }
    }
}

/// 在子窗口内容之后执行：记录本次按下的目标 child、应用拖动、启动 / 更新惯性。
inline void dragScrollInsideChild() {
    using namespace scroll_detail;
    GestureState& state = gestureState();
    const ImGuiIO& io = ImGui::GetIO();
    const int frame = ImGui::GetFrameCount();
    ImGuiWindow* const window = ImGui::GetCurrentWindow();

    // 按下那一帧在内容画完后才知道内层卡片能不能滚动。内层先析构、外层后析构，
    // 所以只接受第一个“确实有可滚动范围”的 child；内层不够长时会自动落到外层。

    if (ImGui::IsMouseClicked(0) && state.clickFrame == frame &&
        state.targetWindow == nullptr && scrollTargetIsHovered(window) &&
        ImGui::GetScrollMaxY() > 0.5f) {
        state.targetWindow = window;
        state.downPos = io.MouseClickedPos[0];
        // 触摸按下时 IsAnyItemHovered() 在部分后端 / 帧序下会短暂为空，但按钮
        // 自己的 ButtonBehavior 已经拿到 ActiveId。以 ActiveId 为主、hover 为兜底，
        // 才能可靠识别“从按钮 / 输入框起手”。
        state.startedOnWidget = ImGui::GetActiveID() != 0 || ImGui::IsAnyItemHovered();
        state.startedOnScrollbar = pointerStartedOnScrollbar(window);
        state.phase = state.startedOnScrollbar ? GesturePhase::Control : GesturePhase::Pending;
        state.velocityKnown = false;
        state.velocity = 0.0f;
        state.idleSeconds = 0.0f;
    }

    if (state.targetWindow != window) {
        return;
    }

    if (ImGui::IsMouseReleased(0) && state.releaseFrame != frame) {
        state.releaseFrame = frame;
        if (state.phase == GesturePhase::Scroll) {
            if (state.actionFrame != frame) {
                applyDragScroll(state);
                state.actionFrame = frame;
            }
            const float idleAtRelease = state.idleSeconds + frameDeltaSeconds();
            if (state.velocityKnown && idleAtRelease <= 0.08f &&
                std::fabs(state.velocity) >= 120.0f && ImGui::GetScrollMaxY() > 0.0f) {
                state.inertiaActive = true;
                state.inertiaVelocity = std::clamp(state.velocity, -3500.0f, 3500.0f);
                state.inertiaStartTime = ImGui::GetTime();
            }
        }
        state.phase = GesturePhase::Idle;
        if (!state.inertiaActive) {
            state.targetWindow = nullptr;
        }
        return;
    }

    if (io.MouseDown[0]) {
        if (state.phase == GesturePhase::Scroll && state.actionFrame != frame) {
            applyDragScroll(state);
            state.actionFrame = frame;
        }
        return;
    }

    applyInertia(state, window, frame);
}

/// 作用域守卫：构造时做方向裁决（仍在 child 内、内容之前），析构时应用滚动。
class ScrollDrag {
public:
    ScrollDrag() { scrollGestureBeforeChildItems(); }
    ~ScrollDrag() { dragScrollInsideChild(); }
    ScrollDrag(const ScrollDrag&) = delete;
    ScrollDrag& operator=(const ScrollDrag&) = delete;
};

}  // namespace quiz::ui
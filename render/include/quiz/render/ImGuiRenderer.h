#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "imgui.h"

// ============================================================================
// Dear ImGui 的「软件渲染后端」
// ----------------------------------------------------------------------------
// 为什么不用 OpenGL：
//   * 安卓端用 GLES 需要 EGL + 着色器代码，还会把 native 代码和 Java 层绑得更紧；
//   * 刷题类应用是「事件驱动」的：没有互动时根本不需要重绘，
//     软件渲染 + 按需重绘完全够用，而且两端共用同一份渲染代码。
// 这个后端负责三件事：
//   1. 把 ImGui 的输入事件喂给 ImGui（鼠标/触摸/键盘）；
//   2. 管理字体图集纹理（ImGui 1.92 的动态纹理接口）；
//   3. 把 ImGui 的三角形列表光栅化到 Framebuffer 上。
// ============================================================================
namespace quiz::render {

/// 一帧的输入事件类型（由平台层翻译成 ImGui 事件）
struct PointerEvent {
    float x = 0.0f;
    float y = 0.0f;
    bool down = false;
    bool isTouch = false;   // true=触摸，false=鼠标
    /// 后端内部使用的全局输入顺序。平台不需要填写；addPointerEvent() 会赋值。
    /// 它用来保证“先 DOWN、再 MOVE、最后 UP”不会被每帧只送一个边沿的节流打乱。
    uint64_t sequence = 0;
};

/// 初始化 ImGui（创建上下文、设置样式、准备字体）
/// @param framebufferWidth/Height 初始画面尺寸（用于 DisplaySize）
/// @param fontScale 界面缩放（按屏幕大小算出来的，例如 1.0 / 1.5 / 2.0）
bool initialize(int framebufferWidth, int framebufferHeight, float fontScale);

void shutdown();

/// 每帧开始：更新 DisplaySize、推进 ImGui 的一帧
void beginFrame(int framebufferWidth, int framebufferHeight, float deltaSeconds);

/// 每帧结束：把 ImGui 画到帧缓冲上；返回 true 表示这一帧确实绘制了内容
/// @param rasterize 传 false 表示「这一帧不改动画面」（空闲省电模式）：
///                  仍然会让 ImGui 完成这一帧，但跳过最耗时的光栅化
bool endFrameAndRender(uint32_t* framebufferPixels, int width, int height, bool rasterize);

/// 输入事件（平台层调用）
void addPointerEvent(const PointerEvent& event);
void addPointerMove(float x, float y, bool isTouch);
void addScroll(float deltaY);
void addKeyEvent(ImGuiKey key, bool down);
void addTextInput(unsigned int codepoint);

/// 把一段 UTF-8 文字交给 ImGui（输入法提交的中文就是走这里）
void addTextUtf8(const std::string& text);

/// 重新加载字体并设置界面缩放。
/// 说明：ImGui 1.92 的字体图集会在绘制时按需生成字形，
///       所以这里不需要预先枚举汉字，也不要手工调用 ImFontAtlas::Build()。
void rebuildFont(float fontScale);

/// 当前字体是否来自系统字体文件（false 表示退回了 ImGui 内置英文字体）
bool usingSystemFont();
std::string fontPath();

// ---------------------------------------------------------------------------
// 图标纹理（SVG → 纹理）
// ---------------------------------------------------------------------------
/// 光栅化一张 SVG 图标并注册成纹理，返回可直接给 ImGui::Image / ImGui::ImageButton
/// 用的 ImTextureID；失败返回 0（并写 outError）。
/// @param assetName assets 下的相对路径，例如 "icons/download.svg"
/// @param sizePx    光栅化边长（像素）。缩放由采样器负责，建议按「显示尺寸 × 2」给，
///                  例如界面缩放 1.2、图标显示 24px → 传 64~96
/// @param colorRgb  0xRRGGBB，对应 SVG 里的 currentColor
/// @param inkFraction 0 = 原样；>0 时按「墨迹」归一化（最长边占 sizePx * inkFraction 并居中），
///                    按钮里的图标要传这个，不然不同留白的图标大小能差一倍
/// 说明：按 assetName+size+color+inkFraction 缓存，同一张图只光栅化一次；换主题色会自动生成新纹理。
ImTextureID loadIconTexture(const std::string& assetName, int sizePx, unsigned int colorRgb,
                            std::string* outError = nullptr, float inkFraction = 0.0f);

/// 直接从 SVG 文本生成（不走缓存）；inkFraction 含义同上
ImTextureID createIconTextureFromSvg(const std::string& svgText, int sizePx, unsigned int colorRgb,
                                     std::string* outError = nullptr, float inkFraction = 0.0f);

/// 清掉图标缓存的索引（纹理本身留在后端里，进程退出时一起释放）
void clearIconCache();

}  // namespace quiz::render

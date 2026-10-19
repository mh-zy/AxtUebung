#include "quiz/ui/UiApp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "imgui.h"
#include "quiz/platform/Platform.h"
#include "quiz/render/ImGuiRenderer.h"
#include "quiz/ui/UiScroll.h"
#include "quiz/util/Log.h"

namespace quiz::ui {

namespace {

/// applyTheme() 每帧把设置里的颜色写进来：自由函数（iconButton / primaryButton）也能取到
ImU32 g_iconRgb = 0;
ImU32 g_buttonTextRgb = 0;

/// 0xRRGGBB → ImU32（不透明）
ImU32 toImU32(unsigned int rgb) {
    return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255);
}

/// 派生色的统一做法：先看底色是亮还是暗，再决定往哪个方向推。
/// 为什么不能一味“调亮”：亮色主题（白底）本来就接近 255，再乘系数会直接饱和成白色，
/// 于是 hover 的按钮、选中的选项都糊在背景里看不见（这个坑真踩过）。
///   strength > 0：强调（亮底 → 调暗，暗底 → 调亮）：hover、按下、选中
///   strength < 0：弱化（亮底 → 调亮，暗底 → 调暗）：置灰、失效的文字
ImU32 shadeColor(ImU32 color, float strength) {
    const int r = (color >> IM_COL32_R_SHIFT) & 0xFF;
    const int g = (color >> IM_COL32_G_SHIFT) & 0xFF;
    const int b = (color >> IM_COL32_B_SHIFT) & 0xFF;
    const int a = (color >> IM_COL32_A_SHIFT) & 0xFF;
    // 感知亮度：0 = 全黑，1 = 全白
    const float luminance = (0.299f * static_cast<float>(r) + 0.587f * static_cast<float>(g) +
                             0.114f * static_cast<float>(b)) / 255.0f;
    const bool light = luminance > 0.5f;
    const float amount = strength >= 0.0f ? (light ? 1.0f - 0.45f * strength : 1.0f + 0.90f * strength)
                                          : (light ? 1.0f - 0.90f * strength : 1.0f + 0.45f * strength);
    const int outR = std::clamp(static_cast<int>(static_cast<float>(r) * amount), 0, 255);
    const int outG = std::clamp(static_cast<int>(static_cast<float>(g) * amount), 0, 255);
    const int outB = std::clamp(static_cast<int>(static_cast<float>(b) * amount), 0, 255);
    return IM_COL32(outR, outG, outB, a);
}

// ---------------------------------------------------------------------------
// 设计系统：一套共用的尺寸与组件。页面里尽量只调用这些，别各画各的
// ---------------------------------------------------------------------------

/// 触摸友好的控件高度（手指点得住的最小高度）
float controlHeight() { return std::max(46.0f, ImGui::GetFontSize() * 2.6f); }
/// 卡片内边距
float cardPadding() { return std::max(12.0f, ImGui::GetFontSize() * 0.85f); }
/// 统一圆角
float cornerRadius() { return 12.0f; }

/// 亮底配深色字、暗底配浅色字：强调色按钮上的文字用这个，任何主题都看得清
ImU32 contrastText(ImU32 background) {
    const int r = (background >> IM_COL32_R_SHIFT) & 0xFF;
    const int g = (background >> IM_COL32_G_SHIFT) & 0xFF;
    const int b = (background >> IM_COL32_B_SHIFT) & 0xFF;
    const float luminance = (0.299f * static_cast<float>(r) + 0.587f * static_cast<float>(g) +
                             0.114f * static_cast<float>(b)) / 255.0f;
    return luminance > 0.55f ? IM_COL32(22, 24, 28, 255) : IM_COL32(246, 249, 247, 255);
}

/// 标签颜色：标签名哈希值 % 0x1000000 → 同一个标签到哪儿都是同一种颜色
ImU32 tagColorOf(const std::string& tag) {
    unsigned int hash = 2166136261u;   // FNV-1a
    for (unsigned char ch : tag) {
        hash ^= static_cast<unsigned int>(ch);
        hash *= 16777619u;
    }
    return IM_COL32((hash >> 16) & 0xFFu, (hash >> 8) & 0xFFu, hash & 0xFFu, 255);
}

/// 「面」的颜色：比卡片底色稍深（亮主题）/ 稍亮（暗主题），用来画选项行、列表项
ImU32 surfaceColor(float strength) {
    return shadeColor(ImGui::GetColorU32(ImGuiCol_ChildBg), strength);
}

/// 一段文字按给定宽度换行后需要多高
float wrappedTextHeight(const std::string& text, float wrapWidth) {
    if (text.empty()) {
        return ImGui::GetTextLineHeight();
    }
    return ImGui::CalcTextSize(text.c_str(), nullptr, false, wrapWidth).y;
}

/// 圆角卡片：高度由调用方算好传进来（比自动高度可预期）
bool cardBegin(const char* id, float height) {
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, cornerRadius());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(cardPadding(), cardPadding()));
    const bool visible = ImGui::BeginChild(id, ImVec2(0.0f, height), ImGuiChildFlags_Borders);
    if (visible) {
        // 卡片内容之前先做手势裁决：卡片里的按钮 / 输入框纵向拖动时，
        // 要在控件处理本次移动前取消 ActiveId。
        scrollGestureBeforeChildItems();
    }
    return visible;
}

void cardEnd() {
    // 卡片自己也是 BeginChild：材料正文、长解析超过卡片高度时，在卡片内部拖动即可滚动。
    // 调用必须放在 EndChild() 之前——此时 SetScrollY() 才会作用在卡片窗口上。
    dragScrollInsideChild();
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
}

/// 主按钮：强调色实心 + 对比色文字。一页只放一个，用来突出「最想让你点的那个」。
/// enabled = false 时换成中性底色 —— 强调色就算被 ImGui 压暗也还是很抢眼，
/// 看起来像能点，容易误导。
bool primaryButton(const char* label, const ImVec2& size, unsigned int accentRgb,
                   bool enabled = true) {
    const ImU32 accent = toImU32(accentRgb);
    const ImU32 fill = enabled ? accent : surfaceColor(0.22f);
    const ImU32 labelColor =
        enabled ? (g_buttonTextRgb != 0 ? g_buttonTextRgb : contrastText(accent))
                : ImGui::GetColorU32(ImGuiCol_TextDisabled);
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(fill));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImGui::ColorConvertU32ToFloat4(shadeColor(fill, 0.35f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImGui::ColorConvertU32ToFloat4(shadeColor(fill, 0.60f)));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(labelColor));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, cornerRadius() * 0.8f);
    const bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    return clicked;
}

/// 小标签：题型、已答/未答、页数这类短信息
void chip(const std::string& text, ImU32 background, ImU32 textColor) {
    const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
    const float padX = ImGui::GetFontSize() * 0.6f;
    const float padY = ImGui::GetFontSize() * 0.22f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size(textSize.x + padX * 2.0f, textSize.y + padY * 2.0f);
    ImDrawList* const draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), background,
                        size.y * 0.5f);
    draw->AddText(ImVec2(origin.x + padX, origin.y + padY), textColor, text.c_str());
    ImGui::Dummy(size);
}

/// 标签条：题库自带的标签 + 用户自己加的标签。一行放不下就换行，
/// 底色由标签名哈希得来，文字取对比色 —— 配什么主题都看得清。
void tagChips(const std::vector<std::string>& tags) {
    const float limit = ImGui::GetContentRegionMax().x;
    float lineWidth = 0.0f;
    for (const std::string& tag : tags) {
        const float width = ImGui::CalcTextSize(tag.c_str()).x + ImGui::GetFontSize() * 1.2f;
        if (lineWidth > 0.0f) {
            if (lineWidth + ImGui::GetStyle().ItemSpacing.x + width > limit) {
                lineWidth = 0.0f;   // 这一行放不下了，另起一行
            } else {
                ImGui::SameLine();
                lineWidth += ImGui::GetStyle().ItemSpacing.x;
            }
        }
        const ImU32 background = tagColorOf(tag);
        chip(tag, background, contrastText(background));
        lineWidth += width;
    }
}

/// 用线条画对勾 —— 不依赖字体里有没有 ✓（不同设备字体不一定齐）
void drawCheckMark(ImDrawList* draw, ImVec2 center, float size, ImU32 color) {
    const ImVec2 a(center.x - size * 0.45f, center.y + size * 0.02f);
    const ImVec2 b(center.x - size * 0.10f, center.y + size * 0.34f);
    const ImVec2 c(center.x + size * 0.45f, center.y - size * 0.34f);
    draw->AddLine(a, b, color, 2.6f);
    draw->AddLine(b, c, color, 2.6f);
}

/// 用线条画叉
void drawCrossMark(ImDrawList* draw, ImVec2 center, float size, ImU32 color) {
    const float h = size * 0.32f;
    draw->AddLine(ImVec2(center.x - h, center.y - h), ImVec2(center.x + h, center.y + h), color, 2.6f);
    draw->AddLine(ImVec2(center.x + h, center.y - h), ImVec2(center.x - h, center.y + h), color, 2.6f);
}

/// 选项行看起来是什么状态
enum class RowState { Normal, Selected, Correct, Wrong, Muted };

/// 一行选项：左边字母徽章 + 自动换行的选项文字 + 右边的对/错标记。
/// 返回 true 表示这一行被点了（interactive = false 时永远返回 false）。
bool optionRow(const char* id, const std::string& letter, const std::string& text, RowState state,
               bool interactive, ImU32 accent, ImU32 correct, ImU32 wrong) {
    const float width = ImGui::GetContentRegionAvail().x;
    const float textWrap = std::max(width - 110.0f, 60.0f);
    const float height =
        std::max(controlHeight(), wrappedTextHeight(text, textWrap) + ImGui::GetFontSize() * 1.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 far(origin.x + width, origin.y + height);
    ImDrawList* const draw = ImGui::GetWindowDrawList();

    ImU32 fill = surfaceColor(0.16f);
    ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
    bool filled = false;   // 强调/对/错：整个行底用实色，文字用对比色
    switch (state) {
        case RowState::Selected: fill = accent; filled = true; break;
        case RowState::Correct: fill = correct; filled = true; break;
        case RowState::Wrong: fill = wrong; filled = true; break;
        case RowState::Muted: fill = surfaceColor(-0.28f); break;
        case RowState::Normal: break;
    }
    if (filled) {
        textColor = contrastText(fill);
    } else if (state == RowState::Muted) {
        textColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    }

    draw->AddRectFilled(origin, far, fill, cornerRadius() * 0.85f);
    if (state == RowState::Selected) {
        draw->AddRect(origin, far, textColor, cornerRadius() * 0.85f, 0, 2.0f);
    }

    // 字母徽章：实色行上做成「反色小胶囊」（徽章底 = 文字色，字母 = 行底色），
    // 这样不管主题是亮是暗、强调色是什么，字母都看得清
    const float badge = ImGui::GetFontSize() * 1.75f;
    const float badgeX = origin.x + cardPadding() * 0.8f;
    const float badgeY = origin.y + (height - badge) * 0.5f;
    const ImVec2 badgeMin(badgeX, badgeY);
    const ImVec2 badgeMax(badgeX + badge, badgeY + badge);
    const ImU32 badgeFill = filled ? textColor : surfaceColor(0.45f);
    const ImU32 badgeLetter = filled ? fill : ImGui::GetColorU32(ImGuiCol_Text);
    draw->AddRectFilled(badgeMin, badgeMax, badgeFill, badge * 0.32f);
    const ImVec2 letterSize = ImGui::CalcTextSize(letter.c_str());
    draw->AddText(ImVec2(badgeMin.x + (badge - letterSize.x) * 0.5f,
                         badgeMin.y + (badge - letterSize.y) * 0.5f),
                  badgeLetter, letter.c_str());

    // 选项文字（自动换行，整体垂直居中）
    const float textX = badgeMax.x + ImGui::GetFontSize() * 0.7f;
    const float textY = origin.y + (height - wrappedTextHeight(text, textWrap)) * 0.5f;
    draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(textX, textY), textColor,
                  text.c_str(), nullptr, textWrap);

    // 结果标记
    if (state == RowState::Correct || state == RowState::Wrong) {
        const ImVec2 center(far.x - ImGui::GetFontSize() * 1.2f, origin.y + height * 0.5f);
        if (state == RowState::Correct) {
            drawCheckMark(draw, center, ImGui::GetFontSize() * 1.1f, textColor);
        } else {
            drawCrossMark(draw, center, ImGui::GetFontSize() * 1.1f, textColor);
        }
    }

    const bool pressed = ImGui::InvisibleButton(id, ImVec2(width, height));
    return interactive && pressed;
}

/// 秒 → 「x 小时 y 分」/「y 分 z 秒」/「z 秒」
std::string formatDuration(double seconds, const i18n::I18n& texts) {
    const int total = static_cast<int>(seconds + 0.5);
    const int hours = total / 3600;
    const int minutes = (total % 3600) / 60;
    const int secs = total % 60;
    if (hours > 0) {
        return texts.format("ui.stats.duration_hm",
                            {{"hours", std::to_string(hours)}, {"minutes", std::to_string(minutes)}});
    }
    if (minutes > 0) {
        return texts.format("ui.stats.duration_ms",
                            {{"minutes", std::to_string(minutes)}, {"seconds", std::to_string(secs)}});
    }
    return texts.format("ui.stats.duration_s", {{"seconds", std::to_string(secs)}});
}

/// 倒计时用的「分:秒」（超过一小时就是「时:分:秒」）
std::string clockText(int seconds) {
    char buffer[32];
    if (seconds < 0) {
        seconds = 0;
    }
    const int hours = seconds / 3600;
    const int minutes = (seconds % 3600) / 60;
    const int secs = seconds % 60;
    if (hours > 0) {
        std::snprintf(buffer, sizeof(buffer), "%d:%02d:%02d", hours, minutes, secs);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%d:%02d", minutes, secs);
    }
    return std::string(buffer);
}

/// 把一段文字截到指定宽度内（按 UTF-8 边界，不切半个汉字），超长时补「…」。
/// Selectable 不会自动换行，硬截会把字切一半，所以长题干先用它收一下尾。
std::string ellipsizeToWidth(const std::string& text, float maxWidth) {
    if (maxWidth <= 0.0f || ImGui::CalcTextSize(text.c_str()).x <= maxWidth) {
        return text;
    }
    std::string out;
    for (size_t i = 0; i < text.size();) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        size_t length = 1;
        if ((ch & 0xE0u) == 0xC0u) {
            length = 2;
        } else if ((ch & 0xF0u) == 0xE0u) {
            length = 3;
        } else if ((ch & 0xF8u) == 0xF0u) {
            length = 4;
        }
        if (i + length > text.size()) {
            break;
        }
        const std::string next = out + text.substr(i, length);
        if (ImGui::CalcTextSize((next + "…").c_str()).x > maxWidth) {
            break;
        }
        out = next;
        i += length;
    }
    return out.empty() ? std::string("…") : out + "…";
}

/// 分数转文字：12.0 分就写 12（只有部分分才留一位小数）
std::string trimScoreText(float value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.1f", static_cast<double>(value));
    std::string text = buffer;
    while (!text.empty() && (text.back() == '0' || text.back() == '.')) {
        text.pop_back();
    }
    return text.empty() ? std::string("0") : text;
}

/// 题库是否可以用（解析成功且有题目）
bool bankUsable(const BankEntry& entry) {
    return entry.ok && !entry.bank.questions.empty();
}

/// 取某个选项在「提交后」应该用的颜色
/// 图标按钮（触屏友好）：透明底 + 无边距，触摸目标不小于 44px；
/// 图标颜色取当前主题的文字色；读不到图标资源时退回文字按钮。返回 true 表示被点击。
/// 图标按钮的边长：随字号缩放，并保证不小于 44px（手指友好的触摸目标）
float iconButtonSide() {
    return std::max(44.0f, ImGui::GetFontSize() * 2.0f);
}

/// 按钮里图标的「墨迹」占按钮边长的比例（归一化后最长边的大小）。
/// 光栅化时按这个比例缩放居中，不同留白的图标才会看上去一样大、也不会顶到按钮边上。
constexpr float kIconInkFraction = 0.56f;

// ⚠ rgbOverride 是 **0xRRGGBB**（不是 ImU32！ImU32 用 rgbOf() 转一下再传）
bool iconButton(const char* id, const char* svg, const std::string& fallbackLabel,
                unsigned int rgbOverride = 0) {
    const ImVec4 textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    unsigned int rgb =
        (static_cast<unsigned int>(textColor.x * 255.0f + 0.5f) << 16) |
        (static_cast<unsigned int>(textColor.y * 255.0f + 0.5f) << 8) |
        static_cast<unsigned int>(textColor.z * 255.0f + 0.5f);
    if (g_iconRgb != 0) {
        // 图标默认色（设置 → 主题配色 → 控件）；这里不调 rgbOf，因为它在下面才定义
        rgb = (((g_iconRgb >> IM_COL32_R_SHIFT) & 0xFFu) << 16) |
              (((g_iconRgb >> IM_COL32_G_SHIFT) & 0xFFu) << 8) |
              ((g_iconRgb >> IM_COL32_B_SHIFT) & 0xFFu);
    }
    if (rgbOverride != 0) {
        rgb = rgbOverride;
    }
    const float side = iconButtonSide();

    std::string error;
    const ImTextureID icon = render::loadIconTexture(svg, 192, rgb, &error, kIconInkFraction);
    if (icon == 0) {
        return ImGui::Button(fallbackLabel.c_str(), ImVec2(180.0f, 0.0f));
    }
    // 图标按钮平时是透明的：悬停/按下时垫一层「比背景稍深（亮色主题）/稍亮（暗色主题）」的底
    const ImU32 background = ImGui::GetColorU32(ImGuiCol_WindowBg);
    ImVec4 hoverTint = ImGui::ColorConvertU32ToFloat4(shadeColor(background, 0.20f));
    ImVec4 activeTint = ImGui::ColorConvertU32ToFloat4(shadeColor(background, 0.40f));
    hoverTint.w = 1.0f;
    activeTint.w = 1.0f;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoverTint);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, activeTint);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    const bool clicked = ImGui::ImageButton(id, icon, ImVec2(side, side));
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    return clicked;
}

/// 刷题模式按钮：图标按钮 + 下面的短说明文字。
/// 手机上不能指望「悬停提示」，所以「现在选的是哪个模式」要直接画出来：
/// 强调色淡底 + 描边 + 右上角对勾角标 + 文字换成强调色。
bool modeButton(const char* id, const char* svg, const std::string& label, bool selected,
                unsigned int accentRgb) {
    const float side = iconButtonSide();
    const ImU32 accent = toImU32(accentRgb);
    ImDrawList* const draw = ImGui::GetWindowDrawList();
    const ImVec2 groupPos = ImGui::GetCursorPos();
    const ImVec2 iconAt = ImGui::GetCursorScreenPos();
    if (selected) {
        const ImU32 tint = (accent & 0x00FFFFFFu) | (96u << IM_COL32_A_SHIFT);
        draw->AddRectFilled(iconAt, ImVec2(iconAt.x + side, iconAt.y + side), tint, cornerRadius());
        draw->AddRect(iconAt, ImVec2(iconAt.x + side, iconAt.y + side), accent, cornerRadius(), 0,
                      2.0f);
    }
    const bool clicked = iconButton(id, svg, label);
    if (selected) {
        const ImVec2 badge(iconAt.x + side * 0.78f, iconAt.y + side * 0.22f);
        const float radius = ImGui::GetFontSize() * 0.42f;
        draw->AddCircleFilled(badge, radius, accent);
        drawCheckMark(draw, badge, radius * 1.5f, contrastText(accent));
    }
    const float captionWidth = ImGui::CalcTextSize(label.c_str()).x;
    ImGui::SetCursorPos(ImVec2(groupPos.x + (side - captionWidth) * 0.5f, groupPos.y + side + 2.0f));
    ImGui::TextColored(
        ImGui::ColorConvertU32ToFloat4(selected ? accent : ImGui::GetColorU32(ImGuiCol_TextDisabled)),
        "%s", label.c_str());
    return clicked;
}

/// 题库卡片：**整张卡都是点击区域**（以前只有标题那一行能点，框里空白处点了没反应）。
/// 内容全部自己画，所以高度、内边距都可控，也不会在卡片里冒出滚动条。
/// 题库卡片被点了哪一块
enum class BankCardAction { None, Open, Clear, WrongBook, Delete };

/// 卡片右侧图标用的样式（图标 + 提示文案）
struct BankCardIcons {
    const char* wrongSvg = "icons/file-x.svg";
    const char* wrongLabel = "错题本";
    const char* clearSvg = "icons/eraser.svg";
    const char* clearLabel = "清除数据";
    const char* deleteSvg = "icons/trash-x.svg";
    const char* deleteLabel = "删除题库";
};

/// ImU32 → 0xRRGGBB（iconButton 的颜色参数要这个格式）
unsigned int rgbOf(ImU32 color) {
    return (((color >> IM_COL32_R_SHIFT) & 0xFFu) << 16) |
           (((color >> IM_COL32_G_SHIFT) & 0xFFu) << 8) |
           ((color >> IM_COL32_B_SHIFT) & 0xFFu);
}

BankCardAction bankCard(const char* id, const std::string& title, const std::string& fileLine,
                        const std::string& progressLine, float progressRatio, bool hasProgress,
                        const std::string& warnText, bool selected, bool clearArmed,
                        bool deleteArmed, bool hasWrong, ImU32 accent, ImU32 warnColor,
                        const BankCardIcons& icons) {
    const float width = ImGui::GetContentRegionAvail().x;
    const float pad = cardPadding();
    const float titleH = ImGui::GetTextLineHeight();
    const float subH = ImGui::GetTextLineHeight() * 0.96f;
    const float barH = 8.0f;
    // 右侧留三个图标（错题本 + 清除数据 + 删除题库）的位置
    const float iconSide = iconButtonSide();
    const float iconGap = 1.0f;
    const float iconsWidth = iconSide * 2.0f + iconGap * 1.5f;
    const float textRight = width - pad - iconsWidth - 8.0f;

    float height = pad + titleH + 6.0f + subH;
    if (hasProgress) {
        height += 10.0f + barH + 6.0f + subH;
    }
    height += pad;

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 far(origin.x + width, origin.y + height);
    ImDrawList* const draw = ImGui::GetWindowDrawList();
    const ImU32 muted = ImGui::GetColorU32(ImGuiCol_TextDisabled);

    draw->AddRectFilled(origin, far,
                        selected ? shadeColor(ImGui::GetColorU32(ImGuiCol_ChildBg), 0.22f)
                                 : ImGui::GetColorU32(ImGuiCol_ChildBg),
                        cornerRadius());
    draw->AddRect(origin, far,
                  selected ? accent : ImGui::GetColorU32(ImGuiCol_Border), cornerRadius(), 0,
                  selected ? 2.0f : 1.0f);

    draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(origin.x + pad, origin.y + pad),
                  selected ? accent : ImGui::GetColorU32(ImGuiCol_Text), title.c_str());
    if (!warnText.empty()) {
        const float warnW = ImGui::CalcTextSize(warnText.c_str()).x;
        draw->AddText(ImVec2(textRight - warnW, origin.y + pad), warnColor, warnText.c_str());
    }

    float y = origin.y + pad + titleH + 6.0f;
    draw->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.96f, ImVec2(origin.x + pad, y), muted,
                  fileLine.c_str());
    y += subH;
    if (hasProgress) {
        y += 10.0f;
        const float barX = origin.x + pad;
        const float barW = width - pad * 2.0f;
        draw->AddRectFilled(ImVec2(barX, y), ImVec2(barX + barW, y + barH), surfaceColor(0.30f),
                            barH * 0.5f);
        const float filled = std::clamp(progressRatio, 0.0f, 1.0f) * barW;
        if (filled > 0.0f) {
            draw->AddRectFilled(ImVec2(barX, y), ImVec2(barX + filled, y + barH), accent,
                                barH * 0.5f);
        }
        y += barH + 6.0f;
        draw->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.96f, ImVec2(origin.x + pad, y),
                      muted, progressLine.c_str());
    }

    // ① 右侧三个图标**必须先建**：ImGui 里先建的那个按钮才会先抢到 ActiveId，
    //    后建的 ItemHoverable 直接返回 false —— 反过来就会被整卡按钮吃掉。
    //    从右往左排：删除题库在最右（最危险的那个放边上），然后清除数据、错题本。
    const float iconsTop = origin.y + (height - iconSide) * 0.2f;
    const ImVec2 deletePos(origin.x + width - pad - iconSide, iconsTop);
    const ImVec2 clearPos(deletePos.x - iconGap - iconSide, iconsTop);
    const ImVec2 wrongPos(clearPos.x - iconGap - iconSide, iconsTop);
    ImGui::SetCursorScreenPos(wrongPos);
    // 没有错题时把「错题本」压暗（还能点，点了会给提示）
    const unsigned int wrongRgb =
        hasWrong ? 0u : rgbOf(ImGui::GetColorU32(ImGuiCol_TextDisabled));
    const bool wrongPressed =
        iconButton("##bank_wrong", icons.wrongSvg, icons.wrongLabel, wrongRgb);
    ImGui::SetCursorScreenPos(clearPos);
    // warnColor 是 ImU32，图标要 0xRRGGBB —— 不转的话红色会被画成蓝色
    const bool clearPressed = iconButton("##bank_clear", icons.clearSvg, icons.clearLabel,
                                         clearArmed ? rgbOf(warnColor) : 0u);
    ImGui::SetCursorScreenPos(deletePos);
    const bool deletePressed = iconButton("##bank_delete", icons.deleteSvg, icons.deleteLabel,
                                          deleteArmed ? rgbOf(warnColor) : 0u);

    // ② 再建整张卡片的点击区（显式把光标放回卡片左上角）
    ImGui::SetCursorScreenPos(origin);
    const bool cardPressed = ImGui::InvisibleButton(id, ImVec2(width, height));
    if (ImGui::IsItemActive()) {
        // 按住时压一层淡淡的底色：手机上也知道点到了
        draw->AddRectFilled(origin, far, IM_COL32(255, 255, 255, 20), cornerRadius());
    }

    if (deletePressed) {
        return BankCardAction::Delete;
    }
    if (wrongPressed) {
        return BankCardAction::WrongBook;
    }
    if (clearPressed) {
        return BankCardAction::Clear;
    }
    return cardPressed ? BankCardAction::Open : BankCardAction::None;
}

ImU32 optionColorAfterSubmit(const Question& question, int optionIndex, bool selected,
                             ImU32 correctColor, ImU32 wrongColor) {
    const bool isCorrectOption =
        std::find(question.answer.begin(), question.answer.end(), optionIndex) != question.answer.end();
    if (isCorrectOption) {
        return correctColor;
    }
    if (selected) {
        return wrongColor;   // 选错了
    }
    return shadeColor(ImGui::GetColorU32(ImGuiCol_Button), -0.80f);   // 没选的选项弱化：亮色主题里淡到接近背景，暗色主题里压暗
}

/// ImGui 的 InputText 需要一块 char 缓冲；这里用 imgui_stdlib 的做法，
/// 直接把 std::string 当缓冲用（回调里 resize），这样每道小题都能有自己的输入。
int inputTextResizeCallback(ImGuiInputTextCallbackData* data) {
    std::string* text = static_cast<std::string*>(data->UserData);
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        text->resize(static_cast<size_t>(data->BufTextLen));
        data->Buf = const_cast<char*>(text->c_str());
    } else if (data->EventFlag == ImGuiInputTextFlags_CallbackEdit && data->BufTextLen >= 0) {
        // 每改一个字就同步回 std::string。以前只在「缓冲区要扩容」时才同步，于是输入框里
        // 打出来的字要等回车才算数（提交按钮一直是灰的、作答状态不更新，手感发滞）。
        // 这里长度不会超过当前容量，assign 不会换 buffer，data->Buf 仍然有效。
        text->assign(data->Buf, static_cast<size_t>(data->BufTextLen));
    }
    return 0;
}

/// 编辑一个 std::string；outEntered 为 true 表示用户按了回车。
/// 空提示和「聚焦边框」都是自己画的，原因有两条：
///   1) ImGui 的 hint 只看“文本是否为空”，不看有没有获得焦点 —— 光标进去以后提示还杵在那儿；
///   2) 这个版本的 ImGui 给 InputText 的底色**永远用 ImGuiCol_FrameBg**（FrameBgActive 不生效），
///      亮色主题下光靠底色看不出选中了哪个框。
/// 所以：获得焦点 → 画一圈强调色边框、不显示空提示；没焦点且为空 → 显示灰色的空提示。
void inputTextString(const char* id, const char* hint, std::string& text, bool disabled,
                     ImU32 focusColor, bool* outEntered = nullptr) {
    ImGui::BeginDisabled(disabled);
    const bool entered = ImGui::InputText(
        id, const_cast<char*>(text.c_str()), text.capacity() + 1,
        ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_CallbackEdit |
            ImGuiInputTextFlags_EnterReturnsTrue,
        inputTextResizeCallback, &text);
    const bool active = ImGui::IsItemActive();
    ImGui::EndDisabled();
    if (!disabled) {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        ImDrawList* const draw = ImGui::GetWindowDrawList();
        if (active) {
            draw->AddRect(min, max, focusColor, ImGui::GetStyle().FrameRounding, 0, 2.0f);
        } else if (text.empty() && hint != nullptr && hint[0] != '\0') {
            const float padding = ImGui::GetStyle().FramePadding.x + 2.0f;
            const ImVec2 pos(min.x + padding, (min.y + max.y) * 0.5f - ImGui::GetFontSize() * 0.5f);
            draw->AddText(pos, ImGui::GetColorU32(ImGuiCol_TextDisabled), hint);
        }
    }
    if (outEntered != nullptr) {
        *outEntered = entered;
    }
}

/// 题型的中文/本地化名字
const char* typeLabelOf(const i18n::I18n& texts, QuestionType type) {
    switch (type) {
        case QuestionType::Multiple: return texts.text("ui.quiz.type_multiple");
        case QuestionType::Judge: return texts.text("ui.quiz.type_judge");
        case QuestionType::Fill: return texts.text("ui.quiz.type_fill");
        case QuestionType::Indeterminate: return texts.text("ui.quiz.type_indeterminate");
        case QuestionType::Ordered: return texts.text("ui.quiz.type_ordered");
        case QuestionType::Material: return texts.text("ui.quiz.type_material");
        case QuestionType::Single: break;
    }
    return texts.text("ui.quiz.type_single");
}

/// 正确答案的显示文本：判断题 → 正确/错误；填空题 → 答案文字；有序题 → C,A,D
std::string answerTextOf(const Question& question, const i18n::I18n& texts) {
    if (question.type == QuestionType::Judge) {
        return (question.answer.empty() || question.answer.front() != 0)
                   ? texts.text("ui.quiz.judge_false")
                   : texts.text("ui.quiz.judge_true");
    }
    if (question.type == QuestionType::Fill) {
        std::string out;
        for (size_t b = 0; b < question.fillAnswers.size(); ++b) {
            if (b > 0) {
                out += " ; ";
            }
            for (size_t k = 0; k < question.fillAnswers[b].size(); ++k) {
                if (k > 0) {
                    out += " / ";
                }
                out += question.fillAnswers[b][k];
            }
        }
        return out;
    }
    std::string out;
    for (size_t k = 0; k < question.answer.size(); ++k) {
        if (k > 0) {
            out += question.type == QuestionType::Ordered ? "," : ", ";
        }
        out += indexToLetter(question.answer[k]);
    }
    return out;
}


}  // namespace

UiApp::UiApp(AppContext& context) : context_(context), settingsScreen_(context) {
    // 设置页头部也要画「和首页同一套」的图标按钮（这次是「返回主页」用 home.svg）。
    // 图标按钮的实现依赖主题色与设计系统尺寸，都在这份文件里，所以把画法注入给设置页，
    // 而不是在那边复制一份（复制迟早会走样）。
    settingsScreen_.setIconButton([this](const char* id, const char* svg, const std::string& label) {
        return iconButton(id, svg, label);
    });
}

void UiApp::draw(float deltaSeconds) {
    frameDelta_ = deltaSeconds;
    pollPendingDialog();   // 上一帧发起的系统对话框（导入 / 导出）有结果了就在这里收尾
    applyTheme();   // 每帧套用设置里的配色（改完立刻生效）
    // 计时口径：只有刷题页面算「做题时长」（首页 / 设置 / 统计页都不算）
    context_.setQuizPageActive(screen_ == Screen::Quiz);

    // 启动后第一次：把窗口调成设置里的比例（Android 不支持改窗口，只留边）
    if (!aspectApplied_) {
        aspectApplied_ = true;
        int aspectWidth = 0;
        int aspectHeight = 0;
        if (context_.settings().uiAspectRatio(aspectWidth, aspectHeight)) {
            for (const auto& preset : platform::aspectPresets()) {
                if (preset.code == context_.settings().uiAspect) {
                    platform::setWindowClientSize(preset.width, preset.height);
                    break;
                }
            }
        }
    }

    float contentX = 0.0f;
    float contentY = 0.0f;
    float contentWidth = 0.0f;
    float contentHeight = 0.0f;
    computeContentRect(contentX, contentY, contentWidth, contentHeight);
    const ImVec2 contentPos(contentX, contentY);
    const ImVec2 contentSize(contentWidth, contentHeight);

    // 选了固定比例：窗口装不满的地方铺黑边，内容区就是那个比例
    if (contentX > 0.0f || contentY > 0.0f) {
        ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0.0f, 0.0f), ImGui::GetIO().DisplaySize,
                                                      IM_COL32(0, 0, 0, 255));
    }

    ImGui::SetNextWindowPos(contentPos);
    ImGui::SetNextWindowSize(contentSize);
    ImGui::Begin("##main", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    switch (screen_) {
        case Screen::Home: drawHome(); break;
        case Screen::Quiz: drawQuiz(); break;
        case Screen::Result: drawResult(); break;
        case Screen::Settings: drawSettings(); break;
        case Screen::Stats: drawStats(); break;
        case Screen::Finished: drawFinished(); break;
        case Screen::ExamResult: drawExamResult(); break;
        case Screen::ExamReady: drawExamReady(); break;
    }

    ImGui::End();

    if (openTagPopup_) {
        ImGui::OpenPopup("##tag_popup");
        openTagPopup_ = false;
    }
    if (ImGui::BeginPopup("##tag_popup")) {
        drawTagPopup();
        ImGui::EndPopup();
    }

    if (openTagExport_) {
        ImGui::OpenPopup("##tag_export_popup");
        openTagExport_ = false;
    }
    if (ImGui::BeginPopup("##tag_export_popup")) {
        drawTagExportPopup();
        ImGui::EndPopup();
    }

    if (openExamPicker_) {
        ImGui::OpenPopup("##exam_picker");
        openExamPicker_ = false;
    }
    // 选卷框放到屏幕中间：默认位置贴着按钮弹，会压住底部那一行
    const ImVec2 examDisplay = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(examDisplay.x * 0.5f, examDisplay.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopup("##exam_picker")) {
        drawExamPicker();
        ImGui::EndPopup();
    }

    if (examSubmitAsk_) {
        ImGui::OpenPopup("##exam_submit_ask");
        examSubmitAsk_ = false;
    }
    // NoTitleBar：默认标题栏里显示的是内部 id（## 开头），手机上就是一条空横条
    if (ImGui::BeginPopupModal("##exam_submit_ask", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoTitleBar)) {
        drawExamSubmitAsk();
        ImGui::EndPopup();
    }

    if (showIssues_) {
        ImGui::OpenPopup(context_.i18n().text("ui.home.issues"));
        showIssues_ = false;
    }
    if (ImGui::BeginPopupModal(context_.i18n().text("ui.home.issues"), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(issuesText_.c_str());
        ImGui::Spacing();
        if (ImGui::Button(context_.i18n().text("ui.common.ok"), ImVec2(120.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    drawToast(deltaSeconds);
}

void UiApp::applyFontRebuild() {
    if (!fontDirty_) {
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    const float minSide = std::min(io.DisplaySize.x, io.DisplaySize.y);
    const float scale = std::clamp(minSide / 620.0f, 1.05f, 2.20f);

    // 字形由 ImGui 在绘制时按需生成，这里只需设置字体与缩放。
    // 这个函数必须在一帧开始之前调用（见头文件里的说明）。
    render::rebuildFont(scale);
    fontDirty_ = false;
}

void UiApp::drawToast(float deltaSeconds) {
    if (!context_.message().empty()) {
        toast_ = context_.message();
        toastTimer_ = 3.0f;
        context_.clearMessage();
    }
    if (toastTimer_ <= 0.0f || toast_.empty()) {
        return;
    }
    toastTimer_ -= deltaSeconds;

    float toastX = 0.0f;
    float toastY = 0.0f;
    float toastWidth = 0.0f;
    float toastHeight = 0.0f;
    computeContentRect(toastX, toastY, toastWidth, toastHeight);
    const ImVec2 size(toastWidth * 0.9f, 0.0f);
    ImGui::SetNextWindowPos(ImVec2(toastX + toastWidth * 0.05f, toastY + toastHeight - 80.0f));
    ImGui::SetNextWindowSize(size);
    ImGui::Begin("##toast", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextWrapped("%s", toast_.c_str());
    ImGui::End();
}

// ---------------------------------------------------------------------------
// 首页
// ---------------------------------------------------------------------------
void UiApp::drawHome() {
    const float contentWidth = ImGui::GetContentRegionAvail().x;   // 这一行的可用宽度，用来右对齐

    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(toImU32(colorTextAccentRgb_)));
    ImGui::Text("%s", context_.i18n().text("ui.app_title"));
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", platform::name());

    //// ---- 语言切换（语言包接口的实际使用）----
    //ImGui::SetNextItemWidth(220.0f);
    //const std::string currentName = context_.i18n().languageName();
    //if (ImGui::BeginCombo(context_.i18n().text("ui.home.language"), currentName.c_str())) {
    //    for (const auto& code : context_.availableLanguages()) {
    //        const std::string name = context_.languageName(code);
    //        const bool selected = (code == context_.i18n().language());
    //        if (ImGui::Selectable(name.c_str(), selected)) {
    //            std::string error;
    //            if (context_.switchLanguage(code, error)) {
    //                context_.setMessage(context_.i18n().format("ui.lang.switched", {{"name", name}}));
    //                fontDirty_ = true;   // 换了语言，字形集合要重新生成
    //            } else {
    //                context_.setMessage(error);
    //            }
    //        }
    //        if (selected) {
    //            ImGui::SetItemDefaultFocus();
    //        }
    //    }
    //    ImGui::EndCombo();
    //}

    // 顶部两个图标靠右：量一下它们占多宽，再把行列位置挪到右边（和标题同一行）
    const float topButtonsWidth = iconButtonSide() * 3.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
    ImGui::SameLine(std::max(contentWidth - topButtonsWidth, 0.0f));
    if (iconButton("##open_stats", "icons/stats.svg", context_.i18n().text("ui.home.stats"))) {
        screen_ = Screen::Stats;
    }
    ImGui::SameLine();
    if (iconButton("##open_settings", "icons/settings.svg",
                   context_.i18n().text("ui.home.settings"))) {
        settingsScreen_.reset();
        screen_ = Screen::Settings;
    }
    ImGui::SameLine();
    if (iconButton("##upload_bank", "icons/upload.svg",
                   context_.i18n().text("ui.home.upload"))) {
        beginImport();   // 弹系统文件对话框（异步），选中的题库拷进 data/banks
    }

    ImGui::Separator();
    ImGui::Text("%s", context_.i18n().text("ui.home.bank_list"));

    // ---- 题库列表 ----
    const auto& banks = context_.banks();
    if (banks.empty()) {
        ImGui::TextWrapped("%s", context_.i18n().text("ui.home.no_banks"));
        return;
    }

    // 底部现在有两行图标小字 + 一个主按钮，预留高度要够（列表自身可滚动）。
    // 底部预留高度：主按钮 + 「刷题模式」一行（图标 + 说明文字）+ 「导出」一行。
    // 按当前字号算出来，换字号 / 换界面比例也不会把底部切掉；列表本身可以滚，多留一点没关系。
    // 底部现在是「主按钮 + 一行标题 + 一行图标」（以前是两行图标），预留高度按实际用量算；
    // 窄窗口会把「每轮题数」挤到下一行，那种情况再多留一行输入框的高度
    const float bottomReserve =
        controlHeight() + iconButtonSide() + ImGui::GetTextLineHeightWithSpacing() * 4.0f +
        (contentWidth < 700.0f ? controlHeight() : 0.0f);
    const float listHeight = std::max(ImGui::GetContentRegionAvail().y - bottomReserve, 140.0f);
    // 点了「删除题库」的第二下：出了循环再删（删完题库列表整个重建，循环里的引用会失效）
    int confirmedDelete = -1;
    if (ImGui::BeginChild("bank_list", ImVec2(0.0f, listHeight), ImGuiChildFlags_None)) {
        // 首页题库卡很多时列表会超出一屏。从卡片、按钮上纵向拖动都会接管为滚动；
        // 横向动作仍留给卡片上的图标控件，短按不动则照常打开题库。
        ScrollDrag scrollDrag;
        const ImU32 accentColor = toImU32(colorAccentRgb_);
        for (int i = 0; i < static_cast<int>(banks.size()); ++i) {
            const BankEntry& entry = banks[static_cast<size_t>(i)];
            ImGui::PushID(i);
            const bool selected = (i == context_.activeBank());
            const int total = static_cast<int>(entry.bank.questions.size());

            // 整张卡片就是一个按钮：框内任意位置点了都选中这个题库
            const int answered = entry.answeredCount();
            const std::string fileLine = context_.i18n().format(
                "ui.home.bank_line",
                {{"file", entry.sourceName}, {"total", std::to_string(total)}, {"lang", entry.bank.meta.lang}});
            std::string progressLine = context_.i18n().text("ui.home.not_started");
            if (answered > 0) {
                progressLine = context_.i18n().format(
                    "ui.home.progress_summary",
                    {{"answered", std::to_string(answered)},
                     {"total", std::to_string(total)},
                     {"correct", std::to_string(entry.correctCount())}});
            }
            const std::string warnText =
                entry.issues.empty() ? std::string()
                                     : ("⚠ " + std::to_string(entry.issues.size()));
            const float ratio =
                total > 0 ? static_cast<float>(answered) / static_cast<float>(total) : 0.0f;
            BankCardIcons icons;
            icons.wrongLabel = context_.i18n().text("ui.home.wrong_book");
            icons.clearLabel = context_.i18n().text("ui.home.restart");
            icons.deleteLabel = context_.i18n().text("ui.home.delete_bank");
            const int wrongCount = context_.wrongCountOf(i);
            const BankCardAction action =
                bankCard("##bank_card", entry.bank.meta.title, fileLine, progressLine, ratio,
                         answered > 0, warnText, selected, clearArmedBank_ == i,
                         deleteArmedBank_ == i, wrongCount > 0, accentColor,
                         toImU32(colorWrongRgb_), icons);
            if (action == BankCardAction::Open) {
                clearArmedBank_ = -1;
                deleteArmedBank_ = -1;
                selectedBank_ = i;
                context_.openBank(i, AppContext::OrderSource::Home);
            } else if (action == BankCardAction::WrongBook) {
                clearArmedBank_ = -1;
                deleteArmedBank_ = -1;
                if (wrongCount == 0) {
                    context_.setMessage(context_.i18n().format(
                        "ui.home.wrong_book_empty", {{"name", entry.bank.meta.title}}));
                } else {
                    selectedBank_ = i;
                    if (context_.openBank(i, AppContext::OrderSource::Home)) {
                        QuizSession& session = context_.session();
                        // 这一轮只刷错题（判据与「错题本」一致：含还没巩固到 3 倍的那些）
                        session.resetOrderWrongOnly(context_.settings().wrongBookRatio);
                        // 从第一道错题开始（不要停在别的位置）
                        const BankProgress& bankProgress =
                            context_.banks()[static_cast<size_t>(i)].progress;
                        for (size_t k = 0; k < bankProgress.questions.size(); ++k) {
                            const QuestionProgress& item = bankProgress.questions[k];
                            if (isWrongBookItem(item, context_.settings().wrongBookRatio)) {
                                session.jumpTo(static_cast<int>(k));
                                break;
                            }
                        }
                        screen_ = Screen::Quiz;
                    }
                }
            } else if (action == BankCardAction::Clear) {
                deleteArmedBank_ = -1;
                if (clearArmedBank_ == i) {
                    // 第二下：真的清（只清这个题库的答题数据，累计统计不动）
                    clearArmedBank_ = -1;
                    context_.clearBankProgress(i);
                    context_.setMessage(context_.i18n().format(
                        "ui.home.clear_done", {{"name", entry.bank.meta.title}}));
                } else {
                    clearArmedBank_ = i;   // 第一下：先问一次
                    context_.setMessage(context_.i18n().format(
                        "ui.home.clear_armed", {{"name", entry.bank.meta.title}}));
                }
            } else if (action == BankCardAction::Delete) {
                clearArmedBank_ = -1;
                if (deleteArmedBank_ == i) {
                    deleteArmedBank_ = -1;   // 第二下：标记成「待删除」，循环外执行
                    confirmedDelete = i;
                } else {
                    deleteArmedBank_ = i;    // 第一下：先问一次
                    context_.setMessage(context_.i18n().format(
                        "ui.home.delete_armed", {{"name", entry.bank.meta.title}}));
                }
            }
            ImGui::Spacing();
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    // 删除题库会把列表整个重建，所以放到循环外面做；这一帧就先画到这儿。
    if (confirmedDelete >= 0 && confirmedDelete < static_cast<int>(context_.banks().size())) {
        const std::string name =
            context_.banks()[static_cast<size_t>(confirmedDelete)].bank.meta.title;
        std::string error;
        if (context_.deleteBank(confirmedDelete, error)) {
            context_.setMessage(context_.i18n().format("ui.home.delete_done", {{"name", name}}));
        } else {
            context_.setMessage(error);
        }
        return;
    }

    const int activeIndex = context_.activeBank();
    const bool hasActive = activeIndex >= 0 && activeIndex < static_cast<int>(banks.size()) &&
                           bankUsable(banks[static_cast<size_t>(activeIndex)]);
    const bool hasProgress =
        hasActive && banks[static_cast<size_t>(activeIndex)].answeredCount() > 0;

    // ---- 底部第一行：开始/继续（主按钮）----
    ImGui::Spacing();
    // 刷题模式选了「考试」：主按钮变成「开始考试」，点它先选卷（题库里可能有好几套卷）
    const bool examMode = context_.settings().homeOrder == "exam";
    const bool examReady = hasActive && !context_.activeBankExams().empty();
    // 考试还在进行中（中途回首页看过别的东西）：主按钮写「继续考试」，点了直接回卷面。
    // 这里不要求 homeOrder == exam：卷子还在跑，主按钮就该是「继续考试」
    const bool examRunning = context_.examActive();
    const bool mainEnabled = examRunning || (examMode ? examReady : hasActive);
    const char* mainLabel = examRunning
                                ? context_.i18n().text("ui.exam.continue")
                                : (examMode ? context_.i18n().text("ui.exam.start")
                                            : (hasProgress
                                                   ? context_.i18n().text("ui.home.continue")
                                                   : context_.i18n().text("ui.home.start")));
    ImGui::BeginDisabled(!mainEnabled);
    if (primaryButton(mainLabel, ImVec2(-1.0f, controlHeight()), colorAccentRgb_, mainEnabled)) {
        if (examRunning) {
            cancelAutoNext();
            screen_ = Screen::Quiz;   // 卷还在内存里：倒计时照走，答案也在
        } else if (examMode) {
            openExamPicker();
        } else {
            startBank(activeIndex);
        }
    }
    ImGui::EndDisabled();

    // ---- 底部：刷题模式 + 每轮题数 + 导出，全挤在同一行里（间距按可用宽度算）----
    // 上面一行放三个标题，各自对准下面那一组：刷题模式 / 每轮题数 / 导出
    ImGui::Spacing();
    {
        struct ModeItem {
            const char* svg;
            const char* labelKey;   // 完整名字（提示语里用）
            const char* shortKey;   // 按钮下面的短说明
            const char* value;      // 写进 settings.home_order 的值；空串 = 还没做
        };
        // 「错题训练」在每张题库卡片右侧的「错题本」图标上（只刷那一个题库的错题）
        const ModeItem modeItems[] = {
            {"icons/layout-arrow-right.svg", "ui.home.practice_sequential", "ui.home.mode_short_sequential",
             "sequence"},
            {"icons/arrows-shuffle.svg", "ui.home.practice_shuffle", "ui.home.mode_short_shuffle",
             "shuffle"},
            {"icons/current-location.svg", "ui.home.practice_weighted",
             "ui.home.mode_short_weighted", "weighted"},
            {"icons/view.svg", "ui.home.practice_recite", "ui.home.mode_short_recite", "recite"},
            // 考试模式：题库里写了卷声明（[exam]）才点得动，否则灰掉
            {"icons/scroll-text.svg", "ui.home.practice_exam", "ui.home.mode_short_exam", "exam"},
        };
        constexpr int kMaxModes = 8;
        const int modeCount = static_cast<int>(sizeof(modeItems) / sizeof(modeItems[0]));
        const float side = iconButtonSide();
        const float arrowSide = ImGui::GetFrameHeight();   // ArrowButton 是正方形，边长 = 行高
        const float rowStart = ImGui::GetCursorPosX();
        const float styleGap = ImGui::GetStyle().ItemSpacing.x;

        // ---- 先量尺寸，再定间距：5 个模式 + 导出 2 个 + 「每轮题数」全在一行 ----
        // 手机上内部宽度只有 499px，英文短说明还可能比图标宽，所以按每一项的实际宽度累加
        float itemWidth[kMaxModes] = {0};
        float modesWidth = 0.0f;
        for (int k = 0; k < modeCount && k < kMaxModes; ++k) {
            const float caption = ImGui::CalcTextSize(context_.i18n().text(modeItems[k].shortKey)).x;
            itemWidth[k] = std::max(side, caption);
            modesWidth += itemWidth[k];
        }
        // 「每轮题数」这一组：标题在上一行，这里只有输入框 + 单位
        const float inputWidth = ImGui::GetFontSize() * 3.4f;
        const float unitWidth = ImGui::CalcTextSize(context_.i18n().text("ui.home.round_size_unit")).x;
        const float blockWidth = inputWidth + styleGap + unitWidth;
        // 模式组与导出组之间留一条明显的缝，免得看起来像 8 个并列的模式
        const float groupGap = std::max(styleGap * 1.6f, 14.0f);
        const float available = ImGui::GetContentRegionAvail().x;

        // 顺序按示意图：模式组 → 「每轮题数」→ 导出组（两组之间都留一条缝）
        const float fixedWidth = modesWidth + groupGap + blockWidth + groupGap + side + arrowSide;
        const int gapCount = modeCount;   // 模式之间 (n-1) 个 + ↓ 与 ▲ 之间 1 个
        float gap = styleGap;
        if (fixedWidth + gap * static_cast<float>(gapCount) > available) {
            // 放不下就压缩间距（最少 4px），先保住「一行」
            gap = std::max(4.0f, (available - fixedWidth) / static_cast<float>(gapCount));
        }
        // 压到 4px 还放不下（极窄窗口）：让「每轮题数」另起一行，总比互相压着好
        const bool blockSameLine = fixedWidth + gap * static_cast<float>(gapCount) <= available;
        const float blockX =
            rowStart + modesWidth + gap * static_cast<float>(modeCount - 1) + groupGap;
        // 块换到下一行时，导出组顶上它原来的位置（否则会被挤到窗口外，右边那个 ▲ 会缺一半）
        const float exportX = blockSameLine ? (blockX + blockWidth + groupGap)
                                            : (rowStart + modesWidth + gap * static_cast<float>(modeCount - 1) +
                                               groupGap);

        // ---- 标题行：三个标题各自对准下面那一组 ----
        ImGui::TextDisabled("%s", context_.i18n().text("ui.home.section_practice"));
        if (blockSameLine) {
            // 块换行时它自己会带标题，这里就别再挤一个上去（会和「导出」叠在一起）
            ImGui::SameLine(blockX);
            ImGui::TextDisabled("%s", context_.i18n().text("ui.home.round_size"));
        }
        ImGui::SameLine(exportX);
        ImGui::TextDisabled("%s", context_.i18n().text("ui.home.section_export"));

        // ---- 图标行 ----
        float itemX = rowStart;
        for (int k = 0; k < modeCount; ++k) {
            const ModeItem& item = modeItems[k];
            const float width = k < kMaxModes ? itemWidth[k] : side;
            // 图标在自己的格子里居中：英文短说明比图标宽时，图标不会偏到一边。
            // 注意第一项和后面的项调用方式不同：SetCursorPosX 只改 x、不会回到这一行，
            // 后面的项必须用 SameLine(x)（它同时把 y 拉回这一行），否则每个模式都会换行
            if (k == 0) {
                ImGui::SetCursorPosX(itemX + (width - side) * 0.5f);
            } else {
                ImGui::SameLine(itemX + (width - side) * 0.5f);
            }
            const std::string shortLabel = context_.i18n().text(item.shortKey);
            // 考试模式要看这个题库有没有卷声明：没有卷声明的题库考不了（灰掉）
            const bool implemented =
                item.value[0] != '\0' &&
                (std::string(item.value) != "exam" || !context_.activeBankExams().empty());
            const bool selected =
                implemented && context_.settings().homeOrder == std::string(item.value);
            ImGui::PushID(k);
            ImGui::BeginGroup();
            if (!implemented) {
                ImGui::BeginDisabled();   // 功能还没做：灰掉、点不动
            }
            if (modeButton("##mode", item.svg, shortLabel, selected, colorAccentRgb_) && implemented) {
                context_.settings().homeOrder = item.value;
                context_.saveSettings();   // 选中态已经能看出切到哪个模式，不再弹 toast
            }
            if (!implemented) {
                ImGui::EndDisabled();
            }
            ImGui::EndGroup();
            ImGui::PopID();
            itemX += width + gap;
        }

        // ---- 同一行的导出组：↓ 导出题库 + ▲（点它从按钮上方竖着拉出另三个）----
        ImGui::SameLine(exportX);
        // 导出也带两字说明（与刷题模式一行同款：图标 + 下面的短说明）
        ImGui::BeginGroup();
        if (modeButton("##export_bank", "icons/download.svg",
                       context_.i18n().text("ui.home.export_short_bank"), false, colorAccentRgb_)) {
            beginExport(AppContext::ExportKind::WholeBank);
        }
        ImGui::EndGroup();
        ImGui::SameLine(exportX + side + gap);
        if (ImGui::ArrowButton("##export_more", ImGuiDir_Up)) {
            ImGui::OpenPopup("##export_more_panel");
        }
        // 面板整体放到按钮上方：pivot 用 (0,1)（窗口左下角对准锚点），高度多少都不用先知道
        ImGui::SetNextWindowPos(ImGui::GetItemRectMin(), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
        if (ImGui::BeginPopup("##export_more_panel")) {
            struct ExportItem {
                const char* id;
                const char* svg;
                const char* shortKey;   // 图标下面的两字说明
                bool tagPick = false;   // 标签题要先弹标签选择框，不是直接导出
                AppContext::ExportKind kind = AppContext::ExportKind::WholeBank;
            };
            // 竖着排：离按钮最近的放错题本（和示意图一致）
            const ExportItem items[] = {
                {"##export_tags", "icons/export-tags.svg", "ui.home.export_short_tags", true,
                 AppContext::ExportKind::WholeBank},
                {"##export_marked", "icons/export-marked.svg", "ui.home.export_short_marked", false,
                 AppContext::ExportKind::MarkedOnly},
                {"##export_wrong", "icons/export-wrong.svg", "ui.home.export_short_wrong", false,
                 AppContext::ExportKind::WrongOnly},
            };
            for (const ExportItem& item : items) {
                ImGui::BeginGroup();
                if (modeButton(item.id, item.svg, context_.i18n().text(item.shortKey), false,
                               colorAccentRgb_)) {
                    if (item.tagPick) {
                        // 先刷新标签列表（题库自带的 + 用户自己加的），再开弹窗
                        tagExportList_ = context_.activeBankTags();
                        tagExportChecked_.assign(tagExportList_.size(), 0);
                        tagExportBake_ = false;
                        openTagExport_ = true;
                    } else {
                        beginExport(item.kind);
                    }
                    ImGui::CloseCurrentPopup();   // 选了就收起来，别挡着
                }
                ImGui::EndGroup();
            }
            ImGui::EndPopup();
        }

        // ---- 同一行右侧：「每轮题数」的输入框 ----
        if (blockSameLine) {
            ImGui::SameLine(blockX);
        } else {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", context_.i18n().text("ui.home.round_size"));
            ImGui::SameLine();
        }
        int roundSize = context_.settings().roundSize;
        ImGui::SetNextItemWidth(inputWidth);
        ImGui::InputInt("##home_round_size", &roundSize, 0, 0, ImGuiInputTextFlags_AutoSelectAll);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            context_.settings().roundSize =
                std::clamp(roundSize, Settings::kMinRoundSize, Settings::kMaxRoundSize);
            context_.saveSettings();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", context_.i18n().text("ui.home.round_size_unit"));
    }
    if (hasActive && !banks[static_cast<size_t>(activeIndex)].issues.empty()) {
        ImGui::SameLine();
        if (ImGui::Button(context_.i18n().text("ui.home.issues"))) {
            openIssuesPopup();
        }
    }

    // 「数据目录 / 字体」两行挪到设置页去了：首页是给手机看的，别塞调试信息
}

void UiApp::beginImport() {
    if (pendingDialog_ != PendingDialog::None) {
        return;   // 已经有一个系统对话框在等着了
    }
    if (!platform::beginPickFiles(context_.i18n().text("ui.home.import_title"),
                                  io::bankFileExtensions(), true)) {
        context_.setMessage(context_.i18n().text("ui.home.import_failed"));
        return;
    }
    pendingDialog_ = PendingDialog::Import;
}

void UiApp::startBank(int index) {
    if (index < 0 || index >= static_cast<int>(context_.banks().size())) {
        return;
    }
    if (!context_.openBank(index, AppContext::OrderSource::Home)) {
        return;
    }
    screen_ = Screen::Quiz;
}

// 发起导出：先把系统的「保存」对话框弹出来（默认落在 data/export/，用户可以改成别的位置）
void UiApp::beginExport(AppContext::ExportKind kind) {
    if (pendingDialog_ != PendingDialog::None) {
        return;   // 已经有一个系统对话框在等着了
    }
    const std::string suggested =
        platform::joinPath(context_.exportDirectory(), context_.suggestedExportName(kind));
    if (!platform::beginPickSavePath(context_.i18n().text("ui.export.save_title"), suggested,
                                     std::vector<std::string>{".quiz"})) {
        context_.setMessage(context_.i18n().format(
            "ui.export.failed", {{"reason", context_.i18n().text("ui.export.failed_pick")}}));
        return;
    }
    pendingDialog_ = PendingDialog::ExportBank;
    pendingExportKind_ = kind;
}

// 每帧看一眼：发起过的系统对话框有结果了吗？有就接着把活干完。
// （Android 上对话框消失时应用会被停一下，这些代码要等回到前台后才轮到执行 —— 没关系，
//   文件与进度都不依赖那一刻的内存状态。）
void UiApp::pollPendingDialog() {
    if (pendingDialog_ == PendingDialog::None) {
        return;
    }

    if (pendingDialog_ == PendingDialog::Import) {
        std::vector<std::string> paths;
        bool cancelled = false;
        if (!platform::takePickedFiles(paths, cancelled)) {
            return;   // 用户还在挑
        }
        pendingDialog_ = PendingDialog::None;
        if (cancelled) {
            return;   // 用户取消：什么都不做
        }
        if (paths.empty()) {
            context_.setMessage(context_.i18n().text("ui.home.import_failed"));
            return;
        }
        std::vector<std::string> names;
        std::string error;
        if (context_.importBankFiles(paths, names, error)) {
            std::string joined;
            for (size_t i = 0; i < names.size(); ++i) {
                if (i > 0) {
                    joined += "、";
                }
                joined += names[i];
            }
            context_.setMessage(context_.i18n().format(
                "ui.home.import_ok",
                {{"count", std::to_string(names.size())}, {"names", joined}}));
        } else {
            context_.setMessage(error);
        }
        return;
    }

    // 导出（题库 / 错题 / 收藏 / 标签）：用户选好位置了
    std::string path;
    bool cancelled = false;
    if (!platform::takePickedSavePath(path, cancelled)) {
        return;   // 用户还在挑
    }
    const PendingDialog kind = pendingDialog_;
    pendingDialog_ = PendingDialog::None;
    if (cancelled) {
        return;   // 取消：什么都不做
    }
    if (path.empty()) {
        context_.setMessage(context_.i18n().format(
            "ui.export.failed", {{"reason", context_.i18n().text("ui.export.failed_pick")}}));
        return;
    }
    std::string error;
    if (kind == PendingDialog::ExportTags) {
        if (context_.exportTaggedTo(pendingTags_, pendingBake_, path, error)) {
            reportExportResult(path);
        } else {
            context_.setMessage(error);
        }
        return;
    }
    if (context_.exportActiveTo(pendingExportKind_, path, error)) {
        reportExportResult(path);
    } else {
        context_.setMessage(error);
    }
}

// 导出内容已经写好的收尾动作。桌面端什么都不做（路径本来就是用户选的）；
// Android 上 SAF 是「先选位置、后写内容」：现在才把内容复制进用户选中的文档，
// 并拿回一个用户认得出的位置（例如 Download/xxx.quiz）用于提示。
void UiApp::reportExportResult(const std::string& localPath) {
    std::string display;
    if (platform::commitPickedSave(localPath, display)) {
        context_.setMessage(context_.i18n().format("ui.export.done", {{"path", display}}));
    } else {
        context_.setMessage(context_.i18n().format(
            "ui.export.failed", {{"reason", context_.i18n().text("ui.export.failed_commit")}}));
    }
}
void UiApp::openIssuesPopup() {
    const int index = context_.activeBank();
    if (index < 0 || index >= static_cast<int>(context_.banks().size())) {
        return;
    }
    const auto& issues = context_.banks()[static_cast<size_t>(index)].issues;
    issuesText_.clear();
    char buffer[256];
    for (const auto& issue : issues) {
        if (issue.line > 0) {
            std::snprintf(buffer, sizeof(buffer), "第 %d 行：%s\n", issue.line, issue.message.c_str());
        } else {
            std::snprintf(buffer, sizeof(buffer), "%s\n", issue.message.c_str());
        }
        issuesText_ += buffer;
        if (issuesText_.size() > 4000) {
            issuesText_ += "…\n";
            break;
        }
    }
    showIssues_ = true;
}

// ---------------------------------------------------------------------------
// 刷题页
// ---------------------------------------------------------------------------
void UiApp::drawQuiz() {
    const auto& banks = context_.banks();
    const int index = context_.activeBank();
    if (index < 0 || index >= static_cast<int>(banks.size())) {
        screen_ = Screen::Home;
        return;
    }
    QuizSession& session = context_.session();

    // 考试模式：到点自动交卷（交卷后直接画成绩页）
    const bool examRunning = context_.examActive();
    if (examRunning && context_.examRemainingSeconds() == 0) {
        finishExamNow();
        return;
    }

    // 先推进「答对自动跳下一题」的倒计时：这一帧就能画到新题
    tickAutoNext();

    const Question* question = session.currentQuestion();

    // ---- 顶栏：返回（图标）+ 题库名 + 第几题；进度条细一点，别抢戏 ----
    const float headerWidth = ImGui::GetContentRegionAvail().x;
    // 题号 / 进度都按「这一轮要刷的题」算（已经答对的题不进这一轮）
    const int planSize = session.planSize();
    const std::string progressText =
        planSize > 0
            ? context_.i18n().format("ui.quiz.progress",
                                     {{"current", std::to_string(session.planPosition())},
                                      {"total", std::to_string(planSize)}})
            : context_.i18n().format("ui.quiz.progress",
                                     {{"current", std::to_string(session.currentIndex() + 1)},
                                      {"total", std::to_string(session.questionCount())}});
    if (iconButton("##quiz_back", "icons/home.svg",
                   context_.i18n().text("ui.quiz.back_home"))) {
        cancelAutoNext();
        context_.saveProgress();
        screen_ = Screen::Home;
    }
    ImGui::SameLine();
    ImGui::Text("%s", banks[static_cast<size_t>(index)].bank.meta.title.c_str());
    // 右上角「第 x / y 题」本身就是个按钮：点它也能开题目总览（和顶部那个图标重复，冗余设计）
    const ImVec2 progressPad(6.0f, 2.0f);
    const float progressButtonWidth = ImGui::CalcTextSize(progressText.c_str()).x + progressPad.x * 2.0f;
    ImGui::SameLine(std::max(headerWidth - progressButtonWidth - 4.0f, 0.0f));
    const ImU32 headerBg = ImGui::GetColorU32(ImGuiCol_WindowBg);
    ImVec4 headerHover = ImGui::ColorConvertU32ToFloat4(shadeColor(headerBg, 0.20f));
    ImVec4 headerActive = ImGui::ColorConvertU32ToFloat4(shadeColor(headerBg, 0.40f));
    headerHover.w = 1.0f;
    headerActive.w = 1.0f;
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, headerHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, headerActive);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, progressPad);
    if (ImGui::Button(progressText.c_str())) {
        openOverviewPopup();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);

    // 考试中：卷名 + 剩余时间（最后 5 分钟变红；不限时的卷不显示倒计时）
    if (examRunning) {
        const int remaining = context_.examRemainingSeconds();
        if (remaining >= 0) {
            ImGui::Spacing();
            const ImU32 clockColor =
                remaining <= 300 ? toImU32(colorWrongRgb_) : toImU32(colorAccentRgb_);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(clockColor), "%s",
                               context_.i18n()
                                   .format("ui.exam.remaining", {{"time", clockText(remaining)}})
                                   .c_str());
            if (!context_.examPaper().title.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", context_.examPaper().title.c_str());
            }
        }
    }

    // 考试中：进度看「作答了多少」（不显示对错，交卷前谁都不知道）
    int examAnswered = 0;
    if (examRunning) {
        for (int planIndex : session.order()) {
            if (session.isExamAnsweredAt(planIndex)) {
                ++examAnswered;
            }
        }
    }
    if (planSize > 0) {
        const int planDone = examRunning ? examAnswered : planSize - session.planRemaining();
        ImGui::Spacing();
        ImGui::ProgressBar(static_cast<float>(planDone) / static_cast<float>(planSize),
                           ImVec2(-1.0f, 6.0f), "");
        if (examRunning) {
            // 考试里只显示「已作答 x / y 题」：上面那行「本轮 …」是同一个数字，重复
            ImGui::TextDisabled(
                "%s",
                context_.i18n()
                    .format("ui.exam.answered_line",
                            {{"answered", std::to_string(examAnswered)},
                             {"total", std::to_string(planSize)}})
                    .c_str());
        } else {
            // 「答对」必须是**本轮**的：以前这里是 session.stats()（题库历史累计），
            // 于是新一轮刚开始、一道题都还没答，就会显示上一轮的正确数
            const SessionStats round = session.roundStats();
            ImGui::TextDisabled(
                "%s · %s",
                context_.i18n()
                    .format("ui.quiz.plan_summary",
                            {{"done", std::to_string(planDone)},
                             {"total", std::to_string(planSize)}})
                    .c_str(),
                context_.i18n()
                    .format("ui.quiz.round_correct", {{"correct", std::to_string(round.correct)}})
                    .c_str());
        }
    }

    if (question == nullptr) {
        ImGui::TextWrapped("%s", context_.i18n().text("ui.quiz.empty_bank"));
        return;
    }

    // 背题模式：进到哪道题就把哪道的答案亮出来（材料题一页里有好几道小题，一起亮）
    applyReciteIfNeeded();

    // 材料题：整段材料 + 同一段材料下的所有小题画在一页里（可上下滚动），逐题作答
    const std::vector<int> paperGroup = session.materialGroupOf(session.currentIndex());
    if (!paperGroup.empty()) {
        cancelAutoNext();
        drawPaper(banks[static_cast<size_t>(index)].bank, paperGroup);
        drawOverviewPanel();   // 卷面页也能开总览（入口在下面的导航里）
        return;
    }

    // ---- 题型标签 + 收藏（收藏用书签图标，选中时变成强调色实心）----
    const ImU32 accentColor = toImU32(colorAccentRgb_);
    chip(typeLabelOf(context_.i18n(), question->type), accentColor, contrastText(accentColor));
    ImGui::SameLine();
    const bool marked = session.isMarked(session.currentIndex());
    if (iconButton("##quiz_mark", marked ? "icons/star-filled.svg" : "icons/star.svg",
                   context_.i18n().text(marked ? "ui.quiz.unmark" : "ui.quiz.mark"),
                   marked ? colorStarRgb_ : 0u)) {
        session.toggleMark();
        context_.saveProgress();
    }
    ImGui::SameLine();
    if (iconButton("##quiz_tag", "icons/export-tags.svg",
                   context_.i18n().text("ui.quiz.tags"))) {
        tagTargetIndex_ = session.currentIndex();
        tagBuffer_[0] = '\0';
        openTagPopup_ = true;
    }
    // 从错题本进来的这一轮：这道题**本次答对**之后，可以手动把它移出错题本
    // （把答题历史重置成「0 次错误、1 次正确」）。
    // 不是从错题本进来的 → 整个图标都不显示；还没答对 → 灰掉
    if (session.isWrongOnlyRound()) {
        ImGui::SameLine();
        const bool canRemove = session.submitted() && session.isCorrect();
        ImGui::BeginDisabled(!canRemove);
        // 图标样式与旁边的星标一致（不单独染色）：没答对时由上面的 BeginDisabled 灰掉
        if (iconButton("##quiz_remove_wrong", "icons/file-x.svg",
                       context_.i18n().text("ui.quiz.remove_from_wrong"))) {
            if (session.removeFromWrongBook(session.currentIndex(), platform::nowString())) {
                context_.saveProgress();
                context_.setMessage(context_.i18n().text("ui.quiz.remove_from_wrong_done"));
            }
        }
        ImGui::EndDisabled();
    }
    // 题目总览：圆点网格，点一下跳到那一题（材料题卷面在下面的导航里也有一个入口）
    ImGui::SameLine();
    if (iconButton("##quiz_overview", "icons/grid.svg",
                   context_.i18n().text("ui.quiz.overview"))) {
        openOverviewPopup();
    }
    const std::vector<std::string> quizTags = session.tags();
    if (!quizTags.empty()) {
        ImGui::Spacing();
        tagChips(quizTags);
    }

    // ---- 题干卡片：高度按内容算，短题干不再是一大片空框 ----
    const std::string stemText = question->stemFor(context_.i18n().language());
    const float stemWrap = std::max(ImGui::GetContentRegionAvail().x - cardPadding() * 2.0f - 4.0f, 80.0f);
    const float stemHeight =
        std::max(controlHeight(), wrappedTextHeight(stemText, stemWrap) + cardPadding() * 2.0f + 6.0f);
    ImGui::Spacing();
    if (cardBegin("stem", stemHeight)) {
        ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
        ImGui::TextWrapped("%s", stemText.c_str());
        ImGui::PopTextWrapPos();
    }
    cardEnd();

    // ---- 选项 ----
    // ---- 填空题：题干下面按编号依次列出每个空（编号由题干里的文字对应）----
    if (question->type == QuestionType::Fill) {
        const int blankCount = std::max(question->fillBlankCount(), 1);
        for (int blank = 0; blank < blankCount; ++blank) {
            ImGui::PushID(blank);
            if (blank == 0 && fillFocusIndex_ != session.currentIndex() && !session.submitted()) {
                fillFocusIndex_ = session.currentIndex();
                ImGui::SetKeyboardFocusHere();   // 一到填空题就可以直接敲字，不用先点一下
            }
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%d.", blank + 1);
            ImGui::SameLine(52.0f);
            ImGui::SetNextItemWidth(-1.0f);
            bool entered = false;
            inputTextString("##fill", blank == 0 ? context_.i18n().text("ui.quiz.fill_placeholder") : "",
                            session.fillTextAt(session.currentIndex(), blank), session.submitted(),
                            toImU32(colorAccentRgb_), &entered);
            ImGui::PopID();
            if (entered && !session.submitted()) {
                submitCurrent();   // 所有空都填了才会真的交上去
            }
        }
        // 这两行提示在手机上会被截掉右边：按可用宽度自动换行
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        if (session.submitted()) {
            ImGui::TextDisabled("%s", context_.i18n().text("ui.quiz.fill_submitted"));
        } else {
            ImGui::TextDisabled("%s", context_.i18n().text("ui.quiz.fill_hint"));
        }
        ImGui::PopTextWrapPos();
    }

    // ---- 多项有序题：上面列备选，下面每个空一排字母按钮 ----
    if (question->type == QuestionType::Ordered) {
        ImGui::TextDisabled("%s", context_.i18n().text("ui.quiz.ordered_pool"));
        for (int i = 0; i < static_cast<int>(question->options.size()); ++i) {
            ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
            ImGui::Text("%s. %s", indexToLetter(i).c_str(),
                        question->options[static_cast<size_t>(i)].c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::Spacing();

        const int slotCount = question->slotCount();
        const float buttonWidth = std::max(44.0f, ImGui::GetFontSize() * 2.4f);
        for (int slot = 0; slot < slotCount; ++slot) {
            ImGui::PushID(slot);
            std::string slotLabel = context_.i18n().format(
                "ui.quiz.ordered_slot", {{"index", std::to_string(slot + 1)}});
            if (slot < static_cast<int>(question->blanks.size()) &&
                !question->blanks[static_cast<size_t>(slot)].empty()) {
                slotLabel = question->blanks[static_cast<size_t>(slot)];
            }
            ImGui::Text("%s", slotLabel.c_str());
            ImGui::SameLine();
            const float available = ImGui::GetContentRegionAvail().x;
            const int chosen = session.orderedSlot(slot);
            float used = 0.0f;
            for (int i = 0; i < static_cast<int>(question->options.size()); ++i) {
                if (used > 0.0f) {
                    if (used + buttonWidth > available) {
                        used = 0.0f;   // 一行放不下就换行（手机竖屏）
                    } else {
                        ImGui::SameLine();
                    }
                }
                const bool picked = chosen == i;
                // 三个状态都设成同一个颜色：手指/鼠标停在按钮上时（hover / active）
                // 也要看得出「这个空已经选了这个字母」
                bool pushed = false;
                if (picked) {
                    const ImVec4 pickedColor = ImGui::ColorConvertU32ToFloat4(
                        shadeColor(ImGui::GetColorU32(ImGuiCol_Button), 0.90f));
                    ImGui::PushStyleColor(ImGuiCol_Button, pickedColor);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, pickedColor);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, pickedColor);
                    pushed = true;
                }
                ImGui::PushID(i);
                if (ImGui::Button(indexToLetter(i).c_str(), ImVec2(buttonWidth, 0.0f))) {
                    if (!session.submitted()) {
                        session.setOrderedSlot(slot, picked ? -1 : i);
                        cancelAutoNext();
                    }
                }
                ImGui::PopID();
                if (pushed) {
                    ImGui::PopStyleColor(3);
                }
                used += buttonWidth + ImGui::GetStyle().ItemSpacing.x;
            }
            ImGui::PopID();
        }
    }

    // 填空题与多项有序题不用「整行选项」这套
    const bool useOptionRows =
        question->type != QuestionType::Fill && question->type != QuestionType::Ordered;
    if (useOptionRows) {
        const int optionCount = question->type == QuestionType::Judge
                                    ? 2
                                    : static_cast<int>(question->options.size());
        const ImU32 accent = toImU32(colorAccentRgb_);
        const ImU32 correctColor = toImU32(colorCorrectRgb_);
        const ImU32 wrongColor = toImU32(colorWrongRgb_);
        for (int i = 0; i < optionCount; ++i) {
            std::string letter;
            std::string text;
            if (question->type == QuestionType::Judge) {
                // 判断题不摆字母，直接写「正确 / 错误」
                text = (i == 0) ? context_.i18n().text("ui.quiz.judge_true")
                                : context_.i18n().text("ui.quiz.judge_false");
            } else {
                letter = indexToLetter(i);
                text = question->options[static_cast<size_t>(i)];
            }
            const bool selected = session.isSelected(i);
            RowState state = RowState::Normal;
            if (session.submitted()) {
                const bool isAnswer =
                    std::find(question->answer.begin(), question->answer.end(), i) !=
                    question->answer.end();
                state = isAnswer ? RowState::Correct : (selected ? RowState::Wrong : RowState::Muted);
            } else if (selected) {
                state = RowState::Selected;
            }
            ImGui::PushID(i);
            if (optionRow("##option", letter, text, state, !session.submitted(), accent,
                          correctColor, wrongColor)) {
                session.selectOption(i);
                cancelAutoNext();
                // 设置开启时：单选 / 判断题点一下就直接判分（多选题仍要点「提交答案」）
                const bool singleAnswer = question->type == QuestionType::Single ||
                                          question->type == QuestionType::Judge ||
                                          question->type == QuestionType::Material;
                if (singleAnswer && context_.settings().autoSubmitSingle && !session.submitted()) {
                    submitCurrent();
                }
            }
            ImGui::PopID();
        }
    }

    // ---- 底部操作条：主按钮占满一行，导航三个等宽 ----
    ImGui::Spacing();
    if (examRunning) {
        // 考试中：没有「提交答案」（答案先留在草稿里，随时能改），只有「交卷」
        if (primaryButton(context_.i18n().text("ui.exam.submit"), ImVec2(-1.0f, controlHeight()),
                          colorAccentRgb_)) {
            examSubmitAsk_ = true;
        }
    } else {
        const bool canSubmit = !session.submitted() && session.hasAnswerInput();
        ImGui::BeginDisabled(!canSubmit);
        if (primaryButton(context_.i18n().text("ui.quiz.submit"), ImVec2(-1.0f, controlHeight()),
                          colorAccentRgb_, canSubmit)) {
            submitCurrent();
        }
        ImGui::EndDisabled();
    }

    const float navWidth =
        (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
    if (ImGui::Button(context_.i18n().text("ui.quiz.prev"), ImVec2(navWidth, 0.0f))) {
        cancelAutoNext();
        session.prev();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!session.hasNextInPlan());
    if (ImGui::Button(context_.i18n().text("ui.quiz.next"), ImVec2(navWidth, 0.0f))) {
        cancelAutoNext();
        session.next();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    // 第三个按钮：考试中给「总览」（交卷在上面那个大按钮上）、交过卷给「成绩单」，
    // 普通练习还是「本次结果」
    if (examRunning) {
        if (ImGui::Button(context_.i18n().text("ui.quiz.overview"), ImVec2(navWidth, 0.0f))) {
            openOverviewPopup();
        }
    } else if (context_.hasExamSummary()) {
        if (ImGui::Button(context_.i18n().text("ui.exam.review"), ImVec2(navWidth, 0.0f))) {
            cancelAutoNext();
            screen_ = Screen::ExamResult;
        }
    } else if (ImGui::Button(context_.i18n().text("ui.result.title"), ImVec2(navWidth, 0.0f))) {
        cancelAutoNext();
        context_.saveProgress();
        screen_ = Screen::Result;
    }

    // ---- 判定与解析：装在一张卡片里（别到处是边框）----
    const std::string analysis = question->analysisFor(context_.i18n().language());
    if (session.submitted()) {
        std::string verdict;
        std::string detail;
        ImU32 verdictColor = toImU32(colorCorrectRgb_);
        if (session.isCorrect() || session.isReciteMode()) {
            // 背题：界面上就是「看答案」的样子（和答对了的显示一致）
            verdict = context_.i18n().text("ui.quiz.result_correct");
        } else if (session.lastScore() > 0.0f) {
            char percent[16];
            std::snprintf(percent, sizeof(percent), "%.0f%%",
                          static_cast<double>(session.lastScore()) * 100.0);
            verdict = context_.i18n().format("ui.quiz.result_partial", {{"score", percent}});
            verdictColor = toImU32(colorAccentRgb_);
            detail = context_.i18n().format("ui.quiz.answer_is",
                                            {{"answer", answerTextOf(*question, context_.i18n())}});
        } else {
            verdict = context_.i18n().format("ui.quiz.result_wrong",
                                             {{"answer", answerTextOf(*question, context_.i18n())}});
            verdictColor = toImU32(colorWrongRgb_);
        }

        const float innerWidth =
            std::max(ImGui::GetContentRegionAvail().x - cardPadding() * 2.0f, 80.0f);
        float cardHeight = cardPadding() * 2.0f + ImGui::GetTextLineHeight();
        if (!detail.empty()) {
            cardHeight += ImGui::GetStyle().ItemSpacing.y + wrappedTextHeight(detail, innerWidth);
        }
        if (!analysis.empty()) {
            cardHeight += ImGui::GetStyle().ItemSpacing.y * 2.0f + ImGui::GetTextLineHeight() +
                          wrappedTextHeight(analysis, innerWidth);
        }
        cardHeight = std::min(cardHeight, 340.0f);   // 解析太长就让卡片自己滚
        ImGui::Spacing();
        if (cardBegin("verdict", cardHeight)) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(verdictColor), "%s", verdict.c_str());
            if (!detail.empty()) {
                ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
                ImGui::TextWrapped("%s", detail.c_str());
                ImGui::PopTextWrapPos();
            }
            if (!analysis.empty()) {
                ImGui::Spacing();
                ImGui::TextDisabled("%s", context_.i18n().text("ui.quiz.analysis"));
                ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
                ImGui::TextWrapped("%s", analysis.c_str());
                ImGui::PopTextWrapPos();
            }
        }
        cardEnd();
    }

    // ---- 自动跳下一题：提示 + 剩余进度（设置里开启、且答对时才会出现）----
    if (autoNextTimer_ > 0.0f) {
        const float total = std::max(context_.settings().autoNextDelay(), 0.05f);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(toImU32(colorAccentRgb_)), "%s",
                           context_.i18n().text("ui.quiz.auto_next_hint"));
        ImGui::ProgressBar(1.0f - autoNextTimer_ / total, ImVec2(240.0f, 6.0f), "");
    }

    drawOverviewPanel();
}

// ---------------------------------------------------------------------------
// 题目总览：圆点网格，点一下跳到那一题
// ---------------------------------------------------------------------------
// 只列**本轮**的题，编号用**轮内位置**（1..N，和顶部「第 x / N 题」一致）——
// 权重刷题一轮里同一道题可能出现多次，按位置编号才不会一题两个号。
// 颜色：未答=灰、答对=绿、在错题本里=红、部分分=黄；当前这一题外圈描边；收藏右上角一个星标色小点。
void UiApp::openOverviewPopup() {
    ImGui::OpenPopup("##quiz_overview_panel");
}

void UiApp::drawOverviewPanel() {
    QuizSession& session = context_.session();
    const int bankIndex = context_.activeBank();
    if (!session.valid() || bankIndex < 0 ||
        bankIndex >= static_cast<int>(context_.banks().size())) {
        return;
    }
    const std::vector<int>& plan = session.order();
    const int count = static_cast<int>(plan.size());
    if (count <= 0) {
        return;
    }
    const BankEntry& entry = context_.banks()[static_cast<size_t>(bankIndex)];
    const BankProgress& progress = entry.progress;
    const QuestionBank& bank = entry.bank;

    // 面板大小：宽度留边、高度按行数算（题多就在面板里滚）
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    // 面板要比网格宽一些：右边得留出滚动条的位置，否则滚动条会压在最后一列的圆上
    const float panelWidth = std::min(display.x - 24.0f, 660.0f);
    const float scrollbar = ImGui::GetStyle().ScrollbarSize + ImGui::GetStyle().ItemSpacing.x;
    const float gridWidth = panelWidth - ImGui::GetStyle().WindowPadding.x * 2.0f - scrollbar;
    const float gap = ImGui::GetStyle().ItemSpacing.x * 0.5f;
    // 圆的直径按 ~52px 反推每行几个（以前固定 5/8 个，圆太大）；至少 5 个、最多 10 个一行
    int perRow = static_cast<int>(gridWidth / 52.0f);
    perRow = std::clamp(perRow, 5, 10);
    const float cell =
        (gridWidth - gap * static_cast<float>(perRow - 1)) / static_cast<float>(perRow);
    const int rows = (count + perRow - 1) / perRow;
    // 行间距是 ItemSpacing.y（换行后光标往下走的就是它）；以前按 x 间距算，
    // 总高度算小了一点点 → 明明放得下也冒出滚动条（手机上一眼就能看见）
    const float gridHeight = static_cast<float>(rows) * cell +
                             static_cast<float>(rows - 1) * ImGui::GetStyle().ItemSpacing.y;
    const float chrome = ImGui::GetTextLineHeightWithSpacing() * 3.4f +
                         ImGui::GetStyle().WindowPadding.y * 2.0f;
    const float maxGrid = display.y * 0.62f;
    // +2px 余量：行高按浮点算会有 1 像素的舍入误差，正好放得下时不该冒滚动条
    const float scrollHeight = std::min(gridHeight + 2.0f, maxGrid);
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(panelWidth, scrollHeight + chrome), ImGuiCond_Always);
    if (!ImGui::BeginPopup("##quiz_overview_panel", ImGuiWindowFlags_NoMove)) {
        return;
    }

    const ImU32 accent = toImU32(colorAccentRgb_);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(accent), "%s",
                       context_.i18n().text("ui.quiz.overview"));
    ImGui::Separator();

    ImDrawList* const draw = ImGui::GetWindowDrawList();
    const int currentPos = session.planPosition() - 1;
    const ImU32 idleFill = shadeColor(ImGui::GetColorU32(ImGuiCol_WindowBg), 0.10f);
    const ImU32 idleText = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    if (ImGui::BeginChild("##overview_grid", ImVec2(0.0f, scrollHeight), ImGuiChildFlags_None)) {
        // 题型总览固定一行 10 题，题多时纵向滚动；格点用 InvisibleButton，
        // 从格点之间的空隙拖动可滚，按在格点上则保留“跳题”点击。
        ScrollDrag scrollDrag;
        for (int k = 0; k < count; ++k) {
            if (k % perRow != 0) {
                ImGui::SameLine(0.0f, gap);
            }
            ImGui::PushID(k);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const bool clicked = ImGui::InvisibleButton("##cell", ImVec2(cell, cell));
            const int index = plan[static_cast<size_t>(k)];
            const QuestionProgress* item =
                (index >= 0 && index < static_cast<int>(progress.questions.size()))
                    ? &progress.questions[static_cast<size_t>(index)]
                    : nullptr;
            const Question* question =
                (index >= 0 && index < static_cast<int>(bank.questions.size()))
                    ? &bank.questions[static_cast<size_t>(index)]
                    : nullptr;
            // 颜色按**本轮**的状态，不拿历史充数：
            //   灰 = 本轮还没动过；蓝 = 正在作答（选了 / 填了但还没交）；
            //   绿 = 已提交且全对；黄 = 部分分；红 = 答错
            ImU32 fill = idleFill;
            ImU32 textColor = idleText;
            if (!session.isExamMode() && session.isSubmittedAt(index) && item != nullptr) {
                unsigned int rgb = colorWrongRgb_;
                if (item->correct) {
                    rgb = colorCorrectRgb_;
                } else if (item->partiallyCorrect()) {
                    rgb = colorPartialRgb_;
                }
                fill = (toImU32(rgb) & 0x00FFFFFFu) | (60u << IM_COL32_A_SHIFT);
                textColor = toImU32(rgb);
            } else if (session.hasDraftAt(index)) {
                fill = (toImU32(colorDraftRgb_) & 0x00FFFFFFu) | (60u << IM_COL32_A_SHIFT);
                textColor = toImU32(colorDraftRgb_);
            }
            const ImVec2 center(at.x + cell * 0.5f, at.y + cell * 0.5f);
            const float radius = cell * 0.5f - 2.0f;
            draw->AddCircleFilled(center, radius, fill);
            // 材料题：圆面里再描一个半径小 3px 的圆环，和同一段材料的其它小题一眼区分
            if (question != nullptr && !question->materialId.empty()) {
                draw->AddCircle(center, std::max(radius - 3.0f, 2.0f),
                                (textColor & 0x00FFFFFFu) | (150u << IM_COL32_A_SHIFT), 0, 1.5f);
            }
            if (k == currentPos) {
                draw->AddCircle(center, radius, accent, 0, 3.0f);   // 当前这一题
            }
            char label[16];
            std::snprintf(label, sizeof(label), "%d", k + 1);
            const ImVec2 textSize = ImGui::CalcTextSize(label);
            draw->AddText(ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f),
                          textColor, label);
            if (item != nullptr && item->marked) {
                draw->AddCircleFilled(ImVec2(at.x + cell - 7.0f, at.y + 7.0f), 4.0f,
                                      toImU32(colorStarRgb_));
            }
            if (clicked) {
                cancelAutoNext();
                session.jumpTo(index);
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();
    if (ImGui::Button(context_.i18n().text("ui.common.close"), ImVec2(140.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// 材料题卷面：材料 + 同一段材料下的所有小题，画在一页里（可上下滚动）
// ---------------------------------------------------------------------------
void UiApp::drawPaper(const QuestionBank& bank, const std::vector<int>& group) {
    QuizSession& session = context_.session();
    const Question* current = session.currentQuestion();
    const Material* material =
        current != nullptr ? bank.materialById(current->materialId) : nullptr;

    std::string header = context_.i18n().text("ui.quiz.material_title");
    if (material != nullptr && !material->title.empty()) {
        header += "｜" + material->title;
    }
    // 手机上「材料 ｜ 标题」+「本段材料共 N 小题」挤一行会把右边截掉：标题自己换行，小题数另起一行
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(toImU32(colorAccentRgb_)), "%s",
                       header.c_str());
    ImGui::PopTextWrapPos();
    ImGui::TextDisabled("%s",
                        context_.i18n()
                            .format("ui.quiz.paper_summary", {{"count", std::to_string(group.size())}})
                            .c_str());

    const bool wholePage = context_.settings().paperSubmitWholePage;
    const bool examRunning = session.isExamMode();
    // 考试中底部多一行「已作答 x / y 题」+ 交卷按钮，留白比整页模式再多一行
    const float navHeight = examRunning ? 190.0f : (wholePage ? 150.0f : 58.0f);
    const float paperHeight = std::max(ImGui::GetContentRegionAvail().y - navHeight, 160.0f);
    if (ImGui::BeginChild("paper_scroll", ImVec2(0.0f, paperHeight), ImGuiChildFlags_Borders)) {
        // 材料正文与多道小题共用这一页；从题干 / 材料空白处上划滚动整页。
        // 输入框和选项仍是控件优先，拖动它们不会把卷面甩走。
        ScrollDrag scrollDrag;
        if (material != nullptr && !material->text.empty()) {
            // 材料正文：卡片样式，太长就让卡片自己滚
            const float materialWrap =
                std::max(ImGui::GetContentRegionAvail().x - cardPadding() * 2.0f - 4.0f, 80.0f);
            const float materialHeight = std::min(
                wrappedTextHeight(material->text, materialWrap) + cardPadding() * 2.0f + 6.0f, 200.0f);
            if (cardBegin("paper_material", materialHeight)) {
                ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
                ImGui::TextWrapped("%s", material->text.c_str());
                ImGui::PopTextWrapPos();
            }
            cardEnd();
            ImGui::Separator();
        }
        for (size_t k = 0; k < group.size(); ++k) {
            drawPaperQuestion(bank, group[k], static_cast<int>(k) + 1, static_cast<int>(group.size()));
            if (k + 1 < group.size()) {
                ImGui::Separator();
            }
        }
    }
    ImGui::EndChild();

    // 底部：考试中只有「交卷」（答案先留在草稿里，随时能改）；整页模式是「提交本页」
    if (examRunning) {
        int answered = 0;
        for (int index : group) {
            if (session.isExamAnsweredAt(index)) {
                ++answered;
            }
        }
        ImGui::TextDisabled("%s",
                            context_.i18n()
                                .format("ui.exam.answered_line",
                                        {{"answered", std::to_string(answered)},
                                         {"total", std::to_string(group.size())}})
                                .c_str());
        if (primaryButton(context_.i18n().text("ui.exam.submit"), ImVec2(-1.0f, controlHeight()),
                          colorAccentRgb_)) {
            examSubmitAsk_ = true;
        }
    } else if (wholePage) {
        int filled = 0;
        int pending = 0;
        for (int index : group) {
            if (session.isSubmittedAt(index)) {
                continue;
            }
            ++pending;
            if (session.hasAnswerInputAt(index)) {
                ++filled;
            }
        }
        ImGui::TextDisabled("%s",
                            context_.i18n()
                                .format("ui.quiz.paper_filled",
                                        {{"filled", std::to_string(filled)},
                                         {"count", std::to_string(pending)}})
                                .c_str());
        ImGui::BeginDisabled(filled == 0);
        if (ImGui::Button(context_.i18n().text("ui.quiz.submit_paper"), ImVec2(200.0f, 0.0f))) {
            const int count = session.submitPage(group, platform::nowString());
            context_.saveProgress();
            context_.setMessage(context_.i18n().format(
                "ui.quiz.paper_submitted", {{"count", std::to_string(count)}}));
        }
        ImGui::EndDisabled();
    }

    const int previous = session.prevGroupIndex(session.currentIndex());
    // 四个等宽按钮：以前的固定宽度（220/220/160）在手机上会溢出，顺手改成按可用宽度算。
    // 考试中「本次结果」换成上面的「交卷」，这里只留三个按钮。
    const float navCount = examRunning ? 3.0f : 4.0f;
    const float navWidth =
        (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * (navCount - 1.0f)) /
        navCount;
    ImGui::BeginDisabled(previous < 0);
    if (ImGui::Button(context_.i18n().text("ui.quiz.prev_paper"), ImVec2(navWidth, 0.0f))) {
        cancelAutoNext();
        session.jumpTo(previous);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    const int following = session.nextGroupIndex(session.currentIndex());
    ImGui::BeginDisabled(following < 0);
    if (ImGui::Button(context_.i18n().text("ui.quiz.next_paper"), ImVec2(navWidth, 0.0f))) {
        cancelAutoNext();
        session.jumpTo(following);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(context_.i18n().text("ui.quiz.overview"), ImVec2(navWidth, 0.0f))) {
        openOverviewPopup();
    }
    if (!examRunning) {
        ImGui::SameLine();
        if (context_.hasExamSummary()) {
            if (ImGui::Button(context_.i18n().text("ui.exam.review"), ImVec2(navWidth, 0.0f))) {
                cancelAutoNext();
                screen_ = Screen::ExamResult;
            }
        } else if (ImGui::Button(context_.i18n().text("ui.result.title"), ImVec2(navWidth, 0.0f))) {
            cancelAutoNext();
            context_.saveProgress();
            screen_ = Screen::Result;
        }
    }
}

void UiApp::drawPaperQuestion(const QuestionBank& bank, int index, int part, int count) {
    (void)count;
    QuizSession& session = context_.session();
    if (index < 0 || index >= static_cast<int>(bank.questions.size())) {
        return;
    }
    const Question& question = bank.questions[static_cast<size_t>(index)];
    const bool examRunning = session.isExamMode();
    const bool locked = session.isSubmittedAt(index);
    // 考试中「已作答」= 草稿里有内容（还没判分）；平时 = 这道小题交过了
    const bool answered = examRunning ? session.isExamAnsweredAt(index) : locked;
    ImGui::PushID(index);

    // 标题行：第 n 小题 · 题型 · 已答 / 未答 · 收藏
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(toImU32(colorAccentRgb_)), "%s",
                       context_.i18n()
                           .format("ui.quiz.paper_part", {{"index", std::to_string(part)}})
                           .c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("[%s]", typeLabelOf(context_.i18n(), question.type));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", answered ? context_.i18n().text("ui.quiz.answered")
                                       : context_.i18n().text("ui.quiz.unanswered"));
    ImGui::SameLine();
    const bool marked = session.isMarked(index);
    if (iconButton("##paper_mark", marked ? "icons/star-filled.svg" : "icons/star.svg",
                   context_.i18n().text(marked ? "ui.quiz.unmark" : "ui.quiz.mark"),
                   marked ? colorStarRgb_ : 0u)) {
        session.toggleMarkAt(index);
        context_.saveProgress();
    }
    ImGui::SameLine();
    if (iconButton("##paper_tag", "icons/export-tags.svg",
                   context_.i18n().text("ui.quiz.tags"))) {
        tagTargetIndex_ = index;
        tagBuffer_[0] = '\0';
        openTagPopup_ = true;
    }
    const std::vector<std::string> paperTags = session.tagsAt(index);
    if (!paperTags.empty()) {
        ImGui::Spacing();
        tagChips(paperTags);
    }

    ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
    ImGui::TextWrapped("%s", question.stemFor(context_.i18n().language()).c_str());
    ImGui::PopTextWrapPos();

    // ---- 作答控件（按题型）----
    if (question.type == QuestionType::Fill) {
        const int blankCount = std::max(question.fillBlankCount(), 1);
        for (int blank = 0; blank < blankCount; ++blank) {
            ImGui::PushID(blank);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%d.", blank + 1);
            ImGui::SameLine(52.0f);
            ImGui::SetNextItemWidth(-1.0f);
            bool entered = false;
            inputTextString("##paper_fill", blank == 0 ? context_.i18n().text("ui.quiz.fill_placeholder") : "",
                            session.fillTextAt(index, blank), locked, toImU32(colorAccentRgb_), &entered);
            ImGui::PopID();
            if (!answered && entered && !context_.settings().paperSubmitWholePage && !examRunning) {
                session.submitAt(index, platform::nowString());
                context_.saveProgress();
            }
        }
    } else if (question.type == QuestionType::Ordered) {
        ImGui::TextDisabled("%s", context_.i18n().text("ui.quiz.ordered_pool"));
        for (int i = 0; i < static_cast<int>(question.options.size()); ++i) {
            ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
            ImGui::Text("%s. %s", indexToLetter(i).c_str(),
                        question.options[static_cast<size_t>(i)].c_str());
            ImGui::PopTextWrapPos();
        }
        const int slotCount = question.slotCount();
        const float buttonWidth = std::max(44.0f, ImGui::GetFontSize() * 2.4f);
        for (int slot = 0; slot < slotCount; ++slot) {
            ImGui::PushID(slot);
            std::string slotLabel = context_.i18n().format(
                "ui.quiz.ordered_slot", {{"index", std::to_string(slot + 1)}});
            if (slot < static_cast<int>(question.blanks.size()) &&
                !question.blanks[static_cast<size_t>(slot)].empty()) {
                slotLabel = question.blanks[static_cast<size_t>(slot)];
            }
            ImGui::Text("%s", slotLabel.c_str());
            ImGui::SameLine();
            const float available = ImGui::GetContentRegionAvail().x;
            const int chosen = session.orderedSlotAt(index, slot);
            float used = 0.0f;
            for (int i = 0; i < static_cast<int>(question.options.size()); ++i) {
                if (used > 0.0f) {
                    if (used + buttonWidth > available) {
                        used = 0.0f;   // 放不下就换行
                    } else {
                        ImGui::SameLine();
                    }
                }
                const bool picked = chosen == i;
                bool pushed = false;
                if (picked) {
                    const ImVec4 pickedColor = ImGui::ColorConvertU32ToFloat4(
                        shadeColor(ImGui::GetColorU32(ImGuiCol_Button), 0.90f));
                    ImGui::PushStyleColor(ImGuiCol_Button, pickedColor);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, pickedColor);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, pickedColor);
                    pushed = true;
                }
                ImGui::PushID(i);
                if (ImGui::Button(indexToLetter(i).c_str(), ImVec2(buttonWidth, 0.0f))) {
                    if (!answered) {
                        session.setOrderedSlotAt(index, slot, picked ? -1 : i);
                        cancelAutoNext();
                    }
                }
                ImGui::PopID();
                if (pushed) {
                    ImGui::PopStyleColor(3);
                }
                used += buttonWidth + ImGui::GetStyle().ItemSpacing.x;
            }
            ImGui::PopID();
        }
    } else {
        const int optionCount =
            question.type == QuestionType::Judge ? 2 : static_cast<int>(question.options.size());
        const ImU32 paperAccent = toImU32(colorAccentRgb_);
        const ImU32 paperCorrect = toImU32(colorCorrectRgb_);
        const ImU32 paperWrong = toImU32(colorWrongRgb_);
        for (int i = 0; i < optionCount; ++i) {
            std::string letter;
            std::string text;
            if (question.type == QuestionType::Judge) {
                text = (i == 0) ? context_.i18n().text("ui.quiz.judge_true")
                                : context_.i18n().text("ui.quiz.judge_false");
            } else {
                letter = indexToLetter(i);
                text = question.options[static_cast<size_t>(i)];
            }
            const bool selected = session.isSelectedAt(index, i);
            RowState state = RowState::Normal;
            if (answered && !examRunning) {
                const bool isAnswer =
                    std::find(question.answer.begin(), question.answer.end(), i) !=
                    question.answer.end();
                state = isAnswer ? RowState::Correct : (selected ? RowState::Wrong : RowState::Muted);
            } else if (selected) {
                state = RowState::Selected;
            }
            ImGui::PushID(i);
            if (optionRow("##paper_option", letter, text, state, !answered, paperAccent,
                          paperCorrect, paperWrong)) {
                session.selectOptionAt(index, i);
                cancelAutoNext();
                const bool singleAnswer = question.type == QuestionType::Single ||
                                          question.type == QuestionType::Judge ||
                                          question.type == QuestionType::Material;
                if (!context_.settings().paperSubmitWholePage && singleAnswer && !examRunning &&
                    context_.settings().autoSubmitSingle && !session.isSubmittedAt(index)) {
                    session.submitAt(index, platform::nowString());
                    context_.saveProgress();
                }
            }
            ImGui::PopID();
        }
    }

    // ---- 提交 / 结果 ----
    // 逐题提交：每题下面有自己的提交按钮；整页提交：统一在页面底部交
    const bool wholePage = context_.settings().paperSubmitWholePage;
    if (!answered) {
        if (!wholePage && !examRunning) {
            ImGui::Spacing();
            ImGui::BeginDisabled(!session.hasAnswerInputAt(index));
            if (ImGui::Button(context_.i18n().text("ui.quiz.submit"), ImVec2(160.0f, 0.0f))) {
                session.submitAt(index, platform::nowString());
                context_.saveProgress();
            }
            ImGui::EndDisabled();
        }
    } else if (!examRunning) {
        const float score = session.scoreAt(index);
        if (session.isCorrectAt(index) || session.isReciteMode()) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(toImU32(colorCorrectRgb_)), "%s",
                               context_.i18n().text("ui.quiz.result_correct"));
        } else if (score > 0.0f) {
            char percent[16];
            std::snprintf(percent, sizeof(percent), "%.0f%%", static_cast<double>(score) * 100.0);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(toImU32(colorAccentRgb_)), "%s",
                               context_.i18n()
                                   .format("ui.quiz.result_partial", {{"score", percent}})
                                   .c_str());
            ImGui::TextColored(
                ImGui::ColorConvertU32ToFloat4(toImU32(colorWrongRgb_)), "%s",
                context_.i18n()
                    .format("ui.quiz.answer_is",
                            {{"answer", answerTextOf(question, context_.i18n())}})
                    .c_str());
        } else {
            ImGui::TextColored(
                ImGui::ColorConvertU32ToFloat4(toImU32(colorWrongRgb_)), "%s",
                context_.i18n()
                    .format("ui.quiz.result_wrong",
                            {{"answer", answerTextOf(question, context_.i18n())}})
                    .c_str());
        }
        const std::string analysis = question.analysisFor(context_.i18n().language());
        if (!analysis.empty()) {
            ImGui::TextDisabled("%s", context_.i18n().text("ui.quiz.analysis"));
            ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
            ImGui::TextWrapped("%s", analysis.c_str());
            ImGui::PopTextWrapPos();
        }
    }
    ImGui::PopID();
}

// ---------------------------------------------------------------------------
// 结果页
// ---------------------------------------------------------------------------
void UiApp::drawResult() {
    QuizSession& session = context_.session();
    const SessionStats stats = session.stats();

    if (iconButton("##result_back", "icons/home.svg",
                   context_.i18n().text("ui.result.back_home"))) {
        screen_ = Screen::Home;
    }
    ImGui::SameLine();
    ImGui::Text("%s", context_.i18n().text("ui.result.title"));

    // ---- 统计卡片 ----
    char rateText[32];
    std::snprintf(rateText, sizeof(rateText), "%.0f%%", stats.accuracy() * 100.0f);
    const ImU32 accentColor = toImU32(colorAccentRgb_);
    ImGui::Spacing();
    float statsHeight = cardPadding() * 2.0f + ImGui::GetTextLineHeight() + 6.0f + 8.0f +
                        wrappedTextHeight(
                            context_.i18n().format("ui.result.summary",
                                                   {{"total", std::to_string(stats.total)},
                                                    {"answered", std::to_string(stats.answered)},
                                                    {"correct", std::to_string(stats.correct)},
                                                    {"rate", rateText}}),
                            std::max(ImGui::GetContentRegionAvail().x - cardPadding() * 2.0f, 80.0f));
    if (stats.partial > 0) {
        statsHeight += ImGui::GetStyle().ItemSpacing.y + ImGui::GetTextLineHeight();
    }
    statsHeight += 10.0f;   // 留点余量：差几像素的话卡片会自己冒出滚动条
    if (cardBegin("stats", statsHeight)) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(accentColor), "%s", rateText);
        ImGui::ProgressBar(stats.accuracy(), ImVec2(-1.0f, 8.0f), "");
        ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
        ImGui::TextDisabled("%s",
                            context_.i18n()
                                .format("ui.result.summary",
                                        {{"total", std::to_string(stats.total)},
                                         {"answered", std::to_string(stats.answered)},
                                         {"correct", std::to_string(stats.correct)},
                                         {"rate", rateText}})
                                .c_str());
        if (stats.partial > 0) {
            ImGui::TextDisabled(
                "%s",
                context_.i18n()
                    .format("ui.result.partial_line", {{"partial", std::to_string(stats.partial)}})
                    .c_str());
        }
        ImGui::PopTextWrapPos();
    }
    cardEnd();

    ImGui::Spacing();
    ImGui::TextDisabled("%s", context_.i18n().text("ui.result.wrong_list"));

    const QuestionBank* bank = nullptr;
    const int index = context_.activeBank();
    if (index >= 0 && index < static_cast<int>(context_.banks().size())) {
        bank = &context_.banks()[static_cast<size_t>(index)].bank;
    }

    int wrongShown = 0;
    const float listHeight = std::max(ImGui::GetContentRegionAvail().y - 74.0f, 120.0f);
    if (bank != nullptr &&
        ImGui::BeginChild("wrong_list", ImVec2(0.0f, listHeight), ImGuiChildFlags_Borders)) {
        // 错题本可能远超一屏；列表项本身是 Selectable，从行间拖动滚动，
        // 单击行仍是跳题，二者由 UiScroll 的 6px 门槛区分。
        ScrollDrag scrollDrag;
        for (size_t i = 0; i < bank->questions.size(); ++i) {
            const QuestionProgress& item = context_.banks()[static_cast<size_t>(index)].progress.questions[i];
            if (!isWrongBookItem(item, context_.settings().wrongBookRatio)) {
                continue;
            }
            ++wrongShown;
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(bank->questions[i].stem.c_str(), false, 0,
                                  ImVec2(0.0f, controlHeight() * 0.8f))) {
                cancelAutoNext();
                session.jumpTo(static_cast<int>(i));
                screen_ = Screen::Quiz;
            }
            ImGui::PopID();
        }
        if (wrongShown == 0) {
            ImGui::TextDisabled("%s", context_.i18n().text("ui.result.no_wrong"));
        }
    }
    ImGui::EndChild();

    const float navWidth =
        (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
    if (primaryButton(context_.i18n().text("ui.result.redo_wrong"), ImVec2(navWidth, controlHeight()),
                      colorAccentRgb_, wrongShown > 0)) {
        cancelAutoNext();
        session.restartWrongOnly();
        context_.saveProgress();
        context_.restartTimer();
        screen_ = Screen::Quiz;
    }
    ImGui::SameLine();
    if (ImGui::Button(context_.i18n().text("ui.result.export_wrong"), ImVec2(navWidth, 0.0f))) {
        beginExport(AppContext::ExportKind::WrongOnly);
    }
    ImGui::SameLine();
    ImGui::PushID("result_footer");
    if (ImGui::Button(context_.i18n().text("ui.quiz.back_home"), ImVec2(navWidth, 0.0f))) {
        screen_ = Screen::Home;
    }
    ImGui::PopID();
}

// ---------------------------------------------------------------------------
// 本轮刷完的结算页（刷完一轮给点正反馈）
// ---------------------------------------------------------------------------
void UiApp::checkRoundFinished() {
    QuizSession& session = context_.session();
    if (!session.valid() || !session.planCompleted()) {
        return;
    }
    cancelAutoNext();
    context_.saveProgress();
    screen_ = Screen::Finished;
}

void UiApp::drawFinished() {
    QuizSession& session = context_.session();
    const i18n::I18n& texts = context_.i18n();
    const ImU32 accentColor = toImU32(colorAccentRgb_);
    const int bankIndex = context_.activeBank();
    const BankProgress* progress = nullptr;
    if (bankIndex >= 0 && bankIndex < static_cast<int>(context_.banks().size())) {
        progress = &context_.banks()[static_cast<size_t>(bankIndex)].progress;
    }

    // 这一轮的成绩（只看这一轮计划里的题）
    int done = 0;
    int correct = 0;
    int streak = 0;
    int bestStreak = 0;
    if (progress != nullptr) {
        for (int index : session.order()) {
            if (index < 0 || index >= static_cast<int>(progress->questions.size())) {
                continue;
            }
            const QuestionProgress& item = progress->questions[static_cast<size_t>(index)];
            if (!item.answered) {
                continue;
            }
            ++done;
            if (item.correct) {
                ++correct;
                ++streak;
                bestStreak = std::max(bestStreak, streak);
            } else {
                streak = 0;
            }
        }
    }
    const float rate = done > 0 ? static_cast<float>(correct) / static_cast<float>(done) : 0.0f;
    char rateText[16];
    std::snprintf(rateText, sizeof(rateText), "%.0f%%", static_cast<double>(rate) * 100.0);
    const int wrongLeft = done - correct;

    if (iconButton("##finish_back", "icons/home.svg", texts.text("ui.finish.home"))) {
        screen_ = Screen::Home;
    }
    ImGui::SameLine();
    ImGui::Text("%s", texts.text("ui.finish.title"));

    // 大标题 + 按正确率分档的鼓励语
    const char* praise = texts.text(rate >= 0.9f   ? "ui.finish.praise_high"
                                    : rate >= 0.6f ? "ui.finish.praise_mid"
                                                   : "ui.finish.praise_low");
    ImGui::Spacing();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(accentColor), "%s",
                       texts.text("ui.finish.headline"));
    ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
    ImGui::TextWrapped("%s", praise);
    ImGui::PopTextWrapPos();

    // 卡片：本轮 / 累计 / 还剩几道错题
    const Stats& stats = context_.stats();
    const float lineH = ImGui::GetTextLineHeight();
    const float gap = ImGui::GetStyle().ItemSpacing.y;
    const std::string roundLine = texts.format(
        "ui.finish.round_line",
        {{"done", std::to_string(done)}, {"correct", std::to_string(correct)}, {"rate", rateText}});
    const std::string timeLine = texts.format(
        "ui.finish.time_line",
        {{"time", formatDuration(context_.elapsedSeconds(), texts)}, {"streak", std::to_string(bestStreak)}});
    const std::string totalLine =
        texts.format("ui.finish.total_line",
                     {{"count", std::to_string(stats.totalSubmitted)},
                      {"days", std::to_string(stats.currentStreakDays)}});
    const std::string wrongLine =
        wrongLeft > 0 ? texts.format("ui.finish.wrong_line", {{"count", std::to_string(wrongLeft)}})
                      : texts.text("ui.finish.no_wrong");
    ImGui::Spacing();
    const float cardHeight = cardPadding() * 2.0f + lineH * 4.0f + gap * 4.0f + 16.0f;
    if (cardBegin("finish_card", cardHeight)) {
        ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
        ImGui::TextWrapped("%s", roundLine.c_str());
        ImGui::TextDisabled("%s", timeLine.c_str());
        ImGui::Spacing();
        ImGui::TextDisabled("%s", totalLine.c_str());
        ImGui::TextDisabled("%s", wrongLine.c_str());
        ImGui::PopTextWrapPos();
    }
    cardEnd();

    // 底部：再刷一轮（剩下的错题优先）/ 本次结果 / 返回首页
    ImGui::Spacing();
    if (primaryButton(texts.text("ui.finish.again"), ImVec2(-1.0f, controlHeight()), colorAccentRgb_)) {
        cancelAutoNext();
        context_.openBank(bankIndex, AppContext::OrderSource::Home);   // 重新生成本轮计划
        screen_ = Screen::Quiz;
    }
    const float navWidth =
        (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button(texts.text("ui.finish.result"), ImVec2(navWidth, 0.0f))) {
        screen_ = Screen::Result;
    }
    ImGui::SameLine();
    if (ImGui::Button(texts.text("ui.finish.home"), ImVec2(navWidth, 0.0f))) {
        screen_ = Screen::Home;
    }
}

// ---------------------------------------------------------------------------
// 数据统计页
// ---------------------------------------------------------------------------
void UiApp::drawStats() {
    const i18n::I18n& texts = context_.i18n();
    const Stats& stats = context_.stats();
    const ImU32 accentColor = toImU32(colorAccentRgb_);
    const std::string today = dayOf(platform::nowString());

    if (iconButton("##stats_back", "icons/home.svg", texts.text("ui.stats.back"))) {
        screen_ = Screen::Home;
    }
    ImGui::SameLine();
    ImGui::Text("%s", texts.text("ui.stats.title"));

    // 现况：从当前题库列表里数出来（累计数在 stats.txt 里，题库增删都不受影响）
    int bankTotal = 0;
    int bankDone = 0;
    int bankWrong = 0;
    int markedTotal = 0;
    int wrongTotal = 0;
    for (const BankEntry& entry : context_.banks()) {
        ++bankTotal;
        int answered = 0;
        int wrong = 0;
        for (const QuestionProgress& item : entry.progress.questions) {
            if (item.marked) {
                ++markedTotal;
            }
            if (!item.answered) {
                continue;
            }
            ++answered;
            if (!item.correct) {
                ++wrong;
            }
        }
        wrongTotal += wrong;
        const int total = static_cast<int>(entry.bank.questions.size());
        if (total > 0 && answered >= total && wrong == 0) {
            ++bankDone;
        }
        if (wrong > 0) {
            ++bankWrong;
        }
    }

    const float lineH = ImGui::GetTextLineHeight();
    const float gap = ImGui::GetStyle().ItemSpacing.y;
    const float scrollHeight = std::max(ImGui::GetContentRegionAvail().y, 140.0f);

    if (ImGui::BeginChild("stats_scroll", ImVec2(0.0f, scrollHeight), ImGuiChildFlags_None)) {
        // 统计页由多张高度不同的卡片组成，整体放在一个滚动 child 里；
        // 从任意卡片空白 / 卡片之间上划都能滚，卡片内部又有自己的局部滚动。
        ScrollDrag scrollDrag;
        // 一张统计卡片 = 几行文字（第一行可以用强调色，当小标题）
        auto statCard = [&](const char* id, const std::vector<std::pair<std::string, bool>>& lines) {
            float height = cardPadding() * 2.0f + 6.0f + lineH * static_cast<float>(lines.size()) +
                           gap * static_cast<float>(lines.size() > 0 ? lines.size() - 1 : 0);
            if (cardBegin(id, height)) {
                for (size_t k = 0; k < lines.size(); ++k) {
                    if (lines[k].second) {
                        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(accentColor), "%s",
                                           lines[k].first.c_str());
                    } else {
                        ImGui::TextDisabled("%s", lines[k].first.c_str());
                    }
                }
            }
            cardEnd();
        };

        if (stats.totalSubmitted == 0) {
            ImGui::Spacing();
            statCard("stats_empty", {{texts.text("ui.stats.empty"), false}});
            ImGui::EndChild();
            return;
        }

        char rate[16];
        std::snprintf(rate, sizeof(rate), "%.0f%%",
                      static_cast<double>(stats.accuracy()) * 100.0);

        // ① 累计刷题 + 正确率
        ImGui::Spacing();
        statCard("stats_total",
                 {{texts.format("ui.stats.total_line",
                                {{"count", std::to_string(stats.totalSubmitted)}}),
                   true},
                  {texts.format("ui.stats.accuracy_line",
                                {{"correct", std::to_string(stats.totalCorrect)}, {"rate", rate}}),
                   false}});

        // ② 使用时长 + 平均每题
        const double perQuestion =
            stats.totalSubmitted > 0
                ? stats.totalSeconds / static_cast<double>(stats.totalSubmitted)
                : 0.0;
        char perText[32];
        std::snprintf(perText, sizeof(perText), "%.0f", perQuestion);
        ImGui::Spacing();
        statCard("stats_time",
                 {{texts.format("ui.stats.time_line",
                                {{"time", formatDuration(stats.totalSeconds, texts)}}),
                   true},
                  {texts.format("ui.stats.per_question", {{"seconds", perText}}), false}});

        // ③ 题库完成情况 + 收藏/错题
        {
            std::vector<std::pair<std::string, bool>> lines;
            lines.push_back({texts.format("ui.stats.banks_line",
                                          {{"done", std::to_string(bankDone)},
                                           {"total", std::to_string(bankTotal)}}),
                             true});
            lines.push_back({texts.format("ui.stats.banks_ever",
                                          {{"count", std::to_string(stats.completedBanks.size())}}),
                             false});
            lines.push_back({bankWrong > 0
                                 ? texts.format("ui.stats.banks_left",
                                                {{"count", std::to_string(bankWrong)}})
                                 : texts.text("ui.stats.banks_all_done"),
                             false});
            lines.push_back({texts.format("ui.stats.marks_line",
                                          {{"marked", std::to_string(markedTotal)},
                                           {"wrong", std::to_string(wrongTotal)}}),
                             false});
            ImGui::Spacing();
            statCard("stats_banks", lines);
        }

        // ④ 连续天数 + 首次/最近
        ImGui::Spacing();
        statCard("stats_days",
                 {{texts.format("ui.stats.streak_line",
                                {{"current", std::to_string(stats.currentStreakDays)},
                                 {"best", std::to_string(stats.bestStreakDays)}}),
                   true},
                  {texts.format("ui.stats.dates_line",
                                {{"first", stats.firstUsedAt.size() >= 10 ? stats.firstUsedAt.substr(0, 10)
                                                                         : std::string("-")},
                                 {"last", stats.lastAnsweredAt.size() >= 10
                                              ? stats.lastAnsweredAt.substr(0, 10)
                                              : std::string("-")}}),
                   false}});

        // ⑤ 最近 7 天：小柱状图
        ImGui::Spacing();
        ImGui::TextDisabled("%s", texts.text("ui.stats.recent"));
        const std::vector<std::pair<std::string, int>> days = stats.recentDays(7, today);
        int maxCount = 1;
        for (const auto& day : days) {
            maxCount = std::max(maxCount, day.second);
        }
        const float barsHeight = ImGui::GetFontSize() * 3.4f;
        if (cardBegin("stats_chart", cardPadding() * 2.0f + barsHeight + lineH + 16.0f)) {
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            const int count = static_cast<int>(days.size());
            const float slot = count > 0 ? width / static_cast<float>(count) : width;
            ImDrawList* const draw = ImGui::GetWindowDrawList();
            for (int k = 0; k < count; ++k) {
                const std::pair<std::string, int>& item = days[static_cast<size_t>(k)];
                const float ratio =
                    static_cast<float>(item.second) / static_cast<float>(maxCount);
                const float barWidth = std::max(slot * 0.42f, 8.0f);
                const float x = origin.x + slot * static_cast<float>(k) + (slot - barWidth) * 0.5f;
                const float barHeight = std::max(barsHeight * ratio, item.second > 0 ? 4.0f : 2.0f);
                const float y = origin.y + barsHeight - barHeight;
                draw->AddRectFilled(ImVec2(x, y), ImVec2(x + barWidth, y + barHeight),
                                    item.second > 0 ? accentColor : surfaceColor(0.30f), 4.0f);
                if (item.second > 0) {
                    const std::string value = std::to_string(item.second);
                    const float textW = ImGui::CalcTextSize(value.c_str()).x;
                    draw->AddText(ImVec2(x + (barWidth - textW) * 0.5f, y - lineH - 1.0f),
                                  ImGui::GetColorU32(ImGuiCol_Text), value.c_str());
                }
                const std::string label =
                    item.first.size() >= 10 ? item.first.substr(5) : item.first;
                const float labelW = ImGui::CalcTextSize(label.c_str()).x;
                draw->AddText(ImVec2(x + (barWidth - labelW) * 0.5f, origin.y + barsHeight + 2.0f),
                              ImGui::GetColorU32(ImGuiCol_TextDisabled), label.c_str());
            }
            ImGui::Dummy(ImVec2(width, barsHeight + lineH + 6.0f));
        }
        cardEnd();

        // ⑥ 题库明细：点一行直接开始刷那个题库
        ImGui::Spacing();
        ImGui::TextDisabled("%s", texts.text("ui.stats.detail"));
        const auto& statsBanks = context_.banks();
        for (int i = 0; i < static_cast<int>(statsBanks.size()); ++i) {
            const BankEntry& entry = statsBanks[static_cast<size_t>(i)];
            int answered = 0;
            int wrong = 0;
            for (const QuestionProgress& item : entry.progress.questions) {
                if (!item.answered) {
                    continue;
                }
                ++answered;
                if (!item.correct) {
                    ++wrong;
                }
            }
            const int total = static_cast<int>(entry.bank.questions.size());
            std::string status = texts.text("ui.stats.status_new");
            if (total > 0 && answered >= total && wrong == 0) {
                status = texts.text("ui.stats.status_done");
            } else if (answered > 0) {
                status = texts.text("ui.stats.status_doing");
            }
            const std::string right =
                texts.format("ui.stats.bank_progress",
                             {{"answered", std::to_string(answered)}, {"total", std::to_string(total)}}) +
                " · " + status;
            const float rowHeight = controlHeight() * 0.9f;
            const ImVec2 rowOrigin = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            const ImVec2 rowFar(rowOrigin.x + width, rowOrigin.y + rowHeight);
            ImDrawList* const draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(rowOrigin, rowFar, surfaceColor(0.14f), cornerRadius() * 0.7f);
            draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                          ImVec2(rowOrigin.x + cardPadding() * 0.8f,
                                 rowOrigin.y + (rowHeight - lineH) * 0.5f),
                          ImGui::GetColorU32(ImGuiCol_Text), entry.bank.meta.title.c_str());
            const float rightW = ImGui::CalcTextSize(right.c_str()).x;
            draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                          ImVec2(rowOrigin.x + width - cardPadding() * 0.8f - rightW,
                                 rowOrigin.y + (rowHeight - lineH) * 0.5f),
                          ImGui::GetColorU32(ImGuiCol_TextDisabled), right.c_str());
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##bank_row", ImVec2(width, rowHeight + 6.0f))) {
                selectedBank_ = i;
                // 统计页的明细按「设置 → 答题 → 刷题顺序」来，和首页模式分开
                context_.openBank(i, AppContext::OrderSource::Stats);
                screen_ = Screen::Quiz;
            }
            if (ImGui::IsItemActive()) {
                draw->AddRectFilled(rowOrigin, rowFar, IM_COL32(255, 255, 255, 20),
                                    cornerRadius() * 0.7f);
            }
            ImGui::PopID();
        }

        // ⑦ 重置统计：点一次变成「再点一次就清零」，避免误触
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        const char* resetLabel = texts.text(statsResetArmed_ ? "ui.stats.reset_confirm" : "ui.stats.reset");
        if (ImGui::Button(resetLabel, ImVec2(-1.0f, controlHeight()))) {
            if (statsResetArmed_) {
                context_.resetStats();
                statsResetArmed_ = false;
                context_.setMessage(texts.text("ui.stats.reset_done"));
            } else {
                statsResetArmed_ = true;
            }
        }
        ImGui::Spacing();
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// 设置页：具体界面在 SettingsScreen（ui/src/SettingsScreen.cpp）
// ---------------------------------------------------------------------------
void UiApp::drawSettings() {
    const SettingsScreen::Result result = settingsScreen_.draw();
    if (result.fontDirty) {
        fontDirty_ = true;
    }
    if (result.cancelAutoNext) {
        cancelAutoNext();
    }
    if (result.leaveToHome) {
        screen_ = Screen::Home;
    }
}

// ---------------------------------------------------------------------------
// 「答对自动跳下一题」与「单选即提交」
// ---------------------------------------------------------------------------
/// 背题模式：把当前这一页（材料题是一组小题）的答案亮出来。
/// 顺便把「以前没做对过的」记成错题，所以要立刻落盘。
void UiApp::applyReciteIfNeeded() {
    QuizSession& session = context_.session();
    if (!session.isReciteMode() || !session.valid()) {
        return;
    }
    const std::string now = platform::nowString();
    bool changed = false;
    const std::vector<int> group = session.materialGroupOf(session.currentIndex());
    if (group.empty()) {
        changed = session.reciteRevealAt(session.currentIndex(), now);
    } else {
        for (int index : group) {
            changed = session.reciteRevealAt(index, now) || changed;
        }
    }
    if (changed) {
        context_.saveProgress();
    }
}

void UiApp::submitCurrent() {
    QuizSession& session = context_.session();
    if (session.submitted() || session.isExamMode()) {
        // 考试模式没有「逐题提交」：答案都先留在草稿里，交卷时一起判分
        return;
    }
    session.submit(platform::nowString());
    context_.saveProgress();   // 每答一题就落盘，断电也不丢

    // 答对了、而且不是最后一题 → 按设置里的停顿时间安排自动跳转
    const int index = session.currentIndex();
    // 「还有下一题」要看本轮的练习顺序：乱序 / 只刷错题时，题号 +1 并不代表还有下一题
    const bool hasNext = session.hasNextInPlan();
    if (context_.settings().autoNextOnCorrect && session.isCorrect() && hasNext) {
        autoNextTimer_ = context_.settings().autoNextDelay();
        autoNextIndex_ = index;
    } else {
        cancelAutoNext();
    }
    checkRoundFinished();   // 这一轮做完了就去结算页
}

void UiApp::tickAutoNext() {
    if (autoNextTimer_ <= 0.0f) {
        return;
    }
    QuizSession& session = context_.session();
    // 已经换了题（手动翻页 / 跳转 / 重做错题）就作废这次倒计时
    if (!session.submitted() || session.currentIndex() != autoNextIndex_) {
        cancelAutoNext();
        return;
    }
    autoNextTimer_ -= frameDelta_;
    if (autoNextTimer_ > 0.0f) {
        return;
    }
    cancelAutoNext();
    // 按本轮练习顺序翻页（乱序 / 只刷错题时，题号 +1 是错的）
    if (session.hasNextInPlan()) {
        session.next();
    }
}

void UiApp::cancelAutoNext() {
    autoNextTimer_ = 0.0f;
    autoNextIndex_ = -1;
}

// ---------------------------------------------------------------------------
// 标签弹窗：题库自带的标签只读，用户自己加的可以删
// ---------------------------------------------------------------------------
void UiApp::drawTagPopup() {
    const i18n::I18n& texts = context_.i18n();
    QuizSession& session = context_.session();
    const int index = tagTargetIndex_;
    ImGui::TextDisabled("%s", texts.text("ui.quiz.tags"));
    const std::vector<std::string> allTags =
        index >= 0 ? session.tagsAt(index) : std::vector<std::string>();
    if (!allTags.empty()) {
        tagChips(allTags);
    }
    ImGui::Spacing();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    const bool entered = ImGui::InputText("##tag_input", tagBuffer_, sizeof(tagBuffer_),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    const bool addPressed = ImGui::Button(texts.text("ui.common.add"));
    if ((entered || addPressed) && index >= 0) {
        session.addTagAt(index, std::string(tagBuffer_));
        tagBuffer_[0] = '\0';
        context_.saveProgress();
    }
    ImGui::TextDisabled("%s", texts.text("ui.quiz.tag_hint"));
    ImGui::Spacing();
    ImGui::TextDisabled("%s", texts.text("ui.quiz.tag_mine"));
    std::vector<std::string> mine;
    if (index >= 0) {
        mine = session.userTagsAt(index);
    }
    if (mine.empty()) {
        ImGui::TextDisabled("%s", texts.text("ui.quiz.tag_empty"));
    }
    for (const std::string& tag : mine) {
        ImGui::PushID(tag.c_str());
        const ImU32 background = tagColorOf(tag);
        chip(tag, background, contrastText(background));
        ImGui::SameLine();
        if (ImGui::SmallButton(texts.text("ui.quiz.tag_remove"))) {
            session.removeTagAt(index, tag);
            context_.saveProgress();
        }
        ImGui::PopID();
    }
    ImGui::Spacing();
    if (ImGui::Button(texts.text("ui.common.close"), ImVec2(140.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
    }
}

// ---------------------------------------------------------------------------
// 「按标签导出」弹窗：标签可多选（或的关系），导出前问一句要不要固化用户标签
// ---------------------------------------------------------------------------
void UiApp::drawTagExportPopup() {
    const i18n::I18n& texts = context_.i18n();
    ImGui::TextUnformatted(texts.text("ui.export.tags_title"));
    ImGui::TextDisabled("%s", texts.text("ui.export.tags_pick"));
    if (context_.activeBank() < 0) {
        ImGui::TextDisabled("%s", texts.text("ui.export.tags_no_bank"));
    } else if (tagExportList_.empty()) {
        ImGui::TextDisabled("%s", texts.text("ui.export.tags_none"));
    }

    int checked = static_cast<int>(std::count(tagExportChecked_.begin(), tagExportChecked_.end(),
                                              static_cast<char>(1)));
    if (!tagExportList_.empty()) {
        const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
        const float listHeight =
            std::min(rowHeight * static_cast<float>(tagExportList_.size() + 1), rowHeight * 10.0f);
        if (ImGui::BeginChild("tag_export_list",
                              ImVec2(ImGui::GetFontSize() * 18.0f, listHeight),
                              ImGuiChildFlags_Borders)) {
            // 标签多时弹窗内列表会滚动；每个标签是 Checkbox，点框仍然勾选，
            // 只有从文字右侧空白处拖动才滚列表。
            ScrollDrag scrollDrag;
            for (size_t i = 0; i < tagExportList_.size(); ++i) {
                bool on = i < tagExportChecked_.size() && tagExportChecked_[i] != 0;
                const std::string label =
                    tagExportList_[i].name + "   (" + std::to_string(tagExportList_[i].count) + ")";
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Checkbox(label.c_str(), &on) && i < tagExportChecked_.size()) {
                    tagExportChecked_[i] = on ? 1 : 0;
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }

    ImGui::Spacing();
    ImGui::Checkbox(texts.text("ui.export.tags_bake"), &tagExportBake_);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
    ImGui::TextWrapped("%s", texts.text("ui.export.tags_bake_hint"));
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::Spacing();

    ImGui::BeginDisabled(checked == 0);
    if (ImGui::Button(texts.text("ui.export.tags_go"), ImVec2(140.0f, 0.0f))) {
        std::vector<std::string> selected;
        for (size_t i = 0; i < tagExportList_.size(); ++i) {
            if (i < tagExportChecked_.size() && tagExportChecked_[i] != 0) {
                selected.push_back(tagExportList_[i].name);
            }
        }
        const std::string suggested =
            platform::joinPath(context_.exportDirectory(), context_.suggestedTaggedName());
        if (pendingDialog_ != PendingDialog::None) {
            // 已经有一个系统对话框在等着了：先不动
        } else if (!platform::beginPickSavePath(texts.text("ui.export.save_title"), suggested,
                                                std::vector<std::string>{".quiz"})) {
            context_.setMessage(context_.i18n().format(
                "ui.export.failed", {{"reason", context_.i18n().text("ui.export.failed_pick")}}));
        } else {
            pendingTags_ = selected;
            pendingBake_ = tagExportBake_;
            pendingDialog_ = PendingDialog::ExportTags;
        }
        ImGui::CloseCurrentPopup();   // 交给系统对话框之后就收起来，结果用提示条显示
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(texts.text("ui.common.cancel"), ImVec2(120.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
    }
}

// ---------------------------------------------------------------------------
// 界面比例
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// 考试模式：选卷 / 交卷 / 成绩页
// ---------------------------------------------------------------------------
/// 首页点了「开始考试」：先把每套卷抽一遍（只为显示题数、满分与抽题提示），再弹选卷框
void UiApp::openExamPicker() {
    examPreviews_.clear();
    const int index = context_.activeBank();
    if (index < 0 || index >= static_cast<int>(context_.banks().size())) {
        return;
    }
    const QuestionBank& bank = context_.banks()[static_cast<size_t>(index)].bank;
    for (const ExamSpec& spec : bank.exams) {
        // 预览用卷里写的 seed（写了就是同一套题；没写就用 0，只影响预览、不影响真正开考）
        examPreviews_.push_back(io::buildExamPaper(bank, spec, spec.seed));
    }
    openExamPicker_ = true;
}

void UiApp::drawExamPicker() {
    const i18n::I18n& texts = context_.i18n();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(toImU32(colorAccentRgb_)), "%s",
                       texts.text("ui.exam.pick_title"));
    const int index = context_.activeBank();
    const bool hasBank = index >= 0 && index < static_cast<int>(context_.banks().size());
    const QuestionBank* bank = hasBank ? &context_.banks()[static_cast<size_t>(index)].bank : nullptr;
    if (bank == nullptr || bank->exams.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetFontSize() * 21.0f);
        ImGui::TextWrapped("%s", texts.text("ui.exam.no_paper"));
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button(texts.text("ui.common.close"), ImVec2(140.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        return;
    }

    ImGui::Separator();
    int chosen = -1;
    for (size_t i = 0; i < bank->exams.size(); ++i) {
        const ExamSpec& spec = bank->exams[i];
        const io::ExamPaper* preview = i < examPreviews_.size() ? &examPreviews_[i] : nullptr;
        const int count = preview != nullptr ? static_cast<int>(preview->items.size()) : 0;
        const int total = preview != nullptr ? preview->totalScore() : 0;
        const std::string line =
            preview != nullptr && preview->minutes > 0
                ? texts.format("ui.exam.paper_line",
                               {{"count", std::to_string(count)},
                                {"score", std::to_string(total)},
                                {"minutes", std::to_string(preview->minutes)}})
                : texts.format("ui.exam.paper_line_no_time",
                               {{"count", std::to_string(count)}, {"score", std::to_string(total)}});
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable(spec.title.c_str(), false, 0, ImVec2(0.0f, controlHeight() * 0.9f))) {
            chosen = static_cast<int>(i);
        }
        // 规格另起一行并自动换行：手机上「卷名 + 8 题 · 满分 17 分 · 限时 20 分钟」挤一行会被截掉右边
        const float examWrap = ImGui::GetFontSize() * 21.0f;
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + examWrap);
        ImGui::TextDisabled("%s", line.c_str());
        ImGui::PopTextWrapPos();
        if (!spec.desc.empty()) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + examWrap);
            ImGui::TextWrapped("%s", spec.desc.c_str());
            ImGui::PopTextWrapPos();
        }
        // 抽不满这类提示：让用户开考前就知道（不是错误，题照样够做）
        if (preview != nullptr) {
            for (const io::Issue& issue : preview->issues) {
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + examWrap);
                ImGui::TextWrapped("⚠ %s", issue.message.c_str());
                ImGui::PopTextWrapPos();
            }
        }
        ImGui::Spacing();
        ImGui::PopID();
    }
    ImGui::Spacing();
    if (ImGui::Button(texts.text("ui.common.close"), ImVec2(140.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
    }
    if (chosen >= 0) {
        // 先到准备页看一眼规格与规则，点「开始答题」才真正开考（倒计时也从那一刻起算）
        pendingExam_ = chosen;
        ImGui::CloseCurrentPopup();
        screen_ = Screen::ExamReady;
    }
}

/// 「确定交卷吗」：交卷之后就判分写进度了，所以先问一句
void UiApp::drawExamSubmitAsk() {
    const i18n::I18n& texts = context_.i18n();
    QuizSession& session = context_.session();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(toImU32(colorAccentRgb_)), "%s",
                       texts.text("ui.exam.submit"));
    int blank = 0;
    for (int index : session.order()) {
        if (!session.isExamAnsweredAt(index)) {
            ++blank;
        }
    }
    if (blank > 0) {
        ImGui::TextWrapped("%s",
                           texts.format("ui.exam.submit_ask", {{"count", std::to_string(blank)}})
                               .c_str());
    } else {
        ImGui::TextWrapped("%s", texts.text("ui.exam.submit_ask_done"));
    }
    ImGui::Spacing();
    if (primaryButton(texts.text("ui.exam.submit"), ImVec2(160.0f, controlHeight()), colorAccentRgb_)) {
        ImGui::CloseCurrentPopup();
        finishExamNow();
    }
    ImGui::SameLine();
    if (ImGui::Button(texts.text("ui.exam.keep_going"), ImVec2(160.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
    }
}

/// 交卷：统一判分（错题进本、累计统计各算一次）→ 切到成绩页
void UiApp::finishExamNow() {
    if (!context_.examActive()) {
        return;
    }
    cancelAutoNext();
    context_.finishExam();
    screen_ = Screen::ExamResult;
}

// ---------------------------------------------------------------------------
// 考试成绩页：分数 / 及格与否 / 逐题对错（点一道题回看判定与解析）
// ---------------------------------------------------------------------------
void UiApp::drawExamResult() {
    const i18n::I18n& texts = context_.i18n();
    const ExamSummary& summary = context_.examSummary();
    const ImU32 accentColor = toImU32(colorAccentRgb_);

    if (iconButton("##exam_back", "icons/home.svg", texts.text("ui.exam.home"))) {
        screen_ = Screen::Home;
    }
    ImGui::SameLine();
    ImGui::Text("%s", texts.text("ui.exam.result_title"));
    if (!summary.title.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("｜%s", summary.title.c_str());
    }

    // ---- 分数卡片 ----
    const std::string earned = trimScoreText(summary.earnedScore);
    const std::string scoreLine = texts.format(
        "ui.exam.score_line", {{"earned", earned}, {"total", std::to_string(summary.totalScore)}});
    char rateText[16];
    std::snprintf(rateText, sizeof(rateText), "%.0f%%",
                  static_cast<double>(summary.accuracy()) * 100.0);
    const std::string summaryLine =
        texts.format("ui.exam.summary",
                     {{"correct", std::to_string(summary.correctCount)},
                      {"count", std::to_string(summary.questionCount)},
                      {"rate", rateText},
                      {"time", formatDuration(summary.usedSeconds, texts)}});
    const float innerWidth = std::max(ImGui::GetContentRegionAvail().x - cardPadding() * 2.0f, 80.0f);
    float cardHeight = cardPadding() * 2.0f + ImGui::GetTextLineHeight() * 2.0f + 8.0f + 12.0f +
                       ImGui::GetStyle().ItemSpacing.y * 3.0f +
                       wrappedTextHeight(summaryLine, innerWidth);
    ImGui::Spacing();
    if (cardBegin("exam_score", cardHeight)) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(accentColor));
        ImGui::Text("%s", scoreLine.c_str());
        ImGui::PopStyleColor();
        ImGui::ProgressBar(summary.percent(), ImVec2(-1.0f, 8.0f), "");
        if (summary.hasPass) {
            ImGui::TextColored(
                ImGui::ColorConvertU32ToFloat4(
                    toImU32(summary.passed ? colorCorrectRgb_ : colorWrongRgb_)),
                "%s", texts.text(summary.passed ? "ui.exam.pass" : "ui.exam.fail"));
        } else {
            ImGui::TextDisabled("%s", texts.text("ui.exam.no_pass_line"));
        }
        ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
        ImGui::TextDisabled("%s", summaryLine.c_str());
        ImGui::PopTextWrapPos();
    }
    cardEnd();

    // ---- 逐题：得分 + 对错 / 未作答；点一下回看那道题 ----
    ImGui::Spacing();
    ImGui::TextDisabled("%s", texts.text("ui.exam.review_hint"));
    const int bankIndex = context_.activeBank();
    const QuestionBank* bank = nullptr;
    if (bankIndex >= 0 && bankIndex < static_cast<int>(context_.banks().size())) {
        bank = &context_.banks()[static_cast<size_t>(bankIndex)].bank;
    }
    int jumpIndex = -1;
    // 底下两个按钮要占一行，成绩列表多留一点（否则最后一行会被按钮压住）
    const float listHeight = std::max(ImGui::GetContentRegionAvail().y - 92.0f, 120.0f);
    if (bank != nullptr) {
        ImGui::BeginChild("exam_list", ImVec2(0.0f, listHeight), ImGuiChildFlags_Borders);
        for (const ExamLine& line : summary.lines) {
            ImGui::PushID(line.position);
            const ImU32 color = !line.answered ? ImGui::GetColorU32(ImGuiCol_TextDisabled)
                                               : toImU32(line.correct ? colorCorrectRgb_ : colorWrongRgb_);
            const char* statusKey = !line.answered
                                        ? "ui.exam.line_unanswered"
                                        : (line.correct ? "ui.exam.line_correct" : "ui.exam.line_wrong");
            const std::string lineScore =
                texts.format("ui.exam.line_score",
                             {{"got", trimScoreText(line.got)}, {"score", std::to_string(line.score)}});
            ImGui::TextDisabled("%d.", line.position);
            ImGui::SameLine();
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color), "%s", lineScore.c_str());
            ImGui::SameLine();
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color), "%s", texts.text(statusKey));
            std::string stem;
            if (line.index >= 0 && line.index < static_cast<int>(bank->questions.size())) {
                stem = bank->questions[static_cast<size_t>(line.index)].stem;
            }
            if (!stem.empty()) {
                // Selectable 不会换行：先按可用宽度截一下，免得长题干被硬切掉后半截
                const float stemLimit = ImGui::GetContentRegionAvail().x - 8.0f;
                std::string label = "   " + stem;
                if (ImGui::CalcTextSize(label.c_str()).x > stemLimit) {
                    label = "   " + ellipsizeToWidth(stem, stemLimit);
                }
                if (ImGui::Selectable(label.c_str(), false, 0, ImVec2(0.0f, controlHeight() * 0.7f))) {
                    jumpIndex = line.index;
                }
            }
            ImGui::Spacing();
            ImGui::PopID();
        }
        // 考试成绩列表可能比屏幕长；这里没有额外 item 包裹整行，所以从任何非控件
        // 区域拖动都能滚。必须在 EndChild() 之前调用，滚动目标才是 exam_list。
        dragScrollInsideChild();
        ImGui::EndChild();
    }
    if (jumpIndex >= 0) {
        cancelAutoNext();
        context_.session().jumpTo(jumpIndex);
        screen_ = Screen::Quiz;   // 交过卷的题会把判定与解析都显示出来
    }

    const float navWidth =
        (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (primaryButton(texts.text("ui.exam.home"), ImVec2(navWidth, controlHeight()),
                      colorAccentRgb_)) {
        screen_ = Screen::Home;
    }
    ImGui::SameLine();
    if (ImGui::Button(texts.text("ui.result.export_wrong"), ImVec2(navWidth, 0.0f))) {
        beginExport(AppContext::ExportKind::WrongOnly);
    }
}// ---------------------------------------------------------------------------
// 考试准备页：选完卷先看规格与规则，点「开始答题」才真正开考（倒计时也从那一刻起算）
// ---------------------------------------------------------------------------
void UiApp::drawExamReady() {
    const i18n::I18n& texts = context_.i18n();
    const int bankIndex = context_.activeBank();
    const std::vector<ExamSpec>& exams = context_.activeBankExams();
    if (bankIndex < 0 || bankIndex >= static_cast<int>(context_.banks().size()) ||
        pendingExam_ < 0 || pendingExam_ >= static_cast<int>(exams.size())) {
        screen_ = Screen::Home;
        return;
    }
    const ExamSpec& spec = exams[static_cast<size_t>(pendingExam_)];
    // 预览一遍拿题数 / 满分（写了 seed 的卷，预览和真正开考抽的是同一套）
    const io::ExamPaper preview =
        io::buildExamPaper(context_.banks()[static_cast<size_t>(bankIndex)].bank, spec, spec.seed);

    if (iconButton("##exam_ready_back", "icons/home.svg", texts.text("ui.exam.prep_back"))) {
        screen_ = Screen::Home;
        return;
    }
    ImGui::SameLine();
    ImGui::Text("%s", texts.text("ui.exam.prep_title"));

    ImGui::Spacing();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(toImU32(colorAccentRgb_)));
    ImGui::TextWrapped("%s", spec.title.c_str());
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();

    const std::string line =
        preview.minutes > 0
            ? texts.format("ui.exam.paper_line",
                           {{"count", std::to_string(preview.items.size())},
                            {"score", std::to_string(preview.totalScore())},
                            {"minutes", std::to_string(preview.minutes)}})
            : texts.format("ui.exam.paper_line_no_time",
                           {{"count", std::to_string(preview.items.size())},
                            {"score", std::to_string(preview.totalScore())}});
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    ImGui::TextDisabled("%s", line.c_str());
    if (preview.passPercent >= 0) {
        ImGui::TextDisabled(
            "%s", texts.format("ui.exam.prep_pass", {{"pass", std::to_string(preview.passPercent)}})
                      .c_str());
    } else {
        ImGui::TextDisabled("%s", texts.text("ui.exam.no_pass_line"));
    }
    ImGui::PopTextWrapPos();

    // 规则卡片（和成绩页同款）
    const std::string rules = texts.text("ui.exam.prep_rules");
    const std::string autoHint =
        preview.minutes > 0 ? texts.text("ui.exam.prep_auto") : std::string();
    const float innerWidth = std::max(ImGui::GetContentRegionAvail().x - cardPadding() * 2.0f, 80.0f);
    float cardHeight = cardPadding() * 2.0f + 8.0f + wrappedTextHeight(rules, innerWidth) +
                       ImGui::GetStyle().ItemSpacing.y * 2.0f;
    if (!autoHint.empty()) {
        cardHeight += wrappedTextHeight(autoHint, innerWidth) + ImGui::GetStyle().ItemSpacing.y;
    }
    ImGui::Spacing();
    if (cardBegin("exam_ready_card", cardHeight)) {
        ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
        ImGui::TextWrapped("%s", rules.c_str());
        if (!autoHint.empty()) {
            ImGui::TextDisabled("%s", autoHint.c_str());
        }
        ImGui::PopTextWrapPos();
    }
    cardEnd();

    // 抽题提示（抽不满之类）也给看一眼
    for (const io::Issue& issue : preview.issues) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::TextDisabled("⚠ %s", issue.message.c_str());
        ImGui::PopTextWrapPos();
    }

    // 按钮贴底
    const float footerHeight = controlHeight() * 2.0f + ImGui::GetStyle().ItemSpacing.y * 2.0f;
    const float gap = ImGui::GetContentRegionAvail().y - footerHeight;
    if (gap > 0.0f) {
        ImGui::Dummy(ImVec2(0.0f, gap));
    }
    if (primaryButton(texts.text("ui.exam.prep_start"), ImVec2(-1.0f, controlHeight()),
                      colorAccentRgb_)) {
        std::string error;
        if (context_.startExam(pendingExam_, error)) {
            cancelAutoNext();
            screen_ = Screen::Quiz;
        } else {
            context_.setMessage(error);
        }
    }
    if (ImGui::Button(texts.text("ui.exam.prep_back"), ImVec2(-1.0f, controlHeight()))) {
        screen_ = Screen::Home;
    }
}
void UiApp::computeContentRect(float& outX, float& outY, float& outWidth, float& outHeight) const {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    outX = 0.0f;
    outY = 0.0f;
    outWidth = display.x;
    outHeight = display.y;

    int aspectWidth = 0;
    int aspectHeight = 0;
    if (!context_.settings().uiAspectRatio(aspectWidth, aspectHeight)) {
        return;   // 跟随窗口
    }

    // 取最大居中内接矩形：内容区严格保持这个比例
    const float target = static_cast<float>(aspectWidth) / static_cast<float>(aspectHeight);
    float width = display.x;
    float height = width / target;
    if (height > display.y) {
        height = display.y;
        width = height * target;
    }
    outWidth = std::floor(width);
    outHeight = std::floor(height);
    outX = std::floor((display.x - outWidth) * 0.5f);
    outY = std::floor((display.y - outHeight) * 0.5f);
}


void UiApp::applyTheme() {
    const Settings& s = context_.settings();
    unsigned int rgb = 0;

    const ImU32 bg = Settings::parseHexColor(s.uiColorBackground, rgb) ? toImU32(rgb) : IM_COL32(15, 23, 28, 255);
    const ImU32 panel = Settings::parseHexColor(s.uiColorPanel, rgb) ? toImU32(rgb) : IM_COL32(23, 31, 36, 255);
    const ImU32 text = Settings::parseHexColor(s.uiColorText, rgb) ? toImU32(rgb) : IM_COL32(235, 245, 240, 255);
    const ImU32 button = Settings::parseHexColor(s.uiColorButton, rgb) ? toImU32(rgb) : IM_COL32(41, 92, 74, 255);
    colorAccentRgb_ = Settings::parseHexColor(s.uiColorAccent, rgb) ? rgb : 0x96DCB4;
    colorCorrectRgb_ = Settings::parseHexColor(s.uiColorCorrect, rgb) ? rgb : 0x78DCA0;
    colorWrongRgb_ = Settings::parseHexColor(s.uiColorWrong, rgb) ? rgb : 0xF08C82;
    const ImU32 accent = toImU32(colorAccentRgb_);

    ImVec4* colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_WindowBg] = ImGui::ColorConvertU32ToFloat4(bg);
    colors[ImGuiCol_ChildBg] = ImGui::ColorConvertU32ToFloat4(panel);
    colors[ImGuiCol_PopupBg] = ImGui::ColorConvertU32ToFloat4(panel);
    const ImU32 input = Settings::parseHexColor(s.uiColorInput, rgb) ? toImU32(rgb) : shadeColor(panel, 0.30f);
    colors[ImGuiCol_FrameBg] = ImGui::ColorConvertU32ToFloat4(input);
    colors[ImGuiCol_FrameBgHovered] = ImGui::ColorConvertU32ToFloat4(shadeColor(input, 0.25f));
    colors[ImGuiCol_FrameBgActive] = ImGui::ColorConvertU32ToFloat4(shadeColor(input, 0.40f));
    colors[ImGuiCol_Button] = ImGui::ColorConvertU32ToFloat4(button);
    colors[ImGuiCol_ButtonHovered] = ImGui::ColorConvertU32ToFloat4(shadeColor(button, 0.35f));
    colors[ImGuiCol_ButtonActive] = ImGui::ColorConvertU32ToFloat4(shadeColor(button, 0.60f));
    colors[ImGuiCol_Header] = ImGui::ColorConvertU32ToFloat4(button);
    colors[ImGuiCol_HeaderHovered] = ImGui::ColorConvertU32ToFloat4(shadeColor(button, 0.35f));
    colors[ImGuiCol_HeaderActive] = ImGui::ColorConvertU32ToFloat4(shadeColor(button, 0.60f));
    colors[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4(text);
    colors[ImGuiCol_TextDisabled] =
        ImGui::ColorConvertU32ToFloat4(Settings::parseHexColor(s.uiColorTextMuted, rgb)
                                            ? toImU32(rgb)
                                            : shadeColor(text, -0.45f));
    // 控件：次按钮（列表 / 下拉 / 选中行）和进度条各有自己的颜色
    if (Settings::parseHexColor(s.uiColorButton2, rgb)) {
        const ImU32 button2 = toImU32(rgb);
        colors[ImGuiCol_Header] = ImGui::ColorConvertU32ToFloat4(button2);
        colors[ImGuiCol_HeaderHovered] = ImGui::ColorConvertU32ToFloat4(shadeColor(button2, 0.35f));
        colors[ImGuiCol_HeaderActive] = ImGui::ColorConvertU32ToFloat4(shadeColor(button2, 0.60f));
    }
    colorPartialRgb_ = Settings::parseHexColor(s.uiColorPartial, rgb) ? rgb : 0xE8C46A;
    colorStarRgb_ = Settings::parseHexColor(s.uiColorStar, rgb) ? rgb : 0xF2C14E;
    colorTextAccentRgb_ = Settings::parseHexColor(s.uiColorTextAccent, rgb) ? rgb : colorAccentRgb_;
    colorButtonTextRgb_ = Settings::parseHexColor(s.uiColorButtonText, rgb) ? rgb : 0x16181C;
    colorIconRgb_ = Settings::parseHexColor(s.uiColorIcon, rgb) ? rgb : colorAccentRgb_;
    colorDraftRgb_ = Settings::parseHexColor(s.uiColorDraft, rgb) ? rgb : colorPartialRgb_;
    g_iconRgb = toImU32(colorIconRgb_);
    g_buttonTextRgb = toImU32(colorButtonTextRgb_);
    colors[ImGuiCol_CheckMark] = ImGui::ColorConvertU32ToFloat4(accent);
    colors[ImGuiCol_SliderGrab] = ImGui::ColorConvertU32ToFloat4(accent);
    ImVec4 border = ImGui::ColorConvertU32ToFloat4(shadeColor(panel, 0.55f));
    border.w = 0.60f;
    colors[ImGuiCol_Border] = border;
    ImVec4 separator = ImGui::ColorConvertU32ToFloat4(shadeColor(button, 0.45f));
    separator.w = 0.80f;
    colors[ImGuiCol_Separator] = separator;
}

}  // namespace quiz::ui

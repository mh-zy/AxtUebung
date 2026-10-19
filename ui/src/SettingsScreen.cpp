#include "quiz/ui/SettingsScreen.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "imgui.h"
#include "quiz/app/Version.h"
#include "quiz/platform/Platform.h"
#include "quiz/render/ImGuiRenderer.h"
#include "quiz/ui/UiScroll.h"

namespace quiz::ui {

namespace {

/// 0xRRGGBB → ImU32（不透明），配色预览用
ImU32 toImU32(unsigned int rgb) {
    return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255);
}

/// 灰色说明文字：按可用宽度自动换行（窄窗口下不会被右边界裁掉）
void wrappedHint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
    ImGui::TextWrapped("%s", text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

}  // namespace

SettingsScreen::SettingsScreen(AppContext& context) : context_(context) {}

const char* SettingsScreen::titleKeyFor(Page page) {
    switch (page) {
        case Page::Answering: return "ui.settings.page_answering";
        case Page::Interface: return "ui.settings.page_interface";
        case Page::Theme: return "ui.settings.page_theme";
        case Page::Prompt: return "ui.settings.page_prompt";
        case Page::Plugins: return "ui.settings.page_plugins";
        case Page::Root: break;
    }
    return "ui.settings.title";
}

bool SettingsScreen::drawSubHeader(Page page) {
    if (ImGui::Button(context_.i18n().text("ui.settings.back"), ImVec2(120.0f, 0.0f))) {
        page_ = Page::Root;
        return true;
    }
    ImGui::SameLine();
    ImGui::Text("%s", context_.i18n().text(titleKeyFor(page)));
    ImGui::Separator();
    return false;
}

SettingsScreen::Result SettingsScreen::draw() {
    Result result;

    if (page_ == Page::Root) {
        // 「返回主页」用首页那个房子图标（和刷题页 / 结算页 / 统计页的返回保持一致）
        const std::string homeLabel = context_.i18n().text("ui.result.back_home");
        const bool backHome = iconButton_ ? iconButton_("##settings_home", "icons/home.svg", homeLabel)
                                          : ImGui::Button(homeLabel.c_str(), ImVec2(160.0f, 0.0f));
        if (backHome) {
            result.leaveToHome = true;
        }
        ImGui::SameLine();
        ImGui::Text("%s", context_.i18n().text("ui.settings.title"));
        ImGui::Separator();
    } else if (drawSubHeader(page_)) {
        return result;   // 这一帧刚点了返回，先不画内容
    }

    switch (page_) {
        case Page::Root: drawRoot(); break;
        case Page::Answering: drawAnswering(result); break;
        case Page::Interface: drawInterface(result); break;
        case Page::Theme: drawTheme(); break;
        case Page::Prompt: drawPrompt(); break;
        case Page::Plugins:
            wrappedHint(context_.i18n().text("ui.settings.not_implemented"));
            break;
    }
    return result;
}

void SettingsScreen::drawRoot() {
    struct Entry {
        const char* labelKey;
        Page page;
    };
    const Entry entries[] = {
        {"ui.settings.page_answering", Page::Answering},
        {"ui.settings.page_interface", Page::Interface},
        {"ui.settings.page_theme", Page::Theme},
        {"ui.settings.page_prompt", Page::Prompt},
        {"ui.settings.page_plugins", Page::Plugins},
    };
    for (const Entry& entry : entries) {
        const std::string label = std::string(context_.i18n().text(entry.labelKey)) + "   ›";
        if (ImGui::Button(label.c_str(), ImVec2(-1.0f, 0.0f))) {
            page_ = entry.page;
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    const std::string fileHint = context_.i18n().format(
        "ui.settings.file_hint",
        {{"path", platform::joinPath(platform::dataDirectory(), "settings.txt")}});
    wrappedHint(fileHint.c_str());
    // 数据目录与字体路径（原来在首页底部，手机上看太挤，挪到这里）
    const std::string dataHint = context_.i18n().format(
        "ui.settings.data_dir", {{"path", platform::dataDirectory()}});
    wrappedHint(dataHint.c_str());
    // 版本号：设置页根页也显示一下，方便对照手机上的安装包
    wrappedHint(context_.i18n().format("ui.settings.version", {{"version", kAppVersion}}).c_str());
    const std::string fontHint = context_.i18n().format(
        "ui.settings.font_path",
        {{"path", render::usingSystemFont() ? render::fontPath()
                                            : std::string(context_.i18n().text("ui.settings.font_builtin"))}});
    wrappedHint(fontHint.c_str());

    ImGui::Spacing();
    ImGui::TextWrapped("%s", context_.i18n().text("ui.settings.beta_warning"));

    // 设置根页最下方保留一块固定的反馈入口：比藏在“关于”里更容易找到。
    ImGui::Spacing();
    ImGui::Separator();
    unsigned int feedbackAccentRgb = 0;
    if (!quiz::Settings::parseHexColor(context_.settings().uiColorAccent, feedbackAccentRgb)) {
        feedbackAccentRgb = 0x96DCB4;
    }
    const ImU32 feedbackAccent = toImU32(feedbackAccentRgb);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(feedbackAccent), "%s",
                       context_.i18n().text("ui.settings.feedback_title"));
    ImGui::TextWrapped("%s", context_.i18n().text("ui.settings.feedback_hint"));
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(feedbackAccent), "%s",
                       "jytc4399@163.com");
}

void SettingsScreen::drawAnswering(Result& result) {
    bool autoNext = context_.settings().autoNextOnCorrect;
    if (ImGui::Checkbox(context_.i18n().text("ui.settings.auto_next"), &autoNext)) {
        context_.settings().autoNextOnCorrect = autoNext;
        context_.saveSettings();
        if (!autoNext) {
            result.cancelAutoNext = true;   // 刚关掉开关，正在倒计时的这次就别跳了
        }
    }

    // 停留时间：可以直接键入（回车或点一下别处生效）
    ImGui::BeginDisabled(!autoNext);
    ImGui::TextDisabled("%s", context_.i18n().text("ui.settings.auto_next_delay"));
    ImGui::SameLine();
    float delaySeconds = context_.settings().autoNextDelaySeconds;
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputFloat("##auto_next_delay", &delaySeconds, 0.0f, 0.0f, "%.1f",
                      ImGuiInputTextFlags_AutoSelectAll);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        context_.settings().autoNextDelaySeconds =
            std::clamp(delaySeconds, Settings::kMinDelaySeconds, Settings::kMaxDelaySeconds);
        context_.saveSettings();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", context_.i18n().text("ui.settings.auto_next_delay_unit"));
    wrappedHint(context_.i18n().text("ui.settings.auto_next_hint"));
    ImGui::EndDisabled();

    ImGui::Spacing();
    bool autoSubmit = context_.settings().autoSubmitSingle;
    if (ImGui::Checkbox(context_.i18n().text("ui.settings.auto_submit"), &autoSubmit)) {
        context_.settings().autoSubmitSingle = autoSubmit;
        context_.saveSettings();
    }
    wrappedHint(context_.i18n().text("ui.settings.auto_submit_hint"));

    ImGui::Spacing();
    ImGui::TextDisabled("%s", context_.i18n().text("ui.settings.paper_submit"));
    ImGui::SetNextItemWidth(260.0f);
    const bool wholePage = context_.settings().paperSubmitWholePage;
    const char* modeText = wholePage ? context_.i18n().text("ui.settings.paper_submit_whole")
                                     : context_.i18n().text("ui.settings.paper_submit_each");
    if (ImGui::BeginCombo("##paper_submit", modeText)) {
        if (ImGui::Selectable(context_.i18n().text("ui.settings.paper_submit_each"), !wholePage)) {
            context_.settings().paperSubmitWholePage = false;
            context_.saveSettings();
        }
        if (ImGui::Selectable(context_.i18n().text("ui.settings.paper_submit_whole"), wholePage)) {
            context_.settings().paperSubmitWholePage = true;
            context_.saveSettings();
        }
        ImGui::EndCombo();
    }
    wrappedHint(context_.i18n().text("ui.settings.paper_submit_hint"));

    ImGui::Spacing();
    ImGui::TextDisabled("%s", context_.i18n().text("ui.settings.practice_order"));
    ImGui::SetNextItemWidth(260.0f);
    const std::string& order = context_.settings().practiceOrder;
    const int orderIndex = (order == "shuffle") ? 1 : ((order == "wrong") ? 2 : 0);
    const char* orderText = context_.i18n().text(
        orderIndex == 1 ? "ui.settings.practice_order_shuffle"
                        : (orderIndex == 2 ? "ui.settings.practice_order_wrong"
                                           : "ui.settings.practice_order_sequence"));
    if (ImGui::BeginCombo("##practice_order", orderText)) {
        const char* orderKeys[] = {"ui.settings.practice_order_sequence",
                                   "ui.settings.practice_order_shuffle",
                                   "ui.settings.practice_order_wrong"};
        const char* orderValues[] = {"sequence", "shuffle", "wrong"};
        for (int k = 0; k < 3; ++k) {
            if (ImGui::Selectable(context_.i18n().text(orderKeys[k]), k == orderIndex)) {
                context_.settings().practiceOrder = orderValues[k];
                context_.saveSettings();
            }
        }
        ImGui::EndCombo();
    }
    wrappedHint(context_.i18n().text("ui.settings.practice_order_hint"));

    // 错题本的被动移出比例：答对次数达到答错次数的这个倍数，就自动移出错题本
    ImGui::Spacing();
    ImGui::TextDisabled("%s", context_.i18n().text("ui.settings.wrong_book_ratio"));
    ImGui::SameLine();
    int wrongRatio = context_.settings().wrongBookRatio;
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputInt("##wrong_book_ratio", &wrongRatio, 0, 0, ImGuiInputTextFlags_AutoSelectAll);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        context_.settings().wrongBookRatio =
            std::clamp(wrongRatio, Settings::kMinWrongBookRatio, Settings::kMaxWrongBookRatio);
        context_.saveSettings();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", context_.i18n().text("ui.settings.wrong_book_ratio_unit"));
    wrappedHint(context_.i18n().text("ui.settings.wrong_book_ratio_hint"));

    // 权重刷题：每提交 M 题在队尾补抽一道（M 可调，默认 5）
    ImGui::Spacing();
    ImGui::TextDisabled("%s", context_.i18n().text("ui.settings.weighted_append"));
    ImGui::SameLine();
    int appendEvery = context_.settings().weightedAppendEvery;
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputInt("##weighted_append_every", &appendEvery, 0, 0, ImGuiInputTextFlags_AutoSelectAll);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        context_.settings().weightedAppendEvery =
            std::clamp(appendEvery, Settings::kMinWeightedAppend, Settings::kMaxWeightedAppend);
        context_.saveSettings();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", context_.i18n().text("ui.settings.weighted_append_unit"));
    wrappedHint(context_.i18n().text("ui.settings.weighted_append_hint"));
}

void SettingsScreen::drawInterface(Result& result) {
    // ---- 语言 ----
    ImGui::SetNextItemWidth(220.0f);
    const std::string currentName = context_.i18n().languageName();
    if (ImGui::BeginCombo(context_.i18n().text("ui.home.language"), currentName.c_str())) {
        for (const auto& code : context_.availableLanguages()) {
            const std::string name = context_.languageName(code);
            const bool selected = (code == context_.i18n().language());
            if (ImGui::Selectable(name.c_str(), selected)) {
                std::string error;
                if (context_.switchLanguage(code, error)) {
                    context_.setMessage(context_.i18n().format("ui.lang.switched", {{"name", name}}));
                    result.fontDirty = true;   // 换了语言，字形集合要重新生成
                } else {
                    context_.setMessage(error);
                }
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    // ---- 界面比例 ----
    ImGui::Spacing();
    ImGui::Separator();
    const std::string aspectCode = context_.settings().uiAspect;
    const bool aspectAuto = aspectCode.empty() || aspectCode == "auto";
    std::string aspectLabel = aspectAuto ? std::string(context_.i18n().text("ui.settings.aspect_auto"))
                                         : aspectCode;
    const std::vector<platform::AspectPreset> presets = platform::aspectPresets();
    for (const auto& preset : presets) {
        if (preset.code == aspectCode) {
            aspectLabel = preset.code + "  " + std::to_string(preset.width) + "×" +
                          std::to_string(preset.height);
            break;
        }
    }

    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::BeginCombo(context_.i18n().text("ui.settings.aspect"), aspectLabel.c_str())) {
        if (ImGui::Selectable(context_.i18n().text("ui.settings.aspect_auto"), aspectAuto)) {
            applyAspect("auto", 0, 0);
        }
        for (const auto& preset : presets) {
            const std::string label =
                preset.code + "  " + std::to_string(preset.width) + "×" + std::to_string(preset.height);
            if (ImGui::Selectable(label.c_str(), preset.code == aspectCode)) {
                applyAspect(preset.code, preset.width, preset.height);
            }
            if (preset.code == aspectCode) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    wrappedHint(context_.i18n().text("ui.settings.aspect_hint"));
}

namespace {

/// 一个可调色项：settings.txt 的键 / 界面文案键 / 结构体成员 / 默认值 / 分组
struct ColorDef {
    const char* key;
    const char* labelKey;
    std::string Settings::* field;
    const char* defaultValue;
    int group;   // 0 底色 / 1 文字 / 2 控件 / 3 状态
};

const ColorDef kThemeColors[] = {
    {"ui_color_bg", "ui.settings.color_bg", &Settings::uiColorBackground, "0F171C", 0},
    {"ui_color_panel", "ui.settings.color_panel", &Settings::uiColorPanel, "171F24", 0},
    {"ui_color_input", "ui.settings.color_input", &Settings::uiColorInput, "232E34", 0},
    {"ui_color_border", "ui.settings.color_border", &Settings::uiColorBorder, "35434A", 0},
    {"ui_color_text", "ui.settings.color_text", &Settings::uiColorText, "EBF5F0", 1},
    {"ui_color_text_muted", "ui.settings.color_text_muted", &Settings::uiColorTextMuted, "8FA3A0", 1},
    {"ui_color_text_accent", "ui.settings.color_text_accent", &Settings::uiColorTextAccent, "96DCB4", 1},
    {"ui_color_button", "ui.settings.color_button", &Settings::uiColorButton, "295C4A", 2},
    {"ui_color_button_text", "ui.settings.color_button_text", &Settings::uiColorButtonText, "16181C", 2},
    {"ui_color_button2", "ui.settings.color_button2", &Settings::uiColorButton2, "24322F", 2},
    {"ui_color_icon", "ui.settings.color_icon", &Settings::uiColorIcon, "EBF5F0", 2},
    {"ui_color_accent", "ui.settings.color_accent", &Settings::uiColorAccent, "96DCB4", 3},
    {"ui_color_correct", "ui.settings.color_correct", &Settings::uiColorCorrect, "78DCA0", 3},
    {"ui_color_wrong", "ui.settings.color_wrong", &Settings::uiColorWrong, "F08C82", 3},
    {"ui_color_partial", "ui.settings.color_partial", &Settings::uiColorPartial, "E8C46A", 3},
    {"ui_color_draft", "ui.settings.color_draft", &Settings::uiColorDraft, "5B9BD5", 3},
    {"ui_color_star", "ui.settings.color_star", &Settings::uiColorStar, "F2C14E", 3},
};

std::string trimText(const std::string& text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r')) { ++begin; }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r')) { --end; }
    return text.substr(begin, end - begin);
}

}  // namespace

void SettingsScreen::drawThemeGroup(int group) {
    Settings& settings = context_.settings();
    for (const ColorDef& def : kThemeColors) {
        if (def.group != group) {
            continue;
        }
        std::string& value = settings.*(def.field);
        unsigned int rgb = 0;
        if (!Settings::parseHexColor(value, rgb) && !Settings::parseHexColor(def.defaultValue, rgb)) {
            rgb = 0xFFFFFF;
        }
        float color[3] = {static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
                          static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                          static_cast<float>(rgb & 0xFF) / 255.0f};
        ImGui::PushID(def.key);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(context_.i18n().text(def.labelKey));
        ImGui::SameLine(190.0f);
        // 点色块弹出取色器（这是「点色块取色」那条）
        if (ImGui::ColorEdit3("##pick", color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
            char picked[8] = {0};
            std::snprintf(picked, sizeof(picked), "%02X%02X%02X",
                          static_cast<int>(color[0] * 255.0f + 0.5f),
                          static_cast<int>(color[1] * 255.0f + 0.5f),
                          static_cast<int>(color[2] * 255.0f + 0.5f));
            value = picked;
            context_.saveSettings();
        }
        ImGui::SameLine();
        char hex[8] = {0};
        std::snprintf(hex, sizeof(hex), "%s", value.c_str());
        // 宽度按当前字体实测算：写死像素在大字号下会把第 6 位（最后一个十六进制字符）截掉
        ImGui::SetNextItemWidth(ImGui::CalcTextSize("ABCDEF").x +
                                ImGui::GetStyle().FramePadding.x * 2.0f + 4.0f);
        if (ImGui::InputText("##hex", hex, sizeof(hex),
                             ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue)) {
            unsigned int parsed = 0;
            if (Settings::parseHexColor(hex, parsed)) {
                value = hex;
                context_.saveSettings();
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", def.defaultValue);
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("%s", context_.i18n().text("ui.settings.color_hint"));
}

void SettingsScreen::drawThemeApply() {
    Settings& settings = context_.settings();
    const std::vector<ThemePreset>& presets = context_.themePresets();
    if (presets.empty()) {
        wrappedHint(context_.i18n().text("ui.settings.preset_empty"));
    } else {
        std::string currentName = settings.uiThemeName;
        ImGui::SetNextItemWidth(240.0f);
        if (ImGui::BeginCombo(context_.i18n().text("ui.settings.preset"), currentName.c_str())) {
            for (const ThemePreset& preset : presets) {
                const bool selected = (preset.name == currentName);
                if (ImGui::Selectable(preset.name.c_str(), selected)) {
                    const int applied = applyThemePreset(preset, settings);
                    settings.uiThemeName = preset.name;
                    context_.saveSettings();
                    context_.setMessage(context_.i18n().format(
                        "ui.settings.preset_applied",
                        {{"name", preset.name}, {"count", std::to_string(applied)}}));
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        wrappedHint(context_.i18n().text("ui.settings.preset_hint"));
    }

    ImGui::Spacing();
    if (ImGui::Button(context_.i18n().text("ui.settings.theme_reset"), ImVec2(200.0f, 0.0f))) {
        for (const ColorDef& def : kThemeColors) {
            settings.*(def.field) = def.defaultValue;
        }
        context_.saveSettings();
        context_.setMessage(context_.i18n().text("ui.settings.theme_reset_done"));
    }
    ImGui::SameLine();
    if (ImGui::Button(context_.i18n().text("ui.settings.theme_copy"), ImVec2(200.0f, 0.0f))) {
        std::string text;
        for (const ColorDef& def : kThemeColors) {
            text += std::string(def.key) + " = " + (settings.*(def.field)) + "\n";
        }
        if (platform::setClipboardText(text)) {
            context_.setMessage(context_.i18n().text("ui.common.copied"));
        }
    }

    ImGui::Spacing();
    wrappedHint(context_.i18n().text("ui.settings.theme_import_hint"));
    static char themeText[2048] = {0};
    ImGui::InputTextMultiline("##theme_text", themeText, sizeof(themeText),
                              ImVec2(-1.0f, ImGui::GetTextLineHeight() * 6.0f));
    if (ImGui::Button(context_.i18n().text("ui.settings.theme_apply"), ImVec2(160.0f, 0.0f))) {
        int applied = 0;
        std::string text(themeText);
        size_t begin = 0;
        while (begin <= text.size()) {
            const size_t end = text.find('\n', begin);
            const std::string line = trimText(text.substr(begin, end == std::string::npos ? std::string::npos : end - begin));
            const size_t equals = line.find('=');
            if (equals != std::string::npos) {
                const std::string key = trimText(line.substr(0, equals));
                const std::string value = trimText(line.substr(equals + 1));
                for (const ColorDef& def : kThemeColors) {
                    if (key == def.key) {
                        unsigned int parsed = 0;
                        if (Settings::parseHexColor(value, parsed)) {
                            settings.*(def.field) = value;
                            ++applied;
                        }
                    }
                }
            }
            if (end == std::string::npos) {
                break;
            }
            begin = end + 1;
        }
        context_.saveSettings();
        context_.setMessage(context_.i18n().format(
            "ui.settings.theme_applied", {{"count", std::to_string(applied)}}));
    }
}

void SettingsScreen::drawTheme() {
    // 主题配色不再往下跳页：五个分组都在这一页上「展开 / 收起」，点标题那一行就行。
    // 展开状态按位记在 themeOpen_ 里（位 0 底色 / 1 文字 / 2 控件 / 3 状态 / 4 预设与应用）。
    struct GroupRow {
        const char* labelKey;
        int group;   // >= 0 = 配色分组编号；-1 = 预设与应用
    };
    const GroupRow rows[] = {
        {"ui.settings.theme_bg", 0},
        {"ui.settings.theme_text", 1},
        {"ui.settings.theme_controls", 2},
        {"ui.settings.theme_status", 3},
        {"ui.settings.theme_preset_page", -1},
    };
    for (int i = 0; i < static_cast<int>(sizeof(rows) / sizeof(rows[0])); ++i) {
        const bool open = (themeOpen_ & (1 << i)) != 0;
        ImGui::PushID(i);
        // 左边的小三角用 ImGui 自带的箭头按钮：它是用图元画出来的，不依赖字体里有没有这个字形
        if (ImGui::ArrowButton("##theme_toggle", open ? ImGuiDir_Down : ImGuiDir_Right)) {
            themeOpen_ ^= (1 << i);
        }
        ImGui::SameLine();
        const std::string label = context_.i18n().text(rows[i].labelKey);
        if (ImGui::Button(label.c_str(), ImVec2(-1.0f, 0.0f))) {
            themeOpen_ ^= (1 << i);   // 标题那一行整行都能点，手指不容易点空
        }
        if (open) {
            ImGui::Indent();
            if (rows[i].group >= 0) {
                drawThemeGroup(rows[i].group);
            } else {
                drawThemeApply();
            }
            ImGui::Unindent();
        }
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::Separator();
    wrappedHint(context_.i18n().text("ui.settings.theme_expand_hint"));
}
void SettingsScreen::drawPrompt() {
    const std::vector<PromptEntry>& prompts = context_.prompts();
    if (prompts.empty()) {
        wrappedHint(context_.i18n().text("ui.settings.prompt_empty"));
        return;
    }
    if (promptIndex_ < 0 || promptIndex_ >= static_cast<int>(prompts.size())) {
        promptIndex_ = 0;
    }

    // ---- 选一个提示词 ----
    ImGui::SetNextItemWidth(300.0f);
    if (ImGui::BeginCombo(context_.i18n().text("ui.settings.prompt_select"),
                          prompts[promptIndex_].name.c_str())) {
        for (int i = 0; i < static_cast<int>(prompts.size()); ++i) {
            const bool selected = (i == promptIndex_);
            if (ImGui::Selectable(prompts[i].name.c_str(), selected)) {
                promptIndex_ = i;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    // ---- 复制：按钮，或者 Ctrl+C（焦点在本页时）----
    bool copyRequested = false;
    if (ImGui::Button(context_.i18n().text("ui.common.copy"), ImVec2(160.0f, 0.0f))) {
        copyRequested = true;
    }
    if (platform::hasHardwareKeyboard()) {   // 触屏端没有键盘，这行提示没意义
        ImGui::SameLine();
        ImGui::TextDisabled("%s", context_.i18n().text("ui.settings.prompt_copy_hint"));
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_C)) {
        copyRequested = true;
    }

    if (copyRequested) {
        const std::string& text = prompts[promptIndex_].text;
        if (platform::setClipboardText(text)) {
            context_.setMessage(context_.i18n().text("ui.common.copied"));
        } else {
            context_.setMessage(context_.i18n().text("ui.settings.prompt_copy_failed"));
        }
    }

    // ---- 预览 ----
    ImGui::Spacing();
    ImGui::TextDisabled("%s", context_.i18n().text("ui.settings.prompt_preview"));
    const float previewHeight = std::max(ImGui::GetContentRegionAvail().y - 8.0f, 120.0f);
    if (ImGui::BeginChild("prompt_preview", ImVec2(-1.0f, previewHeight), ImGuiChildFlags_Borders)) {
        // 提示词通常远超一屏；预览区是只读文本，从任意位置按住上划即可阅读，
        // 不需要精确按住右侧滚动条。EndChild() 由外层无条件配对调用。
        ScrollDrag scrollDrag;
        ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
        ImGui::TextUnformatted(prompts[promptIndex_].text.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
}

void SettingsScreen::applyAspect(const std::string& code, int width, int height) {
    context_.settings().uiAspect = code;
    context_.saveSettings();
    if (code != "auto") {
        platform::setWindowClientSize(width, height);
    }
}

}  // namespace quiz::ui

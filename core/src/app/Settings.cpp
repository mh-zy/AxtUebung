#include "quiz/app/Settings.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "quiz/io/TextDocument.h"

namespace quiz {

namespace {

bool toBool(const std::string& text) {
    const std::string value = io::trim(text);
    return value == "1" || value == "true" || value == "yes" || value == "on" || value == "是";
}

float toFloat(const std::string& text, float fallback) {
    const std::string value = io::trim(text);
    if (value.empty()) {
        return fallback;
    }
    char* end = nullptr;
    const double parsed = std::strtod(value.c_str(), &end);
    if (end == value.c_str()) {
        return fallback;
    }
    return static_cast<float>(parsed);
}

}  // namespace

float Settings::autoNextDelay() const {
    return std::clamp(autoNextDelaySeconds, kMinDelaySeconds, kMaxDelaySeconds);
}


std::string Settings::serialize() const {
    char delay[32];
    std::snprintf(delay, sizeof(delay), "%.2f", static_cast<double>(autoNextDelaySeconds));

    std::string out;
    out += "# 刷题设置（纯文本，可手工编辑；1 = 开，0 = 关）\n\n";
    out += "[ui]\n";
    out += std::string("auto_next_on_correct = ") + (autoNextOnCorrect ? "1" : "0") + "\n";
    out += std::string("auto_submit_single = ") + (autoSubmitSingle ? "1" : "0") + "\n";
    out += std::string("paper_submit_whole = ") + (paperSubmitWholePage ? "1" : "0") + "\n";
    out += "practice_order = " + (practiceOrder.empty() ? std::string("sequence") : practiceOrder) + "\n";
    out += "home_order = " + (homeOrder.empty() ? std::string("sequence") : homeOrder) + "\n";
    out += "round_size = " + std::to_string(roundSize) + "\n";
    out += "wrong_book_ratio = " + std::to_string(wrongBookRatio) + "\n";
    out += "weighted_append_every = " + std::to_string(weightedAppendEvery) + "\n";
    out += std::string("auto_next_delay = ") + delay + "\n";
    out += "ui_aspect = " + (uiAspect.empty() ? std::string("auto") : uiAspect) + "\n";
    out += "ui_color_bg = " + uiColorBackground + "\n";
    out += "ui_color_panel = " + uiColorPanel + "\n";
    out += "ui_color_text = " + uiColorText + "\n";
    out += "ui_color_button = " + uiColorButton + "\n";
    out += "ui_color_accent = " + uiColorAccent + "\n";
    out += "ui_color_correct = " + uiColorCorrect + "\n";
    out += "ui_color_wrong = " + uiColorWrong + "\n";
    out += "ui_color_input = " + uiColorInput + "\n";
    out += "ui_color_border = " + uiColorBorder + "\n";
    out += "ui_color_text_muted = " + uiColorTextMuted + "\n";
    out += "ui_color_text_accent = " + uiColorTextAccent + "\n";
    out += "ui_color_button_text = " + uiColorButtonText + "\n";
    out += "ui_color_button2 = " + uiColorButton2 + "\n";
    out += "ui_color_icon = " + uiColorIcon + "\n";
    out += "ui_color_scrollbar = " + uiColorScrollbar + "\n";
    out += "ui_color_partial = " + uiColorPartial + "\n";
    out += "ui_color_draft = " + uiColorDraft + "\n";
    out += "ui_color_star = " + uiColorStar + "\n";
    out += "ui_theme_name = " + uiThemeName + "\n";
    out += "home_background_source = " +
           (homeBackgroundSource.empty() ? std::string("none") : homeBackgroundSource) + "\n";
    out += "home_background_mode = " +
           (homeBackgroundMode.empty() ? std::string("auto") : homeBackgroundMode) + "\n";
    out += "home_background_opacity = " + std::to_string(homeBackgroundOpacity) + "\n";
    out += "home_background_fit = " +
           (homeBackgroundFit.empty() ? std::string("cover") : homeBackgroundFit) + "\n";
    out += std::string("home_background_top = ") + (homeBackgroundTop ? "1" : "0") + "\n";
    out += std::string("home_background_bottom = ") + (homeBackgroundBottom ? "1" : "0") + "\n";
    out += std::string("manual_intro_dismissed = ") + (manualIntroDismissed ? "1" : "0") + "\n";
    return out;
}

bool Settings::uiAspectRatio(int& outWidth, int& outHeight) const {
    const std::string value = io::trim(uiAspect);
    if (value.empty() || value == "auto") {
        return false;   // 跟随窗口
    }
    const size_t separator = value.find_first_of(":xX");
    if (separator == std::string::npos) {
        return false;
    }
    const int width = std::atoi(value.substr(0, separator).c_str());
    const int height = std::atoi(value.substr(separator + 1).c_str());
    if (width <= 0 || height <= 0) {
        return false;
    }
    outWidth = width;
    outHeight = height;
    return true;
}

Settings Settings::parse(const std::string& text) {
    Settings result;

    std::vector<io::Issue> issues;
    const std::vector<io::Line> lines = io::parse(text, issues);

    std::string section;
    for (const io::Line& line : lines) {
        if (line.isSection) {
            section = line.section;
            continue;
        }
        if (section != "ui") {
            continue;
        }
        if (line.key == "auto_next_on_correct") {
            result.autoNextOnCorrect = toBool(line.value);
        } else if (line.key == "auto_submit_single") {
            result.autoSubmitSingle = toBool(line.value);
        } else if (line.key == "paper_submit_whole") {
            result.paperSubmitWholePage = toBool(line.value);
        } else if (line.key == "practice_order") {
            const std::string value = io::trim(line.value);
            // sequence（顺序，跳过已答对）/ shuffle（乱序，错题权重更高）/ wrong（只刷错题）
            result.practiceOrder = (value == "shuffle" || value == "wrong") ? value : "sequence";
        } else if (line.key == "home_order") {
            const std::string value = io::trim(line.value);
            // 首页模式：sequence（顺序）/ shuffle（乱序）/ weighted（权重刷题）/ recite（背题）/ exam（考试）
            // 注意：新增模式必须加进这个白名单，否则存了也读不回来（重启就变回顺序，实测踩过）
            result.homeOrder = (value == "recite" || value == "shuffle" || value == "weighted" ||
                                value == "exam")
                                   ? value
                                   : "sequence";
        } else if (line.key == "round_size" || line.key == "weighted_round_size") {
            // 老配置里这个键叫 weighted_round_size（那时候只有权重刷题用它），现在也认
            const int value = std::atoi(line.value.c_str());
            result.roundSize = value < Settings::kMinRoundSize
                                   ? Settings::kMinRoundSize
                                   : (value > Settings::kMaxRoundSize ? Settings::kMaxRoundSize : value);
        } else if (line.key == "weighted_append_every") {
            const int value = std::atoi(line.value.c_str());
            result.weightedAppendEvery =
                value < Settings::kMinWeightedAppend
                    ? Settings::kMinWeightedAppend
                    : (value > Settings::kMaxWeightedAppend ? Settings::kMaxWeightedAppend : value);
        } else if (line.key == "wrong_book_ratio") {
            const int value = std::atoi(line.value.c_str());
            result.wrongBookRatio =
                value < Settings::kMinWrongBookRatio
                    ? Settings::kMinWrongBookRatio
                    : (value > Settings::kMaxWrongBookRatio ? Settings::kMaxWrongBookRatio : value);
        } else if (line.key == "auto_next_delay") {
            result.autoNextDelaySeconds = toFloat(line.value, result.autoNextDelaySeconds);
        } else if (line.key == "ui_aspect") {
            result.uiAspect = io::trim(line.value);
        } else if (line.key == "ui_color_bg") {
            result.uiColorBackground = io::trim(line.value);
        } else if (line.key == "ui_color_panel") {
            result.uiColorPanel = io::trim(line.value);
        } else if (line.key == "ui_color_text") {
            result.uiColorText = io::trim(line.value);
        } else if (line.key == "ui_color_button") {
            result.uiColorButton = io::trim(line.value);
        } else if (line.key == "ui_color_accent") {
            result.uiColorAccent = io::trim(line.value);
        } else if (line.key == "ui_color_correct") {
            result.uiColorCorrect = io::trim(line.value);
        } else if (line.key == "ui_color_wrong") {
            result.uiColorWrong = io::trim(line.value);
        } else if (line.key == "ui_color_input") {
            result.uiColorInput = io::trim(line.value);
        } else if (line.key == "ui_color_border") {
            result.uiColorBorder = io::trim(line.value);
        } else if (line.key == "ui_color_text_muted") {
            result.uiColorTextMuted = io::trim(line.value);
        } else if (line.key == "ui_color_text_accent") {
            result.uiColorTextAccent = io::trim(line.value);
        } else if (line.key == "ui_color_button_text") {
            result.uiColorButtonText = io::trim(line.value);
        } else if (line.key == "ui_color_button2") {
            result.uiColorButton2 = io::trim(line.value);
        } else if (line.key == "ui_color_icon") {
            result.uiColorIcon = io::trim(line.value);
        } else if (line.key == "ui_color_scrollbar") {
            result.uiColorScrollbar = io::trim(line.value);
        } else if (line.key == "ui_color_draft") {
            result.uiColorDraft = io::trim(line.value);
        } else if (line.key == "ui_color_partial") {
            result.uiColorPartial = io::trim(line.value);
        } else if (line.key == "ui_color_star") {
            result.uiColorStar = io::trim(line.value);
        } else if (line.key == "ui_theme_name") {
            result.uiThemeName = io::trim(line.value);
        } else if (line.key == "home_background_source") {
            result.homeBackgroundSource = io::trim(line.value);
        } else if (line.key == "home_background_mode") {
            const std::string value = io::trim(line.value);
            result.homeBackgroundMode =
                (value == "tint" || value == "color") ? value : "auto";
        } else if (line.key == "home_background_opacity") {
            const int value = std::atoi(line.value.c_str());
            result.homeBackgroundOpacity =
                value < Settings::kMinBackgroundOpacity
                    ? Settings::kMinBackgroundOpacity
                    : (value > Settings::kMaxBackgroundOpacity ? Settings::kMaxBackgroundOpacity
                                                               : value);
        } else if (line.key == "home_background_fit") {
            result.homeBackgroundFit = io::trim(line.value) == "contain" ? "contain" : "cover";
        } else if (line.key == "home_background_top") {
            result.homeBackgroundTop = toBool(line.value);
        } else if (line.key == "home_background_bottom") {
            result.homeBackgroundBottom = toBool(line.value);
        } else if (line.key == "manual_intro_dismissed") {
            result.manualIntroDismissed = toBool(line.value);
        }
    }
    return result;
}

bool Settings::parseHexColor(const std::string& text, unsigned int& outRgb) {
    std::string value = io::trim(text);
    if (!value.empty() && value[0] == '#') {
        value.erase(0, 1);
    }
    if (value.size() != 6) {
        return false;
    }
    unsigned int rgb = 0;
    for (char ch : value) {
        int digit = -1;
        if (ch >= '0' && ch <= '9') {
            digit = ch - '0';
        } else if (ch >= 'a' && ch <= 'f') {
            digit = ch - 'a' + 10;
        } else if (ch >= 'A' && ch <= 'F') {
            digit = ch - 'A' + 10;
        } else {
            return false;
        }
        rgb = (rgb << 4) | static_cast<unsigned int>(digit);
    }
    outRgb = rgb;
    return true;
}

}  // namespace quiz

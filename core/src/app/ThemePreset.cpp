#include "quiz/app/ThemePreset.h"

#include <string>
#include <utility>
#include <vector>

#include "quiz/io/TextDocument.h"

namespace quiz {

namespace {

/// 键名 → Settings 字段。
/// ⚠ 这张表必须和设置页那张表（ui/src/SettingsScreen.cpp 的 kThemeColors）保持一致：
///   少一项，themes.txt 里对应的那行就会被忽略（解析得出来，但套不上去）。
struct FieldRef {
    const char* key;
    std::string Settings::*field;
};

const FieldRef kFields[] = {
    {"ui_color_bg", &Settings::uiColorBackground},
    {"ui_color_panel", &Settings::uiColorPanel},
    {"ui_color_input", &Settings::uiColorInput},
    {"ui_color_border", &Settings::uiColorBorder},
    {"ui_color_text", &Settings::uiColorText},
    {"ui_color_text_muted", &Settings::uiColorTextMuted},
    {"ui_color_text_accent", &Settings::uiColorTextAccent},
    {"ui_color_button", &Settings::uiColorButton},
    {"ui_color_button_text", &Settings::uiColorButtonText},
    {"ui_color_button2", &Settings::uiColorButton2},
    {"ui_color_icon", &Settings::uiColorIcon},
    {"ui_color_accent", &Settings::uiColorAccent},
    {"ui_color_correct", &Settings::uiColorCorrect},
    {"ui_color_wrong", &Settings::uiColorWrong},
    {"ui_color_partial", &Settings::uiColorPartial},
    {"ui_color_draft", &Settings::uiColorDraft},
    {"ui_color_star", &Settings::uiColorStar},
};

/// 早期版本的短键名 → 现在的键名（老 themes.txt 仍然能用）
const std::pair<const char*, const char*> kLegacyKeys[] = {
    {"bg", "ui_color_bg"},         {"panel", "ui_color_panel"},
    {"text", "ui_color_text"},     {"button", "ui_color_button"},
    {"accent", "ui_color_accent"}, {"correct", "ui_color_correct"},
    {"wrong", "ui_color_wrong"},
};

/// 把键名规范成 ui_color_*；不认识就返回空串（调用方据此忽略这一行）
std::string normalizeColorKey(const std::string& key) {
    for (const FieldRef& ref : kFields) {
        if (key == ref.key) {
            return key;
        }
    }
    for (const auto& legacy : kLegacyKeys) {
        if (key == legacy.first) {
            return legacy.second;
        }
    }
    return std::string();
}

}  // namespace

const std::string& ThemePreset::color(const std::string& key) const {
    static const std::string kEmpty;
    for (const ThemeColor& entry : colors) {
        if (entry.key == key) {
            return entry.value;
        }
    }
    return kEmpty;
}

const std::vector<std::string>& themeColorKeys() {
    static const std::vector<std::string> kKeys = [] {
        std::vector<std::string> keys;
        keys.reserve(sizeof(kFields) / sizeof(kFields[0]));
        for (const FieldRef& ref : kFields) {
            keys.emplace_back(ref.key);
        }
        return keys;
    }();
    return kKeys;
}

std::vector<ThemePreset> parseThemePresets(const std::string& text) {
    std::vector<io::Issue> issues;
    const std::vector<io::Line> lines = io::parse(text, issues);

    std::vector<ThemePreset> presets;
    ThemePreset current;
    bool opened = false;

    const auto flush = [&]() {
        if (opened && !current.name.empty()) {
            presets.push_back(current);
        }
        current = ThemePreset{};
        opened = false;
    };

    for (const io::Line& line : lines) {
        if (line.isSection) {
            continue;   // 允许 [themes] 之类的段落标记，忽略即可
        }
        if (line.key.empty()) {
            continue;
        }
        if (line.key == "id") {
            // id = 下一套从这里开始（老的 id=1 和新的 id = 1 都认）
            if (opened) {
                flush();
            }
            opened = true;
            current.id = line.value;
            continue;
        }
        if (!opened) {
            opened = true;   // 没写 id 也认，从第一个键开始算一块
        }
        if (line.key == "name") {
            current.name = line.value;
            continue;
        }
        const std::string key = normalizeColorKey(line.key);
        if (!key.empty()) {
            current.colors.push_back({key, line.value});
        }
    }
    flush();
    return presets;
}

int applyThemePreset(const ThemePreset& preset, Settings& settings) {
    int applied = 0;
    for (const ThemeColor& color : preset.colors) {
        for (const FieldRef& ref : kFields) {
            if (color.key != ref.key) {
                continue;
            }
            unsigned int rgb = 0;
            if (Settings::parseHexColor(color.value, rgb)) {
                settings.*(ref.field) = color.value;
                ++applied;
            }
            break;
        }
    }
    return applied;
}

}  // namespace quiz

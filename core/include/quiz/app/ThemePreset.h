#pragma once

#include <string>
#include <vector>

#include "quiz/app/Settings.h"

// ============================================================================
// 主题预设：assets/presets/themes.txt 里的一整套配色
// ----------------------------------------------------------------------------
// 文件格式（复用题库那套 INI 风格解析：key = value，# 或 ; 是注释）：
//
//     id = 1                   一套预设的起点（只用来分块：遇到 id 就换一套）
//     name = 现代简约           显示名（必填：没有 name 的块会被丢掉）
//     ui_color_bg = F4F5F7     颜色项，键名与 settings.txt 里的完全一致（一共 18 项）
//     ui_color_panel = FFFFFF
//     ...
//
// 颜色统一是十六进制 RRGGBB（可带 #，大小写都认）。某一项写坏了只跳过那一项，其余照常套用；
// 键名对不上的行直接忽略。
// 兼容早期版本的短键名：bg / panel / text / button / accent / correct / wrong
// （等价于 ui_color_bg / _panel / _text / _button / _accent / _correct / _wrong）。
//
// 完整说明（18 个颜色项的键名、分组、默认值）见 docs/主题配色格式.md。
// ============================================================================
namespace quiz {

/// 一套预设里的一个颜色项
struct ThemeColor {
    std::string key;     // 与 settings.txt 里的键名一致，例如 ui_color_bg
    std::string value;   // 十六进制 RRGGBB
};

struct ThemePreset {
    std::string id;                  // themes.txt 里的 id（分块用，可以省略）
    std::string name;                // 显示名
    std::vector<ThemeColor> colors;  // 这套预设要改的颜色（保持文件里的书写顺序）

    /// 取某个键的颜色；没写这一项就返回空串
    const std::string& color(const std::string& key) const;
};

/// 解析预设文本；只保留有 name 的块
std::vector<ThemePreset> parseThemePresets(const std::string& text);

/// 把一套预设套到设置上：只覆盖写对了的十六进制颜色，返回实际应用了几个（0~17）
int applyThemePreset(const ThemePreset& preset, Settings& settings);

/// 全部可配颜色的键名（18 个，顺序与设置页「主题配色」一致）。
/// 写 themes.txt、写文档、写测试都对着这份清单来
const std::vector<std::string>& themeColorKeys();

}  // namespace quiz

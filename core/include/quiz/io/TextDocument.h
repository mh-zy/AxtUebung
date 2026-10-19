#pragma once

#include <string>
#include <vector>

// ============================================================================
// 极简「INI 风格」文本格式解析器
// ----------------------------------------------------------------------------
// 题库文件、进度文件、语言包都使用同一种格式，规则如下：
//     # 这是注释（以 ; 开头也是注释）     整行忽略
//     [section]                          段落标记
//     key = value                        键值对（第一个 = 之后全部作为值）
//     值里支持转义：\n 换行、\t 制表符、\\ 反斜杠
// 为什么不用 JSON/TOML：纯文本可手写、可 diff、可逐行报错，而且不需要任何第三方库。
// ============================================================================
namespace quiz::io {

/// 一行解析结果
struct Line {
    int lineNumber = 0;      // 行号，从 1 开始（报错时用来定位）
    bool isSection = false;  // 是否是 [section] 行
    std::string section;     // 所属段落名（已去掉方括号）
    std::string key;         // 键（已去首尾空白）
    std::string value;       // 值（已去首尾空白并反转义）
};

/// 解析过程中发现的问题
struct Issue {
    int line = 0;
    std::string message;
};

/// 把文本按行解析；语法错误会记录到 issues，但尽量继续解析后面的行
std::vector<Line> parse(const std::string& text, std::vector<Issue>& issues);

/// 写文件时用 escape，读文件时用 unescape
std::string escape(const std::string& value);
std::string unescape(const std::string& value);

std::string trim(const std::string& text);
std::vector<std::string> split(const std::string& text, char separator);
bool startsWith(const std::string& text, const std::string& prefix);
bool endsWith(const std::string& text, const std::string& suffix);

/// 按行切分（保留空行，行尾的 \r 已去掉）
std::vector<std::string> splitLines(const std::string& text);

}  // namespace quiz::io

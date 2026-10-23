#pragma once

#include <string>

namespace quiz::io {

struct DocumentTextResult {
    bool ok = false;
    std::string text;
    std::string error;
};

/// 把导入文件转换成题库解析器可读取的 UTF-8 文本。
///
/// - 普通题库文件原样返回；
/// - `.docx` / `.docm` / `.dotx` / `.dotm` 会读取 Open XML 包中的
///   `word/document.xml`，提取段落文字；
/// - 旧 `.doc` 支持纯文本、HTML 形式，以及二进制文件中仍可识别的题库正文；
/// - 不修改题目字段，也不在这里解析题库语法。
DocumentTextResult extractBankTextDocument(const std::string& fileName,
                                           const std::string& bytes);

}  // namespace quiz::io

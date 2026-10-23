#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace quiz::math {

/// 题库文本中的片段类型。
///
/// 题库源文件始终保持原样：这里只负责“显示时如何拆分”，导入、导出和保存进度
/// 都不应使用转换后的文本。这样以后接入完整公式渲染时，原始 LaTeX 仍然可用。
enum class MathSegmentKind {
    Text,
    InlineFormula,
    DisplayFormula,
};

struct MathSegment {
    MathSegmentKind kind = MathSegmentKind::Text;

    /// 普通文字原样保存；公式则保存去掉 `$` / `$$` 分隔符后的原始 LaTeX。
    std::string source;

    /// 当前兼容层可以直接显示的 Unicode 文本。未知命令会原样保留。
    std::string text;
};

/// 把 LaTeX 子集转换成当前字体可直接显示的 Unicode 文本。
///
/// 这是第一层兼容：不要求公式绝对正确排版，只保证常见上下标、希腊字母和运算符
/// 可读；遇到未知命令时保留原文，绝不能吞掉后续内容。
std::string latexToUnicode(std::string_view latex);

/// 解析第二层公式片段：
/// - `$...$` 是行内公式；
/// - `$$...$$` 是独立公式块；
/// - 未闭合的 `$` 按普通文字保留；
/// - `\$` 表示字面量美元符号。
std::vector<MathSegment> splitFormulaSegments(std::string_view text);

/// 便于列表、标题等不能做富文本排版的位置使用：
/// 行内公式转成 Unicode，独立公式前后补换行。
std::string toDisplayText(std::string_view text);

}  // namespace quiz::math

#pragma once

#include <string>
#include <vector>

#include "quiz/io/TextDocument.h"
#include "quiz/model/Progress.h"
#include "quiz/model/QuestionBank.h"

// ============================================================================
// 固定格式题库的读取与导出
// ----------------------------------------------------------------------------
// 文件格式（详见 docs/题库格式.md）：
//     [meta]            题库信息：id / title / lang / version / desc
//     [q]               每道题一个段落
//       id / type / stem / option（可多行）/ answer / analysis / tags ...
// ============================================================================
namespace quiz::io {

/// 读取结果：题库 + 问题列表 + 是否致命错误
struct LoadResult {
    QuestionBank bank;
    std::vector<Issue> issues;
    bool hasFatalError = false;

    /// 解析结束时固定下来的“是否可用”。
    /// 为什么不直接看 bank.questions：调用方常常会 std::move(result.bank) 拿走数据，
    /// 搬走之后 bank.questions 就是空的，再用它判断就会得出“不可用”的错误结论
    /// （这个坑真实发生过：首页「开始练习」按钮一直灰着）。
    bool parsedOk = false;

    /// 没有致命错误且至少有一道题，即视为可用
    bool ok() const { return parsedOk; }
};

/// 从文本解析题库；sourceName 会写进 meta.sourceName，用于界面显示与导出命名
LoadResult loadBank(const std::string& text, const std::string& sourceName);

/// 把题库写成固定格式文本（用于「导出题库」）
std::string saveBank(const QuestionBank& bank);

/// 只保留「错题本里的」题目，生成错题本题库（用于「导出错题」）
/// @param ratio 「错题本」的被动移出比例（见 isWrongBookItem）
QuestionBank buildWrongOnlyBank(const QuestionBank& bank, const BankProgress& progress, int ratio = 3);

/// 只保留「标记过」的题目
QuestionBank buildMarkedOnlyBank(const QuestionBank& bank, const BankProgress& progress);

/// 按标签导出：带任意一个 tags 的题都保留（多选 = 或的关系）。
/// 匹配范围包括题目自带的 tags 和用户自己加的标签（存在进度里）。
/// @param bakeUserTags true = 把用户自己加的标签并进导出的题目里（固化），
///                     导出的文件就自带标签、不依赖本机的进度文件
QuestionBank buildTaggedBank(const QuestionBank& bank, const BankProgress& progress,
                             const std::vector<std::string>& tags, bool bakeUserTags);

/// 单独校验一个题库文本（命令行工具与测试使用）
std::vector<Issue> validateBank(const std::string& text);

/// 导入题库时接受的文件后缀（题库本来就是纯文本，.txt / .text 也一起收）。
/// 返回的都是小写、带点，例如 ".quiz"
const std::vector<std::string>& bankFileExtensions();

/// 文件名是不是题库文本文件（按后缀判断，大小写不敏感）
bool isBankTextFileName(const std::string& fileName);

/// 把导入的文件名规范化成题库文件名：去掉路径、统一成 .quiz 后缀、
/// 把不能做文件名的字符换掉。空名字会变成 "imported.quiz"
std::string normalizeBankFileName(const std::string& fileName);

}  // namespace quiz::io

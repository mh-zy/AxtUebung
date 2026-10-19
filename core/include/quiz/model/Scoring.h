#pragma once

#include <string>
#include <vector>

#include "quiz/model/Question.h"

// ============================================================================
// 判分规则（纯函数，界面与命令行共用；单元测试直接调这里）
// ----------------------------------------------------------------------------
//  * 单选 / 判断 / 材料：选中唯一答案才算对
//  * 多选：全对才算对（少选、多选都算错）
//  * 不定项：选中的都在正确答案里 → 得分 = 选中数 / 答案数（少选给部分分）；选到错的得 0 分
//  * 多项有序：按空逐个比对，得分 = 对的空数 / 总空数
//  * 填空：忽略所有空白、忽略大小写（全角自动转半角），命中任一可接受答案即算对
// ============================================================================
namespace quiz {

/// 判分结果：value 是 0~1 的得分，full 表示是否全对
struct Score {
    float value = 0.0f;
    bool full = false;
};

/// 选择题族（单选 / 多选 / 判断 / 不定项 / 材料）判分；chosen 是选中的选项下标
Score scoreChoice(const Question& question, const std::vector<int>& chosen);

/// 多项有序题判分；chosen 与题目答案一一对应（第 i 个空选了哪个选项，-1 = 没填）
Score scoreOrdered(const Question& question, const std::vector<int>& chosen);

/// 填空题答案归一化：去掉所有空白、ASCII 转小写、全角字符转半角
std::string normalizeFillText(const std::string& text);

/// 填空题：第 blank 个空（从 0 开始）的输入是否命中该空的可接受答案
bool fillBlankMatches(const Question& question, int blank, const std::string& input);

/// 填空题判分：每个空比一遍；全对 = 全对，对一部分 = 部分分（对的空数 ÷ 总空数）
Score scoreFill(const Question& question, const std::vector<std::string>& inputs);

}  // namespace quiz
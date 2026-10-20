#pragma once

#include <string>
#include <vector>

#include "quiz/io/TextDocument.h"
#include "quiz/model/QuestionBank.h"

// ============================================================================
// 考试模式：按题库里的卷声明（[exam] / [draw]）抽出一套模拟卷
// ----------------------------------------------------------------------------
// * 只做「题库内抽题」：题目都来自同一个题库，卷声明也写在题库文件里
// * 可复现：同一个 seed 一定抽出同一套题（模拟卷要能重现）
// * 跨规则不重复：一道题只会进卷一次
// * 普通抽题规则只收独立题；材料段只由 material 规则整段选中，
//   避免按小题题型误抽材料，并把材料总分筛选套到普通规则上
// * 材料题整段进卷：一段材料的几道小题一起进，且按 materialPart 顺序
// * 材料题：count 数「几段材料」，与里面有几道小题无关；作为补偿，规则里写了 score 就要求
//   整段材料的总分正好等于它（一段就是那个分数），对不上的段不进候选
// * 卷内顺序默认按难度（由易到难），exam.shuffle = 1 时改按 seed 打乱
//   （打乱也以「一段材料」为单位，不会把材料拆散）
// 学习要点：这里不碰界面、不碰文件，输入题库 + 卷声明，输出题号列表，
//   所以单元测试可以直接调它验证抽题行为。
// ============================================================================
namespace quiz::io {

/// 卷面上的一道题：题库里的原始题号 + 这道题在卷面的分值
struct ExamPaperItem {
    int questionIndex = -1;
    int score = 1;
};

/// 抽出来的一整套卷（还没作答，只是题号 + 分值）
struct ExamPaper {
    std::string examId;                 // 卷标识（用来给进度/导出命名）
    std::string title;                  // 卷名（界面上显示）
    int minutes = 0;                    // 限时（分钟）；0 = 不限时
    int passPercent = -1;               // 及格线（百分比）；<0 = 不判及格
    std::vector<ExamPaperItem> items;   // 卷面（按作答顺序）
    std::vector<Issue> issues;          // 「只抽到 x/y」之类的提示，不算致命错误

    /// 卷面满分 = 各题分值之和
    int totalScore() const {
        int total = 0;
        for (const ExamPaperItem& item : items) {
            total += item.score > 0 ? item.score : 1;
        }
        return total;
    }
};

/// 按卷声明抽题。
/// @param seedOverride >= 0 时用它当抽签种子（命令行 --paper 用，便于复现）；
///                      < 0 时用卷声明里的 seed（seed = 0 就是固定按 0 抽）
ExamPaper buildExamPaper(const QuestionBank& bank, const ExamSpec& exam, int seedOverride = -1);

/// 按 id 找卷（找不到返回 nullptr；id 为空时返回第一套卷）
const ExamSpec* findExam(const QuestionBank& bank, const std::string& id);

}  // namespace quiz::io

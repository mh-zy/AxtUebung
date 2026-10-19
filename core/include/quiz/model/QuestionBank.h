#pragma once

#include <string>
#include <vector>

#include "quiz/model/Question.h"

namespace quiz {

/// 题库的元信息（对应题库文件里的 [meta] 段）
struct BankMeta {
    std::string id;              // 题库唯一标识（进度文件按它命名，改了会导致进度对不上）
    std::string title;           // 显示名称
    std::string lang = "zh-CN";  // 题库内容语言
    std::string desc;            // 说明
    int version = 1;             // 题库格式版本
    std::string sourceName;      // 加载来源（asset 名 / 文件路径），导出时用于命名
};

/// 材料题的一段材料（对应题库文件里的 [material] 段）
struct Material {
    std::string id;     // 材料编号；题目用 material = <id> 挂上来
    std::string title;  // 标题（可空）
    std::string text;   // 材料正文（换行写 \n，或写多行 text = 自动拼接）
};

/// 一条抽题规则（对应题库文件里的 [draw] 段，写在自己那套 [exam] 后面）
struct DrawRule {
    int count = 0;                        // 抽几道题（type = material 时表示「几段材料」；0 = 不限）
    std::vector<QuestionType> types;      // 题型白名单（逗号分隔可多选）；空 = 不限
    bool materialOnly = false;            // 只抽材料题（type = material / material = any）
    std::string materialId;               // 只抽某一段材料（material = m1）
    int difficultyMin = 0;                // 难度下限（1~5）；0 = 不限
    int difficultyMax = 0;                // 难度上限（1~5）；0 = 不限
    std::vector<std::string> tags;        // 命中任意一个标签即可；空 = 不限
    std::vector<std::string> excludeTags; // 命中就排除
    std::vector<std::string> ids;         // 点名抽（写了就不随机）
    int score = 0;                        // 这条规则抽到的每题分值；0 = 用题目自带的 score。
                                          // 材料段：写了 score 就要求整段总分等于它（对不上不进候选）
    int line = 0;                         // 所在行号（报错定位用）
};

/// 一套卷的声明（对应题库文件里的 [exam] 段；一个题库可以写好几套）
struct ExamSpec {
    std::string id;             // 卷标识（缺省自动生成 exam1、exam2…）
    std::string title;          // 卷名（界面上让用户选）
    std::string desc;           // 说明（可选）
    int minutes = 0;            // 限时（分钟）；0 = 不限时
    int passPercent = -1;       // 及格线（百分比）；< 0 = 不判及格
    int seed = 0;               // 抽题种子；0 = 每次重抽（写了它每次抽到同一套题）
    bool shuffle = false;       // 卷内题序是否打乱（默认按难度由易到难）
    std::vector<DrawRule> rules;
};
/// 一个题库 = 元信息 + 材料 + 题目列表
struct QuestionBank {
    BankMeta meta;
    std::vector<Material> materials;
    std::vector<Question> questions;
    std::vector<ExamSpec> exams;   // 卷声明（[exam] + [draw]）：考试模式用；空 = 这个题库考不了

    bool empty() const { return questions.empty(); }
    size_t size() const { return questions.size(); }

    /// 按 id 查找，找不到返回 nullptr
    const Question* findById(const std::string& id) const;

    /// 按 id 查找材料，找不到返回 nullptr
    const Material* materialById(const std::string& id) const;

    /// 统计某一题型的数量，用于首页展示
    int countOfType(QuestionType type) const;
};

}  // namespace quiz

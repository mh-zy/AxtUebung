#include "quiz/io/ExamBuilder.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <random>

namespace quiz::io {

namespace {

std::string toText(int value) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%d", value);
    return std::string(buffer);
}

/// 一道题的标签里有没有 wanted 中的任意一个
bool hasAnyTag(const std::vector<std::string>& tags, const std::vector<std::string>& wanted) {
    for (const std::string& tag : tags) {
        for (const std::string& want : wanted) {
            if (tag == want) {
                return true;
            }
        }
    }
    return false;
}

/// 这道题满足一条抽题规则里的题型 / 难度 / 标签条件吗
bool matchesRule(const Question& question, const DrawRule& rule) {
    if (rule.materialOnly && question.materialId.empty()) {
        return false;   // type = material：只抽材料题
    }
    if (!rule.materialOnly && !rule.types.empty() &&
        std::find(rule.types.begin(), rule.types.end(), question.type) == rule.types.end()) {
        return false;
    }
    if (rule.difficultyMin > 0 || rule.difficultyMax > 0) {
        if (question.difficulty <= 0) {
            return false;   // 题目没写难度：写了难度区间的规则就不收它（免得抽出来对不上）
        }
        if (rule.difficultyMin > 0 && question.difficulty < rule.difficultyMin) {
            return false;
        }
        if (rule.difficultyMax > 0 && question.difficulty > rule.difficultyMax) {
            return false;
        }
    }
    if (!rule.tags.empty() && !hasAnyTag(question.tags, rule.tags)) {
        return false;
    }
    if (!rule.excludeTags.empty() && hasAnyTag(question.tags, rule.excludeTags)) {
        return false;
    }
    return true;
}

/// 抽题的「单位」：普通题就是一道题；材料题是**一整段材料**（几道小题一起进卷）
struct Unit {
    bool material = false;
    std::vector<int> indexes;   // 普通题 1 个；材料段按 materialPart 排好
    int matched = 0;            // 这一段材料里有几道小题命中了当前规则
    int difficulty = 0;         // 段内最小难度（>0 才有值），排卷面顺序用
    int totalScore = 0;         // 整段的总分（普通题就是它自己的分值）
};

/// 把题库切成抽题单位（材料段连续、按 materialPart 排；普通题各自成单位）
std::vector<Unit> unitsOf(const QuestionBank& bank) {
    std::vector<Unit> units;
    std::map<std::string, size_t> materialAt;
    for (size_t i = 0; i < bank.questions.size(); ++i) {
        const Question& question = bank.questions[i];
        if (question.materialId.empty()) {
            Unit unit;
            unit.indexes.push_back(static_cast<int>(i));
            units.push_back(unit);
            continue;
        }
        const auto found = materialAt.find(question.materialId);
        if (found == materialAt.end()) {
            Unit unit;
            unit.material = true;
            unit.indexes.push_back(static_cast<int>(i));
            materialAt[question.materialId] = units.size();
            units.push_back(unit);
        } else {
            units[found->second].indexes.push_back(static_cast<int>(i));
        }
    }
    // 段内小题按 materialPart 排（材料题一页作答，顺序乱了没法看）
    for (Unit& unit : units) {
        if (unit.material) {
            std::stable_sort(unit.indexes.begin(), unit.indexes.end(), [&](int a, int b) {
                return bank.questions[static_cast<size_t>(a)].materialPart <
                       bank.questions[static_cast<size_t>(b)].materialPart;
            });
        }
        // 段内最小难度（>0 才有值）：排卷面顺序用。这里一次算好，
        // 点名抽（id = q1）那条路也才拿得到难度
        int difficulty = 0;
        for (int index : unit.indexes) {
            const int value = bank.questions[static_cast<size_t>(index)].difficulty;
            if (value > 0 && (difficulty == 0 || value < difficulty)) {
                difficulty = value;
            }
        }
        unit.difficulty = difficulty;
        // 整段总分：材料段的「一段算几分」按它算（各小题分值之和）
        int total = 0;
        for (int index : unit.indexes) {
            const int value = bank.questions[static_cast<size_t>(index)].score;
            total += value > 0 ? value : 1;
        }
        unit.totalScore = total;
    }
    return units;
}

/// 报错用：题库里各段材料的总分（去重、最多列 4 个），让作者知道实际有哪些分值
std::string materialTotalsHint(const std::vector<Unit>& units) {
    std::vector<int> totals;
    for (const Unit& unit : units) {
        if (!unit.material) {
            continue;
        }
        if (std::find(totals.begin(), totals.end(), unit.totalScore) == totals.end()) {
            totals.push_back(unit.totalScore);
        }
    }
    if (totals.empty()) {
        return std::string();
    }
    std::sort(totals.begin(), totals.end());
    std::string text = "（现有材料段总分：";
    for (size_t k = 0; k < totals.size() && k < 4; ++k) {
        if (k > 0) {
            text += "、";
        }
        text += toText(totals[k]);
    }
    if (totals.size() > 4) {
        text += "…";
    }
    text += "）";
    return text;
}

}  // namespace

ExamPaper buildExamPaper(const QuestionBank& bank, const ExamSpec& exam, int seedOverride) {
    ExamPaper paper;
    paper.examId = exam.id;
    paper.title = exam.title;
    paper.minutes = exam.minutes;
    paper.passPercent = exam.passPercent;

    const unsigned int seed =
        static_cast<unsigned int>(seedOverride >= 0 ? seedOverride : exam.seed);
    std::mt19937 engine(seed);
    std::vector<Unit> units = unitsOf(bank);
    std::vector<char> used(bank.questions.size(), 0);

    struct Picked {
        Unit unit;
        int scoreOverride = 0;   // >0 = 这条规则给的分值；0 = 用题目自带的
    };
    std::vector<Picked> picked;

    for (size_t r = 0; r < exam.rules.size(); ++r) {
        const DrawRule& rule = exam.rules[r];
        const int ruleNo = static_cast<int>(r) + 1;
        const int line = rule.line;
        int taken = 0;                                   // 这条规则已经收了几道题
        const int target = rule.count > 0 ? rule.count : -1;   // -1 = 不限（全收）
        int extraFromMaterial = 0;                       // 为材料段补齐的小题数

        // ---- 点名抽：写了 id 就按 id 收，不打乱 ----
        if (!rule.ids.empty()) {
            for (const std::string& id : rule.ids) {
                const Question* question = bank.findById(id);
                if (question == nullptr) {
                    paper.issues.push_back({line, "抽题规则里的题目不存在：" + id});
                    continue;
                }
                // 找到它属于哪个抽题单位（材料小题要连着整段一起进卷）
                int at = -1;
                for (size_t i = 0; i < bank.questions.size(); ++i) {
                    if (&bank.questions[i] == question) {
                        at = static_cast<int>(i);
                        break;
                    }
                }
                if (at < 0) {
                    continue;
                }
                for (const Unit& unit : units) {
                    if (std::find(unit.indexes.begin(), unit.indexes.end(),
                                  at) == unit.indexes.end()) {
                        continue;
                    }
                    bool anyNew = false;
                    for (int index : unit.indexes) {
                        if (!used[static_cast<size_t>(index)]) {
                            anyNew = true;
                        }
                    }
                    if (!anyNew) {
                        break;   // 已经进卷了（同一个 id 写了两次 / 前后重复）
                    }
                    Picked item;
                    item.unit = unit;
                    // 材料段用题目自带的分值（一段的总分就是各小题之和）
                    item.scoreOverride = unit.material ? 0 : rule.score;
                    if (unit.material && rule.score > 0 && unit.totalScore != rule.score) {
                        paper.issues.push_back(
                            {line, "点名抽的 " + id + " 所在材料段总分是 " +
                                       toText(unit.totalScore) + "，与本条规则的 score = " +
                                       toText(rule.score) + " 不一致；材料段按题目自带的分值进卷"});
                    }
                    for (int index : unit.indexes) {
                        used[static_cast<size_t>(index)] = 1;
                    }
                    taken += static_cast<int>(unit.indexes.size());
                    picked.push_back(item);
                    break;
                }
            }
            continue;
        }

        // ---- 按条件抽：先筛出候选单位，再洗一遍按顺序取 ----
        std::vector<Unit> candidates;
        int filteredByScore = 0;   // 因为「材料段总分跟 score 对不上」被筛掉的材料段数
        for (const Unit& unit : units) {
            bool anyUsed = false;
            int matched = 0;
            for (int index : unit.indexes) {
                if (used[static_cast<size_t>(index)]) {
                    anyUsed = true;
                    continue;
                }
                if (matchesRule(bank.questions[static_cast<size_t>(index)], rule)) {
                    ++matched;
                }
            }
            if (anyUsed || matched == 0) {
                continue;
            }
            // 材料题：count 数的是「几段材料」，跟里面有几道小题无关（小题数不参与数数）；
            // 作为补偿，规则里写了 score 就要求**整段材料的总分正好等于它** ——
            // 这样「一段材料 = score 分」是定的，卷面总分不会被材料里的小题数带偏。
            if (unit.material && rule.score > 0 && unit.totalScore != rule.score) {
                ++filteredByScore;
                continue;
            }
            Unit candidate = unit;
            candidate.matched = matched;
            candidates.push_back(candidate);
        }
        std::shuffle(candidates.begin(), candidates.end(), engine);

        for (const Unit& unit : candidates) {
            if (target >= 0 && taken >= target) {
                break;
            }
            if (rule.materialOnly && !unit.material) {
                continue;   // type = material：count 算「几段材料」
            }
            Picked item;
            item.unit = unit;
            // 材料段用题目自带的分值（总分已经按 score 滤过，两者相等）；普通题用规则里的分值
            item.scoreOverride = unit.material ? 0 : rule.score;
            for (int index : unit.indexes) {
                used[static_cast<size_t>(index)] = 1;
            }
            taken += static_cast<int>(unit.indexes.size());
            if (unit.material && unit.matched < static_cast<int>(unit.indexes.size())) {
                extraFromMaterial += static_cast<int>(unit.indexes.size()) - unit.matched;
            }
            picked.push_back(item);
        }

        if (filteredByScore > 0) {
            paper.issues.push_back(
                {line, "第 " + toText(ruleNo) + " 条抽题规则写了 score = " + toText(rule.score) +
                           "，有 " + toText(filteredByScore) + " 段材料的总分对不上，没进候选" +
                           materialTotalsHint(units)});
        }
        if (target >= 0 && taken < target) {
            paper.issues.push_back(
                {line, "第 " + toText(ruleNo) + " 条抽题规则只抽到 " + toText(taken) + "/" +
                           toText(target) + "（可用的题不够，已把能抽到的都放进卷）"});
        }
        if (extraFromMaterial > 0) {
            paper.issues.push_back(
                {line, "第 " + toText(ruleNo) + " 条抽题规则为材料段补齐了 " +
                           toText(extraFromMaterial) + " 道小题（材料题整段作答）"});
        }
    }

    // ---- 卷面顺序：默认按难度（由易到难），exam.shuffle = 1 时按种子打乱 ----
    // 打乱 / 排序都以「抽题单位」为单位：一段材料的几道小题永远连在一起
    if (exam.shuffle) {
        std::shuffle(picked.begin(), picked.end(), engine);
    } else {
        std::stable_sort(picked.begin(), picked.end(), [](const Picked& a, const Picked& b) {
            const int left = a.unit.difficulty > 0 ? a.unit.difficulty : 99;
            const int right = b.unit.difficulty > 0 ? b.unit.difficulty : 99;
            return left < right;   // 同难度保持抽题顺序
        });
    }

    for (const Picked& item : picked) {
        for (int index : item.unit.indexes) {
            ExamPaperItem paperItem;
            paperItem.questionIndex = index;
            const int own = bank.questions[static_cast<size_t>(index)].score;
            paperItem.score = item.scoreOverride > 0 ? item.scoreOverride : (own > 0 ? own : 1);
            paper.items.push_back(paperItem);
        }
    }
    return paper;
}

const ExamSpec* findExam(const QuestionBank& bank, const std::string& id) {
    if (bank.exams.empty()) {
        return nullptr;
    }
    if (id.empty()) {
        return &bank.exams.front();
    }
    for (const ExamSpec& exam : bank.exams) {
        if (exam.id == id || exam.title == id) {
            return &exam;
        }
    }
    return nullptr;
}

}  // namespace quiz::io

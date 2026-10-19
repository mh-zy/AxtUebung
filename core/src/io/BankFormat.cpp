#include "quiz/io/BankFormat.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace quiz::io {

namespace {

/// 把整数转成字符串（避免引入 <string> 的 to_string 差异）
std::string toText(int value) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%d", value);
    return std::string(buffer);
}

std::string toLower(const std::string& text) {
    std::string out = text;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

/// 判断题答案：T/F、对/错、正确/错误、A/B 都接受
bool parseJudgeAnswer(const std::string& value, int& outIndex, std::string& error) {
    const std::vector<std::string> tokens = split(value, ',');
    const std::string token = toLower(trim(tokens.empty() ? value : tokens.front()));
    if (token == "t" || token == "true" || token == "y" || token == "yes" || token == "对" ||
        token == "正确" || token == "是" || token == "1" || token == "a") {
        outIndex = 0;  // 正确
        return true;
    }
    if (token == "f" || token == "false" || token == "n" || token == "no" || token == "错" ||
        token == "错误" || token == "否" || token == "2" || token == "b") {
        outIndex = 1;  // 错误
        return true;
    }
    error = "判断题答案应为 T/F（也接受 正确/错误、对/错）";
    return false;
}

/// 解析选择题答案：A / B,C / 1 / 2 等
bool parseChoiceAnswer(const std::string& value, std::vector<int>& out, std::string& error) {
    out.clear();
    const std::vector<std::string> tokens = split(value, ',');
    if (tokens.empty()) {
        error = "答案为空";
        return false;
    }
    for (const auto& token : tokens) {
        const int index = letterToIndex(token);
        if (index < 0) {
            error = "答案只能写成 A / B,C / 1 / 2 这样的形式";
            return false;
        }
        out.push_back(index);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return true;
}

/// 从 "stem.en" 这样的键里拆出基础字段名与语言代码；不是这种形式就返回 false
bool splitLocalizedKey(const std::string& key, std::string& base, std::string& lang) {
    const auto dot = key.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= key.size()) {
        return false;
    }
    base = key.substr(0, dot);
    lang = key.substr(dot + 1);
    return base == "stem" || base == "analysis";
}

/// 多项有序题的答案：B,D,A,C,F —— 顺序有意义，所以不能排序
bool parseOrderedAnswer(const std::string& value, std::vector<int>& out, std::string& error) {
    out.clear();
    const std::vector<std::string> tokens = split(value, ',');
    if (tokens.empty()) {
        error = "答案为空";
        return false;
    }
    for (const auto& token : tokens) {
        const int index = letterToIndex(token);
        if (index < 0) {
            error = "多项有序题的答案要按空的顺序写，例如 B,D,A,C,F";
            return false;
        }
        out.push_back(index);
    }
    return true;
}

/// 按分隔符切开但**保留空串**（填空题用 ; 分空，空位不能丢）
std::vector<std::string> splitKeepingEmpty(const std::string& text, char separator) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : text) {
        if (c == separator) {
            parts.push_back(trim(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    parts.push_back(trim(current));
    return parts;
}

/// 填空题答案：空与空之间用 ; 分隔，同一个空的多个可接受答案用 | 分隔
/// 例：answer = H2O|水 ; 100
std::vector<std::vector<std::string>> parseFillAnswer(const std::string& value) {
    std::vector<std::vector<std::string>> blanks;
    for (const auto& part : splitKeepingEmpty(value, ';')) {
        std::vector<std::string> accepted = split(part, '|');
        if (accepted.empty() && !part.empty()) {
            accepted.push_back(part);
        }
        blanks.push_back(accepted);
    }
    if (blanks.size() == 1 && blanks.front().empty()) {
        blanks.clear();   // 「answer =」这种空答案别留下一个空壳
    }
    return blanks;
}

/// 答案文字能不能解析成「选项表」（A / B,C / 1 / 2）
bool isChoiceAnswerText(const std::string& value) {
    const std::vector<std::string> tokens = split(value, ',');
    if (tokens.empty()) {
        return false;
    }
    for (const auto& token : tokens) {
        if (letterToIndex(token) < 0) {
            return false;
        }
    }
    return true;
}

/// 「type = 材料题」不是一种答题方式，而是「这道小题挂在一段材料下」的标记。
/// 题目没有写具体题型时，按数据自己判断：
///   有 blank 行                 → 多项有序
///   选项题的答案是 A / B,C 这种 → 一个就是单选、多个就是多选
///   答案是 T/F 这类             → 判断
///   其余文字答案                → 填空
/// 想写得明确一点的话，直接在小题上写 type = single/multiple/… 就行。
QuestionType resolveMaterialType(const Question& question, const std::string& rawAnswer) {
    if (!question.blanks.empty()) {
        return QuestionType::Ordered;
    }
    const std::string value = trim(rawAnswer);
    if (value.empty()) {
        return QuestionType::Single;
    }
    if (!question.options.empty()) {
        if (isChoiceAnswerText(value)) {
            return split(value, ',').size() > 1 ? QuestionType::Multiple : QuestionType::Single;
        }
        return QuestionType::Single;   // 选项题的答案写错了，交给后面的校验报错
    }
    int judgeIndex = 0;
    std::string ignored;
    if (parseJudgeAnswer(value, judgeIndex, ignored)) {
        return QuestionType::Judge;
    }
    return QuestionType::Fill;
}

/// 难度区间："2-5"（也接受 2~5）；写单个数字表示「正好这个难度」；空串 = 不限
bool parseDifficultyRange(const std::string& value, int& outMin, int& outMax, std::string& error) {
    outMin = 0;
    outMax = 0;
    const std::string text = trim(value);
    if (text.empty()) {
        return true;
    }
    size_t separator = text.find('-');
    if (separator == std::string::npos) {
        separator = text.find('~');
    }
    if (separator == std::string::npos) {
        const int one = std::atoi(text.c_str());
        if (one <= 0 || one > 5) {
            error = "难度只能是 1~5（区间写成 2-5）";
            return false;
        }
        outMin = one;
        outMax = one;
        return true;
    }
    const std::string left = trim(text.substr(0, separator));
    const std::string right = trim(text.substr(separator + 1));
    outMin = left.empty() ? 0 : std::atoi(left.c_str());
    outMax = right.empty() ? 0 : std::atoi(right.c_str());
    if (outMin > 5 || outMax > 5 || (outMin > 0 && outMax > 0 && outMin > outMax)) {
        error = "难度区间只能是 1~5，而且要写成 2-5 这样（小到大）";
        return false;
    }
    return true;
}

/// 抽题规则里的题型列表："single,multiple"；material / 材料题 = 只抽材料题
bool parseRuleTypes(const std::string& value, DrawRule& rule, std::string& error) {
    for (const std::string& token : split(value, ',')) {
        if (token.empty()) {
            continue;
        }
        const std::string lower = toLower(token);
        if (lower == "material" || token == "材料题" || token == "材料") {
            rule.materialOnly = true;
            continue;
        }
        QuestionType type = QuestionType::Single;
        if (!parseQuestionType(token, type)) {
            error = "抽题规则里的题型不认识：" + token;
            return false;
        }
        if (std::find(rule.types.begin(), rule.types.end(), type) == rule.types.end()) {
            rule.types.push_back(type);
        }
    }
    return true;
}

/// [exam] / [draw] 段里的一行；draw = true 时写进 exam.rules 的最后一条规则
void parseExamLine(const Line& line, bool draw, ExamSpec& exam, std::vector<Issue>& issues) {
    if (!draw) {
        if (line.key == "id") {
            exam.id = trim(line.value);
        } else if (line.key == "title") {
            exam.title = trim(line.value);
        } else if (line.key == "desc") {
            exam.desc = line.value;
        } else if (line.key == "minutes") {
            exam.minutes = std::atoi(line.value.c_str());
            if (exam.minutes < 0) {
                issues.push_back({line.lineNumber, "minutes 不能是负数，已按「不限时」处理"});
                exam.minutes = 0;
            }
        } else if (line.key == "pass") {
            exam.passPercent = std::atoi(line.value.c_str());
            if (exam.passPercent < 0 || exam.passPercent > 100) {
                issues.push_back({line.lineNumber, "及格线只能是 0~100（百分比），已按「不判及格」处理"});
                exam.passPercent = -1;
            }
        } else if (line.key == "seed") {
            exam.seed = std::atoi(line.value.c_str());
            if (exam.seed < 0) {
                issues.push_back({line.lineNumber, "seed 不能是负数，已按 0 处理"});
                exam.seed = 0;
            }
        } else if (line.key == "shuffle") {
            const std::string value = toLower(trim(line.value));
            exam.shuffle = (value == "1" || value == "true" || value == "yes" || value == "是" ||
                            value == "on");
        } else {
            issues.push_back({line.lineNumber, "未知的卷信息键：" + line.key});
        }
        return;
    }

    if (exam.rules.empty()) {
        return;   // 调用方保证 [draw] 时已经有规则了
    }
    DrawRule& rule = exam.rules.back();
    std::string error;
    if (line.key == "count") {
        rule.count = std::atoi(line.value.c_str());
        if (rule.count < 0) {
            issues.push_back({line.lineNumber, "count 不能是负数，已按「不限」处理"});
            rule.count = 0;
        }
    } else if (line.key == "type") {
        if (!parseRuleTypes(line.value, rule, error)) {
            issues.push_back({line.lineNumber, error});
        }
    } else if (line.key == "difficulty") {
        if (!parseDifficultyRange(line.value, rule.difficultyMin, rule.difficultyMax, error)) {
            issues.push_back({line.lineNumber, error});
        }
    } else if (line.key == "tags") {
        rule.tags = split(line.value, ',');
    } else if (line.key == "exclude_tags" || line.key == "exclude") {
        rule.excludeTags = split(line.value, ',');
    } else if (line.key == "material") {
        const std::string value = trim(line.value);
        rule.materialOnly = true;
        if (!value.empty() && toLower(value) != "any" && value != "任意") {
            rule.materialId = value;   // 指定某一段材料
        }
    } else if (line.key == "id") {
        rule.ids = split(line.value, ',');
    } else if (line.key == "score") {
        rule.score = std::atoi(line.value.c_str());
        if (rule.score < 0) {
            issues.push_back({line.lineNumber, "分值不能是负数，已按「用题目自带的分值」处理"});
            rule.score = 0;
        }
    } else {
        issues.push_back({line.lineNumber, "未知的抽题规则键：" + line.key});
    }
}

/// 校验一道题并把问题写进 issues
void validateQuestion(const Question& question, int line, std::vector<Issue>& issues, bool& fatal) {
    if (trim(question.stem).empty()) {
        issues.push_back({line, "题干为空（缺少 stem）"});
        fatal = true;
    }
    if (question.type == QuestionType::Judge) {
        if (question.answer.size() != 1) {
            issues.push_back({line, "判断题必须有且只有一个答案（T 或 F）"});
            fatal = true;
        }
        return;
    }
    if (question.type == QuestionType::Fill) {
        if (question.fillAnswers.empty()) {
            issues.push_back(
                {line, "填空题缺少 answer（写答案文字；多个空用 ; 分隔，同一个空的多个可接受答案用 | 分隔）"});
            fatal = true;
            return;
        }
        for (size_t i = 0; i < question.fillAnswers.size(); ++i) {
            if (question.fillAnswers[i].empty()) {
                issues.push_back({line, "填空题第 " + toText(static_cast<int>(i) + 1) + " 个空没有答案"});
                fatal = true;
            }
        }
        return;
    }
    if (question.options.size() < 2) {
        issues.push_back({line, "选择题至少要有 2 个 option"});
        fatal = true;
    }
    if (question.answer.empty()) {
        issues.push_back({line, "缺少 answer"});
        fatal = true;
        return;
    }
    for (int index : question.answer) {
        if (index < 0 || index >= static_cast<int>(question.options.size())) {
            issues.push_back({line, "answer 指向了不存在的选项：" + indexToLetter(index)});
            fatal = true;
        }
    }
    if (question.type == QuestionType::Single && question.answer.size() != 1) {
        issues.push_back({line, "单选题只能有一个答案（多选请把 type 改成 multiple）"});
        fatal = true;
    }
}

}  // namespace

LoadResult loadBank(const std::string& text, const std::string& sourceName) {
    LoadResult result;
    result.bank.meta.sourceName = sourceName;

    std::vector<Issue> syntaxIssues;
    const std::vector<Line> lines = parse(text, syntaxIssues);
    result.issues = syntaxIssues;

    /// 读到 answer 行时先存原文，等这道题的字段全部读完了再解析：
    /// 这样 type 写在 answer 后面也能正确处理，也让「type = 材料题」能按数据判断题型。
    struct PendingQuestion {
        std::string answer;
        int answerLine = 0;
        bool answerSeen = false;
        bool materialTyped = false;   // 题目里写的是 type = 材料题 / material
        int declaredBlanks = 0;       // 填空题写的 blanks = N（可省）
    };

    enum class Section { None, Meta, Material, Question, Break, Exam, Draw };
    Section section = Section::None;
    Question* current = nullptr;
    PendingQuestion* currentPending = nullptr;
    std::vector<PendingQuestion> pendingAnswers;
    Material* currentMaterial = nullptr;
    // 材料分组：从 [material] 开始，到 [break]（或下一段 [material]、文件结束）为止，
    // 中间的小题自动挂到这段材料上，不必每题都写 material = <id>
    int openMaterial = -1;
    std::vector<int> questionMaterial;   // 与题目一一对应：属于哪段材料，-1 = 不在分组里
    int questionLine = 0;
    int autoIndex = 0;
    int currentExam = -1;   // 正在读的卷（[exam]）在 exams 里的下标；[draw] 归到它
    int autoMaterialIndex = 0;
    std::string currentSectionName;

    for (const Line& line : lines) {
        if (line.isSection) {
            currentSectionName = line.section;
            if (line.section == "meta") {
                section = Section::Meta;
                current = nullptr;
                currentMaterial = nullptr;
            } else if (line.section == "material" || line.section == "材料") {
                section = Section::Material;
                current = nullptr;
                result.bank.materials.push_back(Material{});
                currentMaterial = &result.bank.materials.back();
                ++autoMaterialIndex;
                currentMaterial->id = "m" + toText(autoMaterialIndex);
                openMaterial = static_cast<int>(result.bank.materials.size()) - 1;
            } else if (line.section == "break" || line.section == "分隔") {
                // [break]：材料题结束。后面（到下一段 [material] 或文件末尾）的题目不再属于这段材料
                section = Section::None;
                current = nullptr;
                currentMaterial = nullptr;
                openMaterial = -1;
            } else if (line.section == "exam" || line.section == "卷") {
                // [exam]：一套卷的声明（可选段；一个题库可以写好几套）
                section = Section::Exam;
                current = nullptr;
                currentMaterial = nullptr;
                result.bank.exams.push_back(ExamSpec{});
                currentExam = static_cast<int>(result.bank.exams.size()) - 1;
            } else if (line.section == "draw" || line.section == "抽题") {
                // [draw]：一条抽题规则，归属到前面那套 [exam]
                current = nullptr;
                currentMaterial = nullptr;
                if (currentExam < 0) {
                    section = Section::None;
                    result.issues.push_back({line.lineNumber, "[draw] 抽题规则前面要先写 [exam] 卷声明"});
                } else {
                    section = Section::Draw;
                    DrawRule rule;
                    rule.line = line.lineNumber;
                    result.bank.exams[static_cast<size_t>(currentExam)].rules.push_back(rule);
                }
            } else if (line.section == "q" || line.section == "question") {
                section = Section::Question;
                currentMaterial = nullptr;
                result.bank.questions.push_back(Question{});
                current = &result.bank.questions.back();
                pendingAnswers.push_back(PendingQuestion{});
                currentPending = &pendingAnswers.back();
                questionMaterial.push_back(openMaterial);
                ++autoIndex;
                current->id = "q" + toText(autoIndex);
                questionLine = line.lineNumber;
            } else {
                section = Section::None;
                current = nullptr;
                currentMaterial = nullptr;
                result.issues.push_back({line.lineNumber, "未知的段落 [" + line.section + "]"});
            }
            continue;
        }

        if (section == Section::None) {
            result.issues.push_back({line.lineNumber, "这一行不在任何 [段落] 里，已忽略"});
            continue;
        }

        if (section == Section::Meta) {
            BankMeta& meta = result.bank.meta;
            if (line.key == "id") {
                meta.id = trim(line.value);
            } else if (line.key == "title") {
                meta.title = trim(line.value);
            } else if (line.key == "lang") {
                meta.lang = trim(line.value);
            } else if (line.key == "desc") {
                meta.desc = trim(line.value);
            } else if (line.key == "version") {
                meta.version = std::atoi(line.value.c_str());
            } else {
                result.issues.push_back({line.lineNumber, "未知的题库信息键：" + line.key});
            }
            continue;
        }

        // 材料段落
        if (section == Section::Material) {
            if (currentMaterial != nullptr) {
                if (line.key == "id") {
                    currentMaterial->id = trim(line.value);
                } else if (line.key == "title") {
                    currentMaterial->title = line.value;
                } else if (line.key == "text" || line.key == "body" || line.key == "stem") {
                    // 允许多行 text = 自动拼成一段材料
                    if (!currentMaterial->text.empty()) {
                        currentMaterial->text += "\n";
                    }
                    currentMaterial->text += line.value;
                } else {
                    result.issues.push_back({line.lineNumber, "未知的材料键：" + line.key});
                }
            }
            continue;
        }

        // 卷声明 / 抽题规则（考试模式用；和 [material] 一样是可选段）
        if (section == Section::Exam || section == Section::Draw) {
            if (currentExam >= 0 && currentExam < static_cast<int>(result.bank.exams.size())) {
                parseExamLine(line, section == Section::Draw,
                              result.bank.exams[static_cast<size_t>(currentExam)], result.issues);
            }
            continue;
        }

        // 题目段落
        if (current == nullptr) {
            continue;
        }
        std::string base;
        std::string lang;
        if (splitLocalizedKey(line.key, base, lang)) {
            // 预留的多语言内容：stem.en / analysis.en
            current->localized[base + "@" + lang] = line.value;
            continue;
        }
        if (line.key == "id") {
            current->id = trim(line.value);
        } else if (line.key == "type") {
            QuestionType type = QuestionType::Single;
            if (parseQuestionType(line.value, type)) {
                current->type = type;
                if (currentPending != nullptr && type == QuestionType::Material) {
                    currentPending->materialTyped = true;
                }
            } else {
                result.issues.push_back(
                    {line.lineNumber,
                     "未知题型：" + trim(line.value) +
                         "（可用 single/multiple/judge/fill/indeterminate/ordered/material，"
                         "见 docs/题库格式.md 的「题型一览」）"});
            }
        } else if (line.key == "stem") {
            current->stem = line.value;
        } else if (line.key == "option") {
            current->options.push_back(line.value);
        } else if (line.key == "answer") {
            // 先存原文；等这道题的字段都读完了、题型定下来了再解析（见下面的「解析答案」）
            if (currentPending != nullptr) {
                currentPending->answer = line.value;
                currentPending->answerLine = line.lineNumber;
                currentPending->answerSeen = true;
            }
        } else if (line.key == "blanks") {
            // 填空题的空个数（只用来核对；真正的空数以答案为准）
            if (currentPending != nullptr) {
                currentPending->declaredBlanks = std::atoi(line.value.c_str());
            }
        } else if (line.key == "blank") {
            current->blanks.push_back(line.value);
        } else if (line.key == "material") {
            current->materialId = trim(line.value);
        } else if (line.key == "analysis") {
            current->analysis = line.value;
        } else if (line.key == "tags") {
            current->tags = split(line.value, ',');
        } else if (line.key == "difficulty") {
            current->difficulty = std::atoi(line.value.c_str());
        } else if (line.key == "score") {
            // 分值：考试模式按它算分（默认 1 分）
            current->score = std::atoi(line.value.c_str());
            if (current->score <= 0) {
                result.issues.push_back({line.lineNumber, "分值要大于 0，已按 1 分处理"});
                current->score = 1;
            }
        } else if (line.key == "source") {
            current->source = line.value;
        } else {
            result.issues.push_back({line.lineNumber, "未知的题目键：" + line.key});
        }
    }

    // ---- 补齐缺省信息 + 逐题校验 ----
    if (result.bank.meta.id.empty()) {
        result.bank.meta.id = sourceName.empty() ? "unnamed" : sourceName;
        result.issues.push_back({0, "题库缺少 id，已用文件名代替：" + result.bank.meta.id});
    }
    if (result.bank.meta.title.empty()) {
        result.bank.meta.title = result.bank.meta.id;
    }

    // ---- 材料分组：[material] … [break] 之间的小题自动挂到这段材料上 ----
    for (size_t i = 0; i < result.bank.questions.size(); ++i) {
        Question& question = result.bank.questions[i];
        if (!question.materialId.empty() || i >= questionMaterial.size()) {
            continue;   // 自己写了 material = <id> 的以它为准
        }
        const int materialIndex = questionMaterial[i];
        if (materialIndex >= 0 && materialIndex < static_cast<int>(result.bank.materials.size())) {
            question.materialId = result.bank.materials[static_cast<size_t>(materialIndex)].id;
        }
    }

    // ---- 材料题：标出每道小题是「第几小题 / 共几题」，并检查引用的材料存不存在 ----
    for (size_t i = 0; i < result.bank.questions.size(); ++i) {
        Question& question = result.bank.questions[i];
        if (question.materialId.empty()) {
            continue;
        }
        int part = 1;
        int count = 0;
        for (size_t k = 0; k < result.bank.questions.size(); ++k) {
            if (result.bank.questions[k].materialId != question.materialId) {
                continue;
            }
            ++count;
            if (k < i) {
                ++part;
            }
        }
        question.materialPart = part;
        question.materialCount = count;
    }
    for (const Question& question : result.bank.questions) {
        if (!question.materialId.empty() && result.bank.materialById(question.materialId) == nullptr) {
            result.issues.push_back(
                {0, "题目引用了不存在的材料：" + question.materialId + "（要在前面写 [material] 段并给 id）"});
        }
    }

    // 记录每道题的起始行，便于校验报错定位（pairs: 题目下标 → 行号）
    std::vector<int> questionLines;
    {
        for (const Line& line : lines) {
            if (line.isSection && (line.section == "q" || line.section == "question")) {
                questionLines.push_back(line.lineNumber);
            }
        }
    }
    // ---- 解析答案 ----
    // 放在这里（而不是读到 answer 行时立刻解析）有两个好处：
    //   1) 判断题 / 填空题 / 有序题的答案格式依赖 type，type 写在后面也能解析；
    //   2) 「type = 材料题」可以等数据都到齐了再决定这道小题到底是什么题型。
    for (size_t i = 0; i < result.bank.questions.size(); ++i) {
        Question& question = result.bank.questions[i];
        const int line = i < questionLines.size() ? questionLines[i] : questionLine;
        PendingQuestion pending;
        if (i < pendingAnswers.size()) {
            pending = pendingAnswers[i];
        }

        if (pending.materialTyped) {
            question.type = resolveMaterialType(question, pending.answer);
            if (question.materialId.empty()) {
                result.issues.push_back(
                    {line, "材料题缺少材料：写在 [material] … [break] 之间，或写 material = <材料id>"});
            }
        }
        if (!pending.answerSeen) {
            continue;
        }

        std::string error;
        std::vector<int> indices;
        if (question.type == QuestionType::Fill) {
            question.fillAnswers = parseFillAnswer(pending.answer);
            if (pending.declaredBlanks > 0) {
                if (pending.declaredBlanks > question.fillBlankCount()) {
                    // 声明的空比答案多：补上空位，让界面能显示出来，并提示缺答案
                    question.fillAnswers.resize(static_cast<size_t>(pending.declaredBlanks));
                    result.issues.push_back(
                        {pending.answerLine,
                         "blanks 写了 " + toText(pending.declaredBlanks) + " 个空，但只有 " +
                             toText(static_cast<int>(question.fillAnswers.size())) +
                             " 个空有答案（缺的会算作没答案，导入时会报错）"});
                } else if (pending.declaredBlanks != question.fillBlankCount()) {
                    result.issues.push_back(
                        {pending.answerLine,
                         "blanks 写了 " + toText(pending.declaredBlanks) + "，答案里是 " +
                             toText(question.fillBlankCount()) + " 个空（以答案为准）"});
                }
            }
        } else if (question.type == QuestionType::Judge) {
            int judgeIndex = 0;
            if (parseJudgeAnswer(pending.answer, judgeIndex, error)) {
                indices.push_back(judgeIndex);
            }
        } else if (question.type == QuestionType::Ordered) {
            if (!parseOrderedAnswer(pending.answer, indices, error)) {
                // 解析失败时 answer 为空，validateQuestion 会再报一次「缺少 answer」
            }
        } else if (!parseChoiceAnswer(pending.answer, indices, error)) {
            // 解析失败时 answer 为空，validateQuestion 会再报一次「缺少 answer」
        }
        if (!error.empty()) {
            result.issues.push_back({pending.answerLine, error});
        }
        question.answer = indices;
    }

    for (size_t i = 0; i < result.bank.questions.size(); ++i) {
        const int line = i < questionLines.size() ? questionLines[i] : questionLine;
        validateQuestion(result.bank.questions[i], line, result.issues, result.hasFatalError);
    }
    if (result.bank.questions.empty()) {
        result.issues.push_back({0, "题库里没有任何 [q] 题目"});
        result.hasFatalError = true;
    }

    // ---- 卷声明：补默认值 + 检查 ----
    // 考试模式完全依赖 [exam]：这里报得细一点，导入时就能看见问题（都不算致命错误）
    for (size_t i = 0; i < result.bank.exams.size(); ++i) {
        ExamSpec& exam = result.bank.exams[i];
        if (exam.id.empty()) {
            exam.id = "exam" + toText(static_cast<int>(i) + 1);
        }
        if (exam.title.empty()) {
            exam.title = exam.id;
        }
        if (exam.rules.empty()) {
            result.issues.push_back({0, "卷「" + exam.title + "」没有 [draw] 抽题规则，考试模式用不了它"});
        }
        for (size_t k = 0; k < exam.rules.size(); ++k) {
            const DrawRule& rule = exam.rules[k];
            if (rule.count == 0 && rule.ids.empty()) {
                result.issues.push_back({rule.line, "第 " + toText(static_cast<int>(k) + 1) +
                                                        " 条抽题规则没写 count，按「能抽多少抽多少」处理"});
            }
        }
        for (size_t k = 0; k < i; ++k) {
            if (result.bank.exams[k].id == exam.id) {
                result.issues.push_back({0, "卷 id 重复了：" + exam.id + "（导出与记进度会串）"});
            }
        }
    }

    // 在这里就把结论定下来，避免调用方 move 走 bank 之后再判断
    result.parsedOk = !result.hasFatalError && !result.bank.questions.empty();
    return result;
}

std::string saveBank(const QuestionBank& bank) {
    std::string out;
    out += "# ============================================================================\n";
    out += "# 题库文件（QuizApp 固定格式，v1）\n";
    out += "# 语法：# 是注释；[meta] 题库信息；[q] 一道题；key = value；值里 \\n 表示换行\n";
    out += "# ============================================================================\n\n";

    out += "[meta]\n";
    out += "id = " + escape(bank.meta.id) + "\n";
    out += "title = " + escape(bank.meta.title) + "\n";
    out += "lang = " + escape(bank.meta.lang) + "\n";
    out += "version = " + toText(bank.meta.version) + "\n";
    if (!bank.meta.desc.empty()) {
        out += "desc = " + escape(bank.meta.desc) + "\n";
    }

    // 材料段写在题目前面（题目用 material = <id> 引用）
    for (const Material& material : bank.materials) {
        out += "\n[material]\n";
        out += "id = " + escape(material.id) + "\n";
        if (!material.title.empty()) {
            out += "title = " + escape(material.title) + "\n";
        }
        out += "text = " + escape(material.text) + "\n";
    }

    for (size_t i = 0; i < bank.questions.size(); ++i) {
        const Question& question = bank.questions[i];
        out += "\n[q]\n";
        out += "id = " + escape(question.id.empty() ? ("q" + toText(static_cast<int>(i) + 1)) : question.id) + "\n";
        out += std::string("type = ") + toString(question.type) + "\n";
        if (!question.materialId.empty()) {
            out += "material = " + escape(question.materialId) + "\n";
        }
        out += "stem = " + escape(question.stem) + "\n";
        for (const auto& option : question.options) {
            out += "option = " + escape(option) + "\n";
        }
        for (const auto& blank : question.blanks) {
            out += "blank = " + escape(blank) + "\n";
        }
        if (question.type == QuestionType::Judge) {
            out += std::string("answer = ") + (question.isJudgeTrue() ? "T" : "F") + "\n";
        } else if (question.type == QuestionType::Fill) {
            if (question.fillBlankCount() > 1) {
                out += "blanks = " + toText(question.fillBlankCount()) + "\n";
            }
            std::string answer;
            for (size_t k = 0; k < question.fillAnswers.size(); ++k) {
                if (k > 0) {
                    answer += " ; ";
                }
                for (size_t a = 0; a < question.fillAnswers[k].size(); ++a) {
                    if (a > 0) {
                        answer += "|";
                    }
                    answer += escape(question.fillAnswers[k][a]);
                }
            }
            out += "answer = " + answer + "\n";
        } else {
            std::string answer;
            for (size_t k = 0; k < question.answer.size(); ++k) {
                if (k > 0) {
                    answer += ",";
                }
                answer += indexToLetter(question.answer[k]);
            }
            out += "answer = " + answer + "\n";
        }
        if (!question.analysis.empty()) {
            out += "analysis = " + escape(question.analysis) + "\n";
        }
        if (!question.tags.empty()) {
            std::string tags;
            for (size_t k = 0; k < question.tags.size(); ++k) {
                if (k > 0) {
                    tags += ",";
                }
                tags += question.tags[k];
            }
            out += "tags = " + escape(tags) + "\n";
        }
        if (question.difficulty > 0) {
            out += "difficulty = " + toText(question.difficulty) + "\n";
        }
        if (question.score > 0 && question.score != 1) {
            out += "score = " + toText(question.score) + "\n";
        }
        if (!question.source.empty()) {
            out += "source = " + escape(question.source) + "\n";
        }
        // 多语言内容（预留）：原样导出
        for (const auto& entry : question.localized) {
            // 内部键是 "stem@en"，文件里写成 "stem.en"（和读取时的规则一致）
            std::string key = entry.first;
            std::replace(key.begin(), key.end(), '@', '.');
            out += key + " = " + escape(entry.second) + "\n";
        }
    }

    // ---- 卷声明（考试模式）写在文件最后：一套卷一个 [exam]，紧跟它自己的 [draw] 规则 ----
    for (const ExamSpec& exam : bank.exams) {
        out += "\n[exam]\n";
        out += "id = " + escape(exam.id) + "\n";
        out += "title = " + escape(exam.title) + "\n";
        if (exam.minutes > 0) {
            out += "minutes = " + toText(exam.minutes) + "\n";
        }
        if (exam.passPercent >= 0) {
            out += "pass = " + toText(exam.passPercent) + "\n";
        }
        if (exam.seed != 0) {
            out += "seed = " + toText(exam.seed) + "\n";
        }
        if (exam.shuffle) {
            out += "shuffle = 1\n";
        }
        if (!exam.desc.empty()) {
            out += "desc = " + escape(exam.desc) + "\n";
        }
        for (const DrawRule& rule : exam.rules) {
            out += "\n[draw]\n";
            if (rule.count > 0) {
                out += "count = " + toText(rule.count) + "\n";
            }
            if (rule.materialOnly) {
                out += "material = " +
                       (rule.materialId.empty() ? std::string("any") : escape(rule.materialId)) + "\n";
            }
            if (!rule.types.empty()) {
                std::string types;
                for (size_t k = 0; k < rule.types.size(); ++k) {
                    if (k > 0) {
                        types += ",";
                    }
                    types += toString(rule.types[k]);
                }
                out += "type = " + types + "\n";
            }
            if (rule.difficultyMin > 0 || rule.difficultyMax > 0) {
                std::string range = rule.difficultyMin > 0 ? toText(rule.difficultyMin) : std::string();
                if (rule.difficultyMax > 0) {
                    range += "-" + toText(rule.difficultyMax);
                } else if (!range.empty()) {
                    range += "-";
                }
                out += "difficulty = " + range + "\n";
            }
            if (!rule.tags.empty()) {
                std::string tags;
                for (size_t k = 0; k < rule.tags.size(); ++k) {
                    if (k > 0) {
                        tags += ",";
                    }
                    tags += escape(rule.tags[k]);
                }
                out += "tags = " + tags + "\n";
            }
            if (!rule.excludeTags.empty()) {
                std::string tags;
                for (size_t k = 0; k < rule.excludeTags.size(); ++k) {
                    if (k > 0) {
                        tags += ",";
                    }
                    tags += escape(rule.excludeTags[k]);
                }
                out += "exclude_tags = " + tags + "\n";
            }
            if (!rule.ids.empty()) {
                std::string ids;
                for (size_t k = 0; k < rule.ids.size(); ++k) {
                    if (k > 0) {
                        ids += ",";
                    }
                    ids += escape(rule.ids[k]);
                }
                out += "id = " + ids + "\n";
            }
            if (rule.score > 0) {
                out += "score = " + toText(rule.score) + "\n";
            }
        }
    }
    return out;
}

namespace {

QuestionBank buildSubsetBank(const QuestionBank& bank, const BankProgress& progress,
                             bool keepWrong, bool keepMarked, int wrongRatio,
                             const std::string& suffix, const std::string& titleSuffix) {
    QuestionBank result;
    result.meta = bank.meta;
    result.meta.id = bank.meta.id + suffix;
    result.meta.title = bank.meta.title + titleSuffix;
    result.meta.sourceName = bank.meta.sourceName;

    for (size_t i = 0; i < bank.questions.size(); ++i) {
        bool keep = false;
        if (i < progress.questions.size()) {
            const QuestionProgress& item = progress.questions[i];
            if (keepWrong && isWrongBookItem(item, wrongRatio)) {
                keep = true;
            }
            if (keepMarked && item.marked) {
                keep = true;
            }
        }
        if (keep) {
            result.questions.push_back(bank.questions[i]);
        }
    }
    return result;
}

}  // namespace

QuestionBank buildWrongOnlyBank(const QuestionBank& bank, const BankProgress& progress,
                               int ratio) {
    return buildSubsetBank(bank, progress, true, false, ratio, "-wrong", "（错题本）");
}

QuestionBank buildMarkedOnlyBank(const QuestionBank& bank, const BankProgress& progress) {
    return buildSubsetBank(bank, progress, false, true, 3, "-marked", "（收藏题）");
}

QuestionBank buildTaggedBank(const QuestionBank& bank, const BankProgress& progress,
                             const std::vector<std::string>& tags, bool bakeUserTags) {
    QuestionBank result;
    result.meta = bank.meta;
    result.meta.id = bank.meta.id + "-tags";
    result.meta.title = bank.meta.title + "（标签题）";
    result.meta.sourceName = bank.meta.sourceName;

    const auto hasTag = [](const std::vector<std::string>& list, const std::string& tag) {
        for (const std::string& item : list) {
            if (item == tag) {
                return true;
            }
        }
        return false;
    };

    for (size_t i = 0; i < bank.questions.size(); ++i) {
        const Question& question = bank.questions[i];
        std::vector<std::string> userTags;
        if (i < progress.questions.size()) {
            userTags = progress.questions[i].tags;
        }
        bool keep = false;
        for (const std::string& tag : tags) {
            if (hasTag(question.tags, tag) || hasTag(userTags, tag)) {
                keep = true;
                break;
            }
        }
        if (!keep) {
            continue;
        }
        Question copy = question;
        if (bakeUserTags) {
            for (const std::string& tag : userTags) {
                if (!hasTag(copy.tags, tag)) {
                    copy.tags.push_back(tag);   // 固化：用户标签变成题目自带的标签
                }
            }
        }
        result.questions.push_back(std::move(copy));
    }
    return result;
}

const std::vector<std::string>& bankFileExtensions() {
    static const std::vector<std::string> kExtensions = {".quiz", ".txt", ".text"};
    return kExtensions;
}

namespace {

/// 取小写后缀（含点）；没有后缀就返回空串
std::string lowerSuffixOf(const std::string& name) {
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0) {
        return std::string();
    }
    std::string suffix = name.substr(dot);
    for (char& ch : suffix) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return suffix;
}

}  // namespace

bool isBankTextFileName(const std::string& fileName) {
    const std::string suffix = lowerSuffixOf(fileName);
    for (const std::string& known : bankFileExtensions()) {
        if (suffix == known) {
            return true;
        }
    }
    return false;
}

std::string normalizeBankFileName(const std::string& fileName) {
    // 1) 去掉路径（Windows 的 \ 与其它平台的 / 都处理）
    size_t slash = fileName.find_last_of("\\/");
    std::string name = slash == std::string::npos ? fileName : fileName.substr(slash + 1);
    // 2) 先去首尾空白，再判后缀（“  .hidden  ”这种是隐藏文件，不算带后缀）
    while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) {
        name.erase(name.begin());
    }
    while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) {
        name.pop_back();
    }
    // 3) 去掉已有后缀（.quiz / .txt / .text 都算）
    const size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0) {
        name = name.substr(0, dot);
    }
    // 4) 换成合法文件名字符
    for (char& ch : name) {
        if (ch == '\\' || ch == '/' || ch == ':' || ch == '*' || ch == '?' || ch == '"' ||
            ch == '<' || ch == '>' || ch == '|') {
            ch = '_';
        }
    }
    while (!name.empty() && (name.front() == ' ' || name.front() == '.')) {
        name.erase(name.begin());
    }
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) {
        name.pop_back();
    }
    if (name.empty()) {
        name = "imported";
    }
    return name + ".quiz";
}

std::vector<Issue> validateBank(const std::string& text) {
    LoadResult result = loadBank(text, "validate");
    if (result.hasFatalError) {
        result.issues.push_back({0, "题库存在致命错误，无法使用"});
    }
    return result.issues;
}

}  // namespace quiz::io

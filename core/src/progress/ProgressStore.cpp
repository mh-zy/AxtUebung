#include "quiz/progress/ProgressStore.h"

#include <cstdio>
#include <cstdlib>

#include "quiz/io/TextDocument.h"

namespace quiz::progress {

namespace {

std::string tagsToText(const std::vector<std::string>& tags) {
    std::string out;
    for (size_t i = 0; i < tags.size(); ++i) {
        if (i > 0) {
            out += ",";
        }
        out += tags[i];
    }
    return out;
}

std::string scoreToText(float score) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%.3f", static_cast<double>(score));
    return std::string(buffer);
}

bool toBool(const std::string& text) {
    const std::string value = io::trim(text);
    return value == "1" || value == "true" || value == "yes" || value == "是";
}

}  // namespace

std::string serialize(const BankProgress& progress) {
    std::string out;
    out += "# QuizApp 进度文件（纯文本，可手工编辑）\n";
    out += "# 只记「历史结论」：答过没有、最后一次对不对、对/错各几次、时间、收藏、标签。\n";
    out += "# 选了什么 / 填了什么不写盘（只在内存里），所以这里看不到作答内容。\n\n";
    out += "[bank]\n";
    out += "id = " + io::escape(progress.bankId) + "\n";
    out += "updated = " + io::escape(progress.updatedAt) + "\n";
    out += "position = " + std::to_string(progress.position) + "\n";

    for (size_t i = 0; i < progress.questions.size(); ++i) {
        const QuestionProgress& item = progress.questions[i];
        // 完全没碰过的题不写，进度文件更短、更易读
        // （只加了标签、还没作答的题也要写，不然标签会丢）
        if (!item.answered && !item.marked && item.tags.empty()) {
            continue;
        }
        out += "\n[answer]\n";
        out += "index = " + std::to_string(i) + "\n";
        out += std::string("answered = ") + (item.answered ? "1" : "0") + "\n";
        out += std::string("correct = ") + (item.correct ? "1" : "0") + "\n";
        if (item.rightCount > 0) {
            out += "right = " + std::to_string(item.rightCount) + "\n";
        }
        if (item.wrongCount > 0) {
            out += "wrong = " + std::to_string(item.wrongCount) + "\n";
        }
        if (!item.tags.empty()) {
            out += "tags = " + io::escape(tagsToText(item.tags)) + "\n";
        }
        if (item.score > 0.0f && item.score < 1.0f) {
            out += "score = " + scoreToText(item.score) + "\n";
        }
        out += "time = " + io::escape(item.answeredAt) + "\n";
        out += std::string("marked = ") + (item.marked ? "1" : "0") + "\n";
    }
    return out;
}

BankProgress deserialize(const std::string& text, const std::string& bankId, int questionCount) {
    BankProgress result;
    result.bankId = bankId;

    std::vector<io::Issue> issues;
    const std::vector<io::Line> lines = io::parse(text, issues);

    enum class Section { None, Bank, Answer };
    Section section = Section::None;
    QuestionProgress current;
    int currentIndex = -1;

    auto flush = [&]() {
        if (currentIndex >= 0) {
            if (result.questions.empty()) {
                result.questions.resize(static_cast<size_t>(questionCount > currentIndex ? questionCount : currentIndex + 1));
            }
            if (currentIndex < static_cast<int>(result.questions.size())) {
                result.questions[static_cast<size_t>(currentIndex)] = current;
            }
        }
    };

    for (const io::Line& line : lines) {
        if (line.isSection) {
            flush();
            current = QuestionProgress{};
            currentIndex = -1;
            section = line.section == "bank" ? Section::Bank
                                              : (line.section == "answer" ? Section::Answer : Section::None);
            continue;
        }
        if (section == Section::Bank) {
            if (line.key == "id") {
                result.bankId = io::trim(line.value);
            } else if (line.key == "updated") {
                result.updatedAt = line.value;
            } else if (line.key == "position") {
                result.position = std::atoi(line.value.c_str());
            }
        } else if (section == Section::Answer) {
            if (line.key == "index") {
                currentIndex = std::atoi(line.value.c_str());
            } else if (line.key == "answered") {
                current.answered = toBool(line.value);
            } else if (line.key == "correct") {
                current.correct = toBool(line.value);
            } else if (line.key == "right") {
                current.rightCount = std::atoi(line.value.c_str());
            } else if (line.key == "wrong") {
                current.wrongCount = std::atoi(line.value.c_str());
            } else if (line.key == "score") {
                current.score = static_cast<float>(std::atof(line.value.c_str()));
            } else if (line.key == "tags") {
                current.tags = io::split(line.value, ',');
            } else if (line.key == "time") {
                current.answeredAt = line.value;
            } else if (line.key == "marked") {
                current.marked = toBool(line.value);
            }
        }
    }
    flush();

    result.resizeTo(questionCount);
    for (auto& item : result.questions) {
        if (item.answered && item.correct && item.score <= 0.0f) {
            item.score = 1.0f;   // 老进度文件没有 score 字段，按「全对」补上
        }
    }
    return result;
}

}  // namespace quiz::progress

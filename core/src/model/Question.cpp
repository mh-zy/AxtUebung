#include "quiz/model/Question.h"

#include <cctype>

#include "quiz/io/TextDocument.h"

namespace quiz {

const char* toString(QuestionType type) {
    switch (type) {
        case QuestionType::Single: return "single";
        case QuestionType::Multiple: return "multiple";
        case QuestionType::Judge: return "judge";
        case QuestionType::Fill: return "fill";
        case QuestionType::Indeterminate: return "indeterminate";
        case QuestionType::Ordered: return "ordered";
        case QuestionType::Material: return "material";
    }
    return "single";
}

bool parseQuestionType(const std::string& text, QuestionType& out) {
    const std::string value = io::trim(text);
    if (value == "single" || value == "单选" || value == "1") {
        out = QuestionType::Single;
        return true;
    }
    if (value == "multiple" || value == "多选" || value == "2") {
        out = QuestionType::Multiple;
        return true;
    }
    if (value == "judge" || value == "判断" || value == "3") {
        out = QuestionType::Judge;
        return true;
    }
    if (value == "fill" || value == "blank" || value == "fillin" || value == "填空" ||
        value == "填空题" || value == "4") {
        out = QuestionType::Fill;
        return true;
    }
    if (value == "indeterminate" || value == "partial" || value == "不定项" ||
        value == "不定项选择" || value == "不定项选择题" || value == "5") {
        out = QuestionType::Indeterminate;
        return true;
    }
    if (value == "ordered" || value == "sequence" || value == "sort" || value == "排序" ||
        value == "有序" || value == "多项有序" || value == "多项有序选择题" || value == "七选五" ||
        value == "配对" || value == "6") {
        out = QuestionType::Ordered;
        return true;
    }
    if (value == "material" || value == "passage" || value == "材料" || value == "材料题") {
        out = QuestionType::Material;
        return true;
    }
    return false;
}

std::string indexToLetter(int index) {
    if (index < 0 || index > 25) {
        return "?";
    }
    return std::string(1, static_cast<char>('A' + index));
}

int letterToIndex(const std::string& letter) {
    if (letter.empty()) {
        return -1;
    }
    const char c = letter[0];
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a';
    }
    // 也允许直接用数字 1、2、3 表示选项（手写题库时更省事）
    if (c >= '1' && c <= '9') {
        return c - '1';
    }
    return -1;
}

const std::string& Question::stemFor(const std::string& lang) const {
    const auto it = localized.find("stem@" + lang);
    return it != localized.end() ? it->second : stem;
}

const std::string& Question::analysisFor(const std::string& lang) const {
    const auto it = localized.find("analysis@" + lang);
    return it != localized.end() ? it->second : analysis;
}

}  // namespace quiz

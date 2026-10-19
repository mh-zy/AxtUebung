#include "quiz/model/Scoring.h"

#include <algorithm>

namespace quiz {

namespace {

bool contains(const std::vector<int>& list, int value) {
    return std::find(list.begin(), list.end(), value) != list.end();
}

bool isSpaceByte(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

}  // namespace

std::string normalizeFillText(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            ++i;
            if (isSpaceByte(c)) {
                continue;   // 空白一律忽略：「H2O 」「H 2 O」都算同一个答案
            }
            char lower = static_cast<char>(c);
            if (c >= 'A' && c <= 'Z') {
                lower = static_cast<char>(c - 'A' + 'a');
            }
            out.push_back(lower);
            continue;
        }

        // 多字节 UTF-8：只处理全角 ！～ 与全角空格，其余原样保留
        int extra = 0;
        unsigned int code = 0;
        if ((c & 0xE0) == 0xC0) {
            extra = 1;
            code = c & 0x1Fu;
        } else if ((c & 0xF0) == 0xE0) {
            extra = 2;
            code = c & 0x0Fu;
        } else if ((c & 0xF8) == 0xF0) {
            extra = 3;
            code = c & 0x07u;
        } else {
            out.push_back(text[i]);
            ++i;
            continue;
        }
        if (i + static_cast<size_t>(extra) >= text.size()) {
            out.push_back(text[i]);
            ++i;
            continue;
        }
        bool valid = true;
        for (int k = 1; k <= extra; ++k) {
            const unsigned char cc = static_cast<unsigned char>(text[i + static_cast<size_t>(k)]);
            if ((cc & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            code = (code << 6) | (cc & 0x3Fu);
        }
        if (!valid) {
            out.push_back(text[i]);
            ++i;
            continue;
        }
        if (code == 0x3000u) {
            i += static_cast<size_t>(extra) + 1;   // 全角空格同样忽略
            continue;
        }
        if (code >= 0xFF01u && code <= 0xFF5Eu) {
            char half = static_cast<char>(code - 0xFEE0u);
            if (half >= 'A' && half <= 'Z') {
                half = static_cast<char>(half - 'A' + 'a');
            }
            out.push_back(half);
        } else {
            out.append(text, i, static_cast<size_t>(extra) + 1);
        }
        i += static_cast<size_t>(extra) + 1;
    }
    return out;
}

Score scoreChoice(const Question& question, const std::vector<int>& chosen) {
    Score result;
    if (chosen.empty() || question.answer.empty()) {
        return result;
    }
    std::vector<int> expected = question.answer;
    std::vector<int> picked = chosen;
    std::sort(expected.begin(), expected.end());
    expected.erase(std::unique(expected.begin(), expected.end()), expected.end());
    std::sort(picked.begin(), picked.end());
    picked.erase(std::unique(picked.begin(), picked.end()), picked.end());

    if (question.type == QuestionType::Single || question.type == QuestionType::Judge ||
        question.type == QuestionType::Material) {
        result.full = picked.size() == 1 && picked.front() == expected.front();
        result.value = result.full ? 1.0f : 0.0f;
        return result;
    }

    if (question.type == QuestionType::Indeterminate) {
        for (int index : picked) {
            if (!contains(expected, index)) {
                return result;   // 选到错的：0 分
            }
        }
        result.full = picked == expected;
        result.value = static_cast<float>(picked.size()) / static_cast<float>(expected.size());
        return result;
    }

    // 多选：全对才算对
    result.full = picked == expected;
    result.value = result.full ? 1.0f : 0.0f;
    return result;
}

Score scoreOrdered(const Question& question, const std::vector<int>& chosen) {
    Score result;
    const int slots = question.slotCount();
    if (slots <= 0 || static_cast<int>(chosen.size()) < slots) {
        return result;
    }
    int hit = 0;
    for (int i = 0; i < slots; ++i) {
        const size_t index = static_cast<size_t>(i);
        if (index < question.answer.size() && chosen[index] == question.answer[index]) {
            ++hit;
        }
    }
    result.value = static_cast<float>(hit) / static_cast<float>(slots);
    result.full = hit == slots;
    return result;
}

bool fillBlankMatches(const Question& question, int blank, const std::string& input) {
    if (blank < 0 || blank >= question.fillBlankCount()) {
        return false;
    }
    const std::string normalized = normalizeFillText(input);
    if (normalized.empty()) {
        return false;
    }
    for (const auto& candidate : question.fillAnswers[static_cast<size_t>(blank)]) {
        if (normalizeFillText(candidate) == normalized) {
            return true;
        }
    }
    return false;
}

Score scoreFill(const Question& question, const std::vector<std::string>& inputs) {
    Score result;
    const int blanks = question.fillBlankCount();
    if (blanks <= 0) {
        return result;   // 没有空的填空题按 0 分算，交给导入校验去报错
    }
    int hit = 0;
    for (int i = 0; i < blanks; ++i) {
        const std::string input = i < static_cast<int>(inputs.size())
                                      ? inputs[static_cast<size_t>(i)]
                                      : std::string();
        if (fillBlankMatches(question, i, input)) {
            ++hit;
        }
    }
    result.value = static_cast<float>(hit) / static_cast<float>(blanks);
    result.full = hit == blanks;
    return result;
}

}  // namespace quiz
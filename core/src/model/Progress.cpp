#include "quiz/model/Progress.h"

namespace quiz {

bool isWrongBookItem(const QuestionProgress& item, int ratio) {
    if (!item.answered) {
        return false;   // 还没答过 → 不是错题
    }
    if (!item.correct) {
        return true;    // 最后一次没答对 → 在错题本里
    }
    // 最后一次答对了，但答对次数还没到答错次数的 ratio 倍 → 仍然算错题（还没巩固）
    const int times = ratio > 0 ? ratio : 3;
    return item.rightCount < times * item.wrongCount;
}

void BankProgress::resizeTo(int questionCount) {
    if (questionCount < 0) {
        questionCount = 0;
    }
    questions.resize(static_cast<size_t>(questionCount));
    if (position < 0) {
        position = 0;
    }
    if (position >= questionCount) {
        position = questionCount > 0 ? questionCount - 1 : 0;
    }
}

int BankProgress::answeredCount() const {
    int count = 0;
    for (const auto& item : questions) {
        if (item.answered) {
            ++count;
        }
    }
    return count;
}

int BankProgress::correctCount() const {
    int count = 0;
    for (const auto& item : questions) {
        if (item.answered && item.correct) {
            ++count;
        }
    }
    return count;
}

int BankProgress::wrongCount() const {
    int count = 0;
    for (const auto& item : questions) {
        if (item.answered && !item.correct) {
            ++count;
        }
    }
    return count;
}

}  // namespace quiz

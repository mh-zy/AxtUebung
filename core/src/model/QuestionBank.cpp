#include "quiz/model/QuestionBank.h"

namespace quiz {

const Question* QuestionBank::findById(const std::string& id) const {
    for (const auto& question : questions) {
        if (question.id == id) {
            return &question;
        }
    }
    return nullptr;
}

const Material* QuestionBank::materialById(const std::string& id) const {
    for (const auto& material : materials) {
        if (material.id == id) {
            return &material;
        }
    }
    return nullptr;
}

int QuestionBank::countOfType(QuestionType type) const {
    int count = 0;
    for (const auto& question : questions) {
        if (question.type == type) {
            ++count;
        }
    }
    return count;
}

}  // namespace quiz

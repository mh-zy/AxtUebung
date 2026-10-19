#pragma once

#include <string>
#include <vector>

namespace quiz {

/// 单题的答题记录
/// 注意：这里**不存用户选了什么 / 填了什么**。作答只活在内存里（QuizSession 的草稿），
/// 退出题库或关掉程序就释放 —— 重新进来看到的是一道干净的题，而不是上次的判分结果。
/// 这里只留「历史结论」：答过没有、最后一次对不对、对/错各几次、什么时候答的、收藏与标签。
struct QuestionProgress {
    bool answered = false;
    bool correct = false;
    std::vector<std::string> tags;         // 用户自己给这道题加的标签（题库自带的在 Question 里）
    float score = 0.0f;        // 得分 0~1（不定项与有序题可能既不是 0 也不是 1）
    std::string answeredAt;    // 答题时间（文本，便于直接阅读进度文件）
    bool marked = false;       // 收藏 / 标记（可做「重点题」练习）
    int rightCount = 0;        // 一共答对过几次（每次「提交答案」累加）
    int wrongCount = 0;        // 一共答错过几次

    /// 答了但没全对，却拿到了部分分
    bool partiallyCorrect() const { return answered && !correct && score > 0.0f; }
};

/// 「错题本」里有没有这道题：
///   * 没答过 → 不是；
///   * 最后一次没答对（含只拿部分分的）→ 是；
///   * 最后一次答对了，但「答对次数」还没到「答错次数」的 ratio 倍 → 仍然留着（这就是被动移出的门槛）。
/// 也就是说：答错一次之后，要连着答对到比例达标才自动移出（默认 3 倍）。
/// ratio <= 0 时按 3 倍算。
bool isWrongBookItem(const QuestionProgress& item, int ratio = 3);

/// 一个题库的进度
struct BankProgress {
    std::string bankId;                       // 对应 BankMeta::id
    std::string updatedAt;                    // 最近保存时间
    int position = 0;                         // 上次停留在第几题（从 0 开始）
    std::vector<QuestionProgress> questions;  // 与题库题目一一对应

    /// 题目数量变化时（题库被编辑过）对齐记录：多出来的丢弃，少的补齐
    void resizeTo(int questionCount);

    int answeredCount() const;
    int correctCount() const;
    int wrongCount() const;
};

}  // namespace quiz

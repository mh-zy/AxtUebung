#pragma once

#include <string>
#include <vector>

#include "quiz/model/Progress.h"
#include "quiz/model/QuestionBank.h"

// ============================================================================
// 答题会话：把「题库 + 进度 + 当前作答」封装成一个可测试的状态机
// ----------------------------------------------------------------------------
// UI 只做两件事：把用户操作翻译成这里的函数调用，把这里的状态画出来。
// 所以换 UI（ImGui / 自绘 / 以后换 SDL）都不需要改这里。
//
// 草稿（**本次会话**的作答）按题目分开存，所以材料题的「卷面」可以一页里
// 同时填好几道小题：每题各填各的，交掉哪题就把哪题写进进度。
//
// 但「作答内容」只活在内存里：进度只记历史结论（答过没有 / 最后对错 / 对错次数 /
// 时间 / 收藏 / 标签）。重新打开题库时，所有题都是干净的 —— 不回放上次的选项与判分。
// ============================================================================
namespace quiz {

/// 一次练习的统计
struct SessionStats {
    int total = 0;       // 题目总数
    int answered = 0;    // 已作答
    int correct = 0;     // 全对
    int wrong = 0;       // 没有全对（含只拿到部分分的）
    int partial = 0;     // 其中「拿到部分分」的题数
    int marked = 0;      // 收藏
    float accuracy() const { return answered > 0 ? static_cast<float>(correct) / static_cast<float>(answered) : 0.0f; }
};

/// 交卷后逐题的判分明细（成绩页用；score 是 0~1 的得分，乘以卷面分值才是分数）
struct ExamQuestionResult {
    int index = -1;
    bool answered = false;   // 交卷时作答了没有
    bool correct = false;    // 是不是全对
    float score = 0.0f;      // 0~1
};

class QuizSession {
public:
    /// 绑定题库与进度。progress 由 AppContext 持有，会话直接写进去，界面只读。
    void bind(const QuestionBank* bank, BankProgress* progress);

    bool valid() const;
    size_t questionCount() const;
    int currentIndex() const { return currentIndex_; }
    const Question* currentQuestion() const;

    // ---- 当前题（逐题界面用；内部就是下面 *At 版本的 currentIndex_ 版）----
    /// 点击某个选项：单选的题型会替换选择，多选 / 不定项会切换
    void selectOption(int optionIndex);
    void clearSelection();
    bool isSelected(int optionIndex) const;

    /// 填空题：第 blank 个空（从 0 开始）的输入
    const std::string& fillTextAt(int index, int blank) const;
    /// 界面里直接编辑填空题草稿（InputText 需要一个地址稳定的 std::string）
    std::string& fillTextAt(int index, int blank);
    /// 多项有序题：设置第 slot 个空选了哪个选项（-1 = 清空）
    void setOrderedSlot(int slot, int optionIndex);
    int orderedSlot(int slot) const;
    int orderedSlotCount() const;
    /// 当前题的输入够不够提交（选择题要选、有序题要填满、填空题要有字）
    bool hasAnswerInput() const;
    /// 这道题现在有没有「正在作答」的内容（选了选项 / 填了字 / 排了序，但还没提交）
    bool hasDraftAt(int index) const;
    /// 提交当前题（判分并写入进度）；timestamp 由外部传入（便于测试）
    void submit(const std::string& timestamp);
    /// 只看答案（不判分）
    void reveal();

    // ---- 材料题卷面用：按题目下标操作（一页里可以同时填好几道小题）----
    bool isSubmittedAt(int index) const;
    bool isCorrectAt(int index) const;
    float scoreAt(int index) const;
    const std::vector<int>& selectionAt(int index) const;
    bool isSelectedAt(int index, int optionIndex) const;

    void selectOptionAt(int index, int optionIndex);
    void setOrderedSlotAt(int index, int slot, int optionIndex);
    int orderedSlotAt(int index, int slot) const;
    bool hasAnswerInputAt(int index) const;
    void submitAt(int index, const std::string& timestamp);
    /// 整页提交：把这一页里「已经填了」的题一起交掉，返回实际交掉的题数
    int submitPage(const std::vector<int>& indexes, const std::string& timestamp);
    /// 取出「上次取走之后新提交的题」：{题目下标, 是否全对}
    /// 给累计统计用（AppContext 会在保存进度时取走）
    std::vector<std::pair<int, bool>> takeNewAnswers();
    /// 与 index 属于同一段材料的全部题目（下标升序）；不在材料里就返回空
    std::vector<int> materialGroupOf(int index) const;
    /// 跳过整段材料后的下一题 / 上一题下标；没有就返回 -1
    int nextGroupIndex(int index) const;
    int prevGroupIndex(int index) const;

    // ---- 练习顺序（存的是题库里的原始下标）----
    // 默认是「顺序」（0..N-1）。乱序、将来的权重/只刷错题，都只是换一份顺序；
    // 进度始终按原始下标存，所以换顺序、筛题目都不会串位。
    /// 顺序刷题：跳过「已经答对」的题（没做过的 + 答错的才进这一轮）
    /// @param limit 「每轮题数」：>0 时本轮只取前 limit 道（0 = 不限制，池子里全进）
    void resetOrder(int limit = 0);
    /// 只把「错题本里的题」排进这一轮（错题训练）
    /// @param ratio 「错题本」的被动移出比例（答对次数 ≥ ratio × 答错次数就不再算错题）
    void resetOrderWrongOnly(int ratio = 3);
    /// 这一轮是不是「从错题本进来的」（界面据此显示「移出错题本」按钮）
    bool isWrongOnlyRound() const { return wrongOnlyRound_; }
    /// 主动移出错题本：把这道题的答题历史重置成「0 次错误、1 次正确」
    /// （界面上只在「从错题本进来」且当前这题刚答对时才允许按）
    bool removeFromWrongBook(int index, const std::string& timestamp);
    /// 这一轮的成绩（只看本轮计划里的题；权重刷题按轮内位置算）。
    /// 注意别拿 stats() 代替它：那个是题库的历史累计，新一轮开始时会显示上一轮的数字
    SessionStats roundStats() const;

    // ---- 背题（只看答案、不做题）----
    /// 背题模式：整库按题号顺序全排进来（**已经答对的也进**），而且**不受「每轮题数」限制**。
    /// 界面进到哪道题就把哪道题的答案亮出来（见 reciteRevealAt / isReciteMode）
    void resetOrderRecite();
    /// 现在是背题模式吗（界面据此把答案直接展示出来）
    bool isReciteMode() const { return reciteMode_; }
    /// 背题：亮出第 index 题的答案 —— 草稿填上标准答案、标记「已交卷 + 已看答案」。
    /// 进度上：**以前没做对过的记成错题**（于是会进错题本），以前做对过的不动；
    /// 不计入累计统计（背题不算「提交了一题」）。同一道题重复调用只处理一次。
    /// @return true = 这次真的处理了（调用方据此决定要不要落盘）
    bool reciteRevealAt(int index, const std::string& timestamp);
    // ---- 考试模式 ----
    /// 开始一场考试：按卷面顺序排题（**不受「每轮题数」限制**）。
    /// 答题期间只记草稿：不判分、不写进度（同一道题可以反复改答案），
    /// 交卷时才由 finishExam() 统一判分。
    void resetOrderExam(const std::vector<int>& order);
    /// 现在是考试模式吗（界面据此不画判定卡，只显示「已作答 / 未作答」）
    bool isExamMode() const { return examMode_; }
    /// 这道题在卷面上作答了没有（选了 / 填了就算）
    bool isExamAnsweredAt(int index) const { return hasDraftAt(index); }
    /// 交卷：卷面里作答过的题按草稿统一判分写进进度（错题照旧进错题本、累计统计 +1），
    /// **没作答的题不动进度**（不算错题，只在成绩里按 0 分算）。返回逐题明细。
    std::vector<ExamQuestionResult> finishExam(const std::string& timestamp);
    /// 直接指定本轮顺序；limit > 0 时按「每轮题数」截断（连兜底出来的整库复习也一起截）
    void setOrder(const std::vector<int>& order, int limit = 0);
    /// 乱序刷题：全库都进这一轮（已经答对的也当复习），排队权重 新 1.00 > 错 0.95 > 对 0.90
    /// （只影响先后，差距 ≤10%）；同样的 seed 结果一样
    /// @param limit 「每轮题数」：>0 时只取洗好的前 limit 道
    void shuffleOrder(unsigned int seed, int limit = 0);
    /// 乱序用的排队权重（新 > 错 > 对，差距 ≤10%）
    float shuffleWeight(int index) const;

    // ---- 权重刷题（掌握度 × 难度适配 × 最弱标签）----
    /// 掌握度 0~1：没做过 0；答错过按答对次数回升；全对接近 1
    float masteryAt(int index) const;
    /// 单题抽中权重：掌握度越低、难度越接近当前水平、标签越弱，权重越高。
    /// 无标签 / 无难度的题仍使用 0.15 + (1-mastery) 的旧公式；结果夹在 [0.05, 4.0]。
    /// 统计按当前进度实时扫描，刚提交的对错次数会立即影响下一次抽题。
    float weightedWeight(int index) const;
    /// 开始一轮权重刷题：
    ///   1) 先抽 min(count, 题库大小) 道（同一批内不重复）；
    ///   2) 之后每**提交** appendEvery 题，就在队尾按**当时的**掌握度再补抽一道
    ///      （提交时对错次数已经更新，所以刚答错的题会更早被抽回来）。
    /// @param count       每轮题数（<=0 表示整个题库）
    /// @param appendEvery 每提交几题补一道（<=0 按默认 5）
    void beginWeightedRound(unsigned int seed, int count, int appendEvery = 5);
    /// 这一轮是不是权重刷题（界面用它区分「本轮题数会增长」）
    bool isLiveWeightedRound() const { return liveWeighted_; }
    /// 这一轮要刷的题（跳过已答对的）
    std::vector<int> practicePool() const;
    /// 这一轮一共几题 / 当前是第几题（都按本轮算，不是题库总数）
    int planSize() const { return static_cast<int>(order_.size()); }
    int planPosition() const { return cursor_ + 1; }
    /// 记下「本轮从什么时候开始」：只有在这个时间之后答的题才算这一轮做完的。
    /// 不设（空串）就退回「答过就算」——单元测试用这个默认值。
    void setRoundStart(const std::string& timestamp) { roundStartAt_ = timestamp; }
    /// 这一轮这道题做完了吗
    bool isRoundDone(int index) const;
    /// 把光标对到本轮的题（顺序刷题就是「编号最前的没做/做错的那道」）
    void jumpToRoundStart();
    /// 本轮还有下一题吗（乱序时按本轮顺序算，不能拿题号去比题库总数）
    bool hasNextInPlan() const { return cursor_ + 1 < static_cast<int>(order_.size()); }

    // ---- 标签 ----
    /// 这道题的标签：题库自带的 + 用户自己加的（去重，顺序保持）
    std::vector<std::string> tagsAt(int index) const;
    /// 用户自己加的标签
    const std::vector<std::string>& userTagsAt(int index) const;
    void addTagAt(int index, const std::string& tag);
    void removeTagAt(int index, const std::string& tag);
    std::vector<std::string> tags() const { return tagsAt(currentIndex_); }
    const std::vector<std::string>& userTags() const { return userTagsAt(currentIndex_); }
    void addTag(const std::string& tag) { addTagAt(currentIndex_, tag); }
    void removeTag(const std::string& tag) { removeTagAt(currentIndex_, tag); }
    /// 这一轮是不是做完了（计划非空，且计划里的题都做完了）
    bool planCompleted() const;
    /// 这一轮还剩几题没做
    int planRemaining() const;
    const std::vector<int>& order() const { return order_; }
    /// 这道题在练习顺序里的位置；不在这一轮里就返回 -1
    int orderPositionOf(int index) const;

    // ---- 导航 ----
    void next();
    void prev();
    void jumpTo(int index);

    /// 收藏 / 取消收藏当前题（立即写入进度）
    void toggleMark();
    /// 收藏 / 取消收藏指定题目（材料题卷面里每题一个收藏按钮）
    void toggleMarkAt(int index);

    // ---- 状态查询（UI 用）----
    bool submitted() const;
    bool revealed() const;
    bool isCorrect() const;
    float lastScore() const;
    const std::vector<int>& selection() const;
    const std::vector<int>& correctAnswer() const;
    SessionStats stats() const;
    bool allAnswered() const;
    int firstWrongIndex() const;
    bool isMarked(int index) const;

    /// 清空所有作答记录（重新开始）
    void restart();
    /// 只把错题重置为「未作答」，并跳到第一道错题（重做错题）
    void restartWrongOnly();

private:
    /// 一道题还没提交的作答
    struct Draft {
        std::vector<int> choice;             // 选项下标（有序题按空的顺序存，-1 = 没填）
        std::vector<std::string> fill;       // 填空题：每个空输入的文字
    };

    const Question* questionAt(int index) const;
    QuestionProgress* progressAt(int index);
    const QuestionProgress* progressAt(int index) const;
    Draft& draftAt(int index);
    const Draft& draftAt(int index) const;
    /// 清空作答草稿（绑定题库、重新开始时调用）。
    /// 只看内存，不碰进度：所以重新打开题库不会把上次的作答恢复出来。
    void resetDrafts();

    const QuestionBank* bank_ = nullptr;
    BankProgress* progress_ = nullptr;
    int currentIndex_ = 0;
    std::vector<Draft> drafts_;    // 与题目一一对应（只活在内存里）
    std::vector<char> revealed_;   // 每题是否已经看过答案
    std::vector<char> submitted_;  // 本次会话里每题交过没有
    std::vector<char> doneAt_;     // 权重刷题：本轮计划里「第几个位置」做完了（同一题号可能重复）
    bool allowRepeat_ = false;     // 本轮计划是不是「允许同一道题出现多次」
    bool liveWeighted_ = false;    // 本轮是权重刷题（开轮抽一批 + 提交时补题）
    int liveAppendEvery_ = 5;      // 每提交几题补一道
    int liveSubmitted_ = 0;        // 本轮已提交几题（补题计数）
    unsigned int liveRng_ = 2463534242u;   // 抽签用的 xorshift 状态（同 seed 可复现）
    bool reciteMode_ = false;      // 本轮是不是背题（界面直接把答案亮出来）
    bool examMode_ = false;        // 本轮是不是考试（答题只记草稿，交卷才判分写进度）
    bool wrongOnlyRound_ = false;  // 本轮是不是从错题本进来的
    /// 记下「本轮的第几个位置做完了」（权重刷题按位置算，不能按题号算）
    void markRoundPositionDone(int index);
    std::vector<std::pair<int, bool>> newAnswers_;   // 还没被统计取走的新提交
    std::vector<int> order_;   // 这一轮的练习顺序（原始下标）
    int cursor_ = 0;           // 当前在 order_ 里的位置
    std::string roundStartAt_; // 本轮开始的时间戳（空 = 不做区分）
    std::vector<char> planWasDone_;   // 生成本轮计划时，这道题是不是已经答过了

    /// 让 cursor_ 跟上 currentIndex_（重做错题、跳页之后调用）
    void syncCursorToCurrent();
    /// 按 weightedWeight 从 candidates 里抽一个（空表返回 -1）
    int pickWeighted(const std::vector<int>& candidates);
    /// 权重刷题：往队尾补一道（避开最近 5 道；题库太小时放开）
    void appendWeightedQuestion();
    /// 抽签用的 xorshift32
    unsigned int liveNextRandom();
};

}  // namespace quiz
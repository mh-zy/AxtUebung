#include "quiz/session/QuizSession.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "quiz/model/Scoring.h"

namespace quiz {

namespace {

/// 越界时返回的空值（避免调用方到处判空）
const std::vector<int>& emptyChoice() {
    static const std::vector<int> value;
    return value;
}

const std::string& emptyText() {
    static const std::string value;
    return value;
}

}  // namespace

void QuizSession::bind(const QuestionBank* bank, BankProgress* progress) {
    bank_ = bank;
    progress_ = progress;
    currentIndex_ = 0;
    if (progress_ != nullptr && bank_ != nullptr) {
        progress_->bankId = bank_->meta.id;
        progress_->resizeTo(static_cast<int>(bank_->questions.size()));
        currentIndex_ = progress_->position;
        if (currentIndex_ >= static_cast<int>(bank_->questions.size())) {
            currentIndex_ = bank_->empty() ? 0 : static_cast<int>(bank_->questions.size()) - 1;
        }
    }
    resetDrafts();
    resetOrder();        // 默认顺序；调用方随后可以 setOrder / shuffleOrder 换掉
    jumpToRoundStart();  // 从本轮第一题开始（不是上次停下的那道）
}

std::vector<std::string> QuizSession::tagsAt(int index) const {
    std::vector<std::string> out;
    const Question* question = questionAt(index);
    if (question != nullptr) {
        for (const auto& tag : question->tags) {
            if (!tag.empty() && std::find(out.begin(), out.end(), tag) == out.end()) {
                out.push_back(tag);
            }
        }
    }
    const QuestionProgress* item = progressAt(index);
    if (item != nullptr) {
        for (const auto& tag : item->tags) {
            if (!tag.empty() && std::find(out.begin(), out.end(), tag) == out.end()) {
                out.push_back(tag);
            }
        }
    }
    return out;
}

const std::vector<std::string>& QuizSession::userTagsAt(int index) const {
    static const std::vector<std::string> empty;
    const QuestionProgress* item = progressAt(index);
    return item != nullptr ? item->tags : empty;
}

void QuizSession::addTagAt(int index, const std::string& tag) {
    QuestionProgress* item = progressAt(index);
    if (item == nullptr) {
        return;
    }
    // 去掉首尾空白；空的、重复的都不加；题库里已有的也不算“用户标签”
    std::string value = tag;
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.erase(value.begin());
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    if (value.empty() || value.find(',') != std::string::npos) {
        return;
    }
    if (std::find(item->tags.begin(), item->tags.end(), value) != item->tags.end()) {
        return;
    }
    item->tags.push_back(value);
}

void QuizSession::removeTagAt(int index, const std::string& tag) {
    QuestionProgress* item = progressAt(index);
    if (item == nullptr) {
        return;
    }
    const auto it = std::find(item->tags.begin(), item->tags.end(), tag);
    if (it != item->tags.end()) {
        item->tags.erase(it);
    }
}

void QuizSession::jumpToRoundStart() {
    cursor_ = 0;
    if (order_.empty()) {
        return;
    }
    currentIndex_ = order_.front();
    if (progress_ != nullptr) {
        progress_->position = currentIndex_;
    }
    resetDrafts();   // 换题了，草稿按新题库重来
}

namespace {

/// 乱序抽签时的权重：错题更容易被抽到，没做过的次之（已答对的压根不进这一轮）
constexpr int kWeightWrong = 3;
constexpr int kWeightNew = 1;

}  // namespace

std::vector<int> QuizSession::practicePool() const {
    std::vector<int> pool;
    const int count = static_cast<int>(bank_ != nullptr ? bank_->questions.size() : 0);
    for (int i = 0; i < count; ++i) {
        const QuestionProgress* item = progressAt(i);
        if (item != nullptr && item->answered && item->correct) {
            continue;   // 已经答对的，这一轮不刷
        }
        pool.push_back(i);
    }
    return pool;
}

void QuizSession::resetOrder(int limit) {
    setOrder(practicePool(), limit);   // 顺序：没做过 / 答错的，按题库编号排
}

void QuizSession::resetOrderWrongOnly(int ratio) {
    std::vector<int> wrong;
    const int count = static_cast<int>(bank_ != nullptr ? bank_->questions.size() : 0);
    for (int i = 0; i < count; ++i) {
        const QuestionProgress* item = progressAt(i);
        if (item != nullptr && isWrongBookItem(*item, ratio)) {
            wrong.push_back(i);
        }
    }
    setOrder(wrong);   // 一道错题都没有时，setOrder 会兜底成「整库复习」
    wrongOnlyRound_ = true;
}

bool QuizSession::removeFromWrongBook(int index, const std::string& timestamp) {
    QuestionProgress* item = progressAt(index);
    if (item == nullptr) {
        return false;
    }
    // 「重置成 0 次错误、1 次正确」：这样它立刻就不算错题了（见 isWrongBookItem）
    item->answered = true;
    item->correct = true;
    item->score = 1.0f;
    item->rightCount = 1;
    item->wrongCount = 0;
    item->answeredAt = timestamp;
    return true;
}

SessionStats QuizSession::roundStats() const {
    SessionStats stats;
    stats.total = static_cast<int>(order_.size());
    for (int index : order_) {
        if (!isRoundDone(index)) {
            continue;   // 这一轮还没过到的题不算
        }
        ++stats.answered;
        const QuestionProgress* item = progressAt(index);
        if (item == nullptr) {
            continue;
        }
        if (item->correct) {
            ++stats.correct;
        } else if (item->partiallyCorrect()) {
            ++stats.partial;
        }
    }
    stats.wrong = stats.answered - stats.correct;
    return stats;
}

void QuizSession::resetOrderRecite() {
    const int count = static_cast<int>(bank_ != nullptr ? bank_->questions.size() : 0);
    std::vector<int> all;
    all.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        all.push_back(i);   // 整库按题号顺序，已经答对的也进
    }
    setOrder(all, 0);   // limit = 0：背题不看「每轮题数」
    reciteMode_ = true;
}

bool QuizSession::reciteRevealAt(int index, const std::string& timestamp) {
    const Question* question = questionAt(index);
    QuestionProgress* item = progressAt(index);
    if (question == nullptr || item == nullptr || isSubmittedAt(index)) {
        return false;   // 这一轮里已经亮过了
    }

    // 1) 草稿填上标准答案：界面上看起来就是「选对了 / 填对了」
    Draft& draft = draftAt(index);
    draft.choice = question->answer;
    draft.fill.clear();
    draft.fill.reserve(question->fillAnswers.size());
    for (const std::vector<std::string>& perBlank : question->fillAnswers) {
        draft.fill.push_back(perBlank.empty() ? std::string() : perBlank.front());
    }

    // 2) 标记「已交卷 + 已看答案」：选项按答案上色、解析出出来，用户不能再改
    submitted_[static_cast<size_t>(index)] = 1;
    revealed_[static_cast<size_t>(index)] = 1;

    // 3) 进度的历史结论：以前没做对过 → 记成错题（这样它会进错题本）；
    //    以前做对过的不动 —— 背题不该把「会了」的记录改坏。
    //    注意不累加 wrongCount：用户并没有真的答错一次
    if (!(item->answered && item->correct)) {
        item->answered = true;
        item->correct = false;
        item->score = 0.0f;
    }

    // 4) answeredAt 每次都要更新：本轮「做完没做完」是按它跟本轮开始时间比的
    //    （以前答过的题必须重新过一遍才算完，否则这一轮永远走不完）
    item->answeredAt = timestamp;
    return true;
}

void QuizSession::resetOrderExam(const std::vector<int>& order) {
    setOrder(order, 0);   // limit = 0：卷面有几十道题，不能被「每轮题数」截断
    examMode_ = true;
}

std::vector<ExamQuestionResult> QuizSession::finishExam(const std::string& timestamp) {
    std::vector<ExamQuestionResult> results;
    results.reserve(order_.size());
    for (int index : order_) {
        ExamQuestionResult result;
        result.index = index;
        const Question* question = questionAt(index);
        QuestionProgress* item = progressAt(index);
        if (question == nullptr || item == nullptr) {
            results.push_back(result);
            continue;
        }
        if (!hasDraftAt(index)) {
            // 空题：不改进度（用户没答，不该被记成错题），只在成绩里算 0 分
            results.push_back(result);
            continue;
        }
        const Draft& draft = draftAt(index);
        Score score;
        if (question->type == QuestionType::Fill) {
            score = scoreFill(*question, draft.fill);
        } else if (question->type == QuestionType::Ordered) {
            score = scoreOrdered(*question, draft.choice);
        } else {
            score = scoreChoice(*question, draft.choice);
        }
        item->answered = true;
        item->correct = score.full;
        item->score = score.value;
        item->answeredAt = timestamp;
        if (score.full) {
            ++item->rightCount;
        } else {
            ++item->wrongCount;
        }
        if (index < static_cast<int>(submitted_.size())) {
            submitted_[static_cast<size_t>(index)] = 1;
        }
        if (index < static_cast<int>(revealed_.size())) {
            revealed_[static_cast<size_t>(index)] = 1;
        }
        markRoundPositionDone(index);
        newAnswers_.push_back({index, item->correct});
        result.answered = true;
        result.correct = score.full;
        result.score = score.value;
        results.push_back(result);
    }
    // 交卷之后不再是「考试中」：界面恢复成普通查看（判定与解析都能看）
    examMode_ = false;
    return results;
}

float QuizSession::shuffleWeight(int index) const {
    // 乱序只影响「先后」，不影响「出现与否」：新题先出，错题次之，
    // 已经答对的当复习掉在最后，三者差距控制在 10% 以内（1.00 / 0.95 / 0.90）
    const QuestionProgress* item = progressAt(index);
    if (item == nullptr || !item->answered) {
        return 1.00f;
    }
    return item->correct ? 0.90f : 0.95f;
}

void QuizSession::shuffleOrder(unsigned int seed, int limit) {
    // 全库都进这一轮（含已经答对的，当复习），只把新题排在前面一点
    std::vector<int> pool;
    const int count = static_cast<int>(bank_ != nullptr ? bank_->questions.size() : 0);
    for (int i = 0; i < count; ++i) {
        pool.push_back(i);
    }
    std::vector<double> weights;
    weights.reserve(pool.size());
    for (int index : pool) {
        weights.push_back(static_cast<double>(shuffleWeight(index)));
    }
    std::mt19937 engine(seed);
    std::vector<int> order;
    while (!pool.empty()) {
        std::discrete_distribution<size_t> pick(weights.begin(), weights.end());
        const size_t at = pick(engine);
        order.push_back(pool[at]);
        pool.erase(pool.begin() + static_cast<long>(at));
        weights.erase(weights.begin() + static_cast<long>(at));
    }
    setOrder(order, limit);   // 「每轮题数」交给 setOrder 统一截断
}

float QuizSession::masteryAt(int index) const {
    const QuestionProgress* item = progressAt(index);
    if (item == nullptr || !item->answered) {
        return 0.0f;   // 没做过：最该练
    }
    const float rights = static_cast<float>(item->rightCount);
    if (item->correct) {
        const float value = 0.70f + 0.05f * (rights > 1.0f ? rights - 1.0f : 0.0f);
        return value > 0.95f ? 0.95f : value;
    }
    if (item->score > 0.0f && item->score < 1.0f) {
        return 0.40f + 0.30f * item->score;   // 拿到部分分：介于错与对之间
    }
    const float value = 0.15f + 0.05f * rights;   // 答错过：对过的次数越多越熟
    return value > 0.45f ? 0.45f : value;
}

float QuizSession::weightedWeight(int index) const {
    // 抽题权重 = 掌握度 × 难度适配 × 标签弱项，最后夹在 [0.05, 4.0]。
    //
    // 三个因子是相乘关系：
    //   * 掌握度：保留原来的 0.15 + (1-mastery)，不会让已经会的题彻底消失；
    //   * 难度：既看当前题自己的掌握度，也看同难度层的历史正确率。目标正确率取
    //     0.65，太简单和太难都降权，因为这两种题对当前水平的信息量最小；
    //   * 标签：一道题可能属于多个标签，取其中最弱的一个，避免被强标签掩盖。
    //
    // 统计在每次调用时现扫题库：提交后下一题就能看到新次数，不需要维护缓存失效点。
    // 几百道题的题库里这次额外扫描远小于一次抽签本身；以后题库到几万题再加缓存。
    const auto clampValue = [](float value, float low, float high) {
        return std::max(low, std::min(high, value));
    };

    float weight = 0.15f + (1.0f - masteryAt(index));
    const Question* question = questionAt(index);
    const QuestionProgress* item = progressAt(index);

    // ---- 难度因子 ----
    // 有作答记录时，用这道题自己的掌握度估下一次答对的概率；没作答时，用同难度层
    // 历史正确率。样本少于 3 次时先验为 d=1→0.9、d=5→0.3，避免一两次偶然结果
    // 就把整层难度判死。没有写难度的题不加难度因子，保证旧题库行为不变。
    if (question != nullptr && question->difficulty >= 1 && question->difficulty <= 5) {
        const int difficulty = question->difficulty;
        int diffAsked = 0;
        int diffRight = 0;
        const int count = static_cast<int>(bank_->questions.size());
        for (int j = 0; j < count; ++j) {
            const Question* other = questionAt(j);
            const QuestionProgress* otherProgress = progressAt(j);
            if (other == nullptr || otherProgress == nullptr ||
                other->difficulty != difficulty) {
                continue;
            }
            diffAsked += otherProgress->rightCount + otherProgress->wrongCount;
            diffRight += otherProgress->rightCount;
        }

        float probability = 0.0f;
        if (item != nullptr && item->answered) {
            probability = 0.25f + 0.65f * masteryAt(index);
        }
        else if (diffAsked >= 3) {
            probability = static_cast<float>(diffRight) / static_cast<float>(diffAsked);
        }
        else {
            probability = 0.90f - 0.15f * static_cast<float>(difficulty - 1);
        }
        probability = clampValue(probability, 0.0f, 1.0f);

        constexpr float kTargetProbability = 0.65f;
        const float distance =
            std::fabs(probability - kTargetProbability) / kTargetProbability;
        const float difficultyFactor =
            clampValue(0.45f + 1.15f * (1.0f - distance), 0.5f, 1.6f);
        weight *= difficultyFactor;
    }

    // ---- 标签因子 ----
    // tagsAt() 已经把「题库自带标签 + 用户标签」合并并去重。聚合时按作答次数加权，
    // 所以同一标签下的多道小题会共同影响这个标签的掌握度。
    // tagMastery 的 +1.5 / +3 是贝叶斯式收缩：新标签只有一两次记录时仍接近 0.5，
    // 不会因为偶然答错一次就把整类题长期抬高权重。
    const std::vector<std::string> tagList = tagsAt(index);
    if (!tagList.empty()) {
        float weakestTagMastery = 1.0f;
        const int count = static_cast<int>(bank_->questions.size());
        for (const std::string& tag : tagList) {
            int tagAsked = 0;
            int tagRight = 0;
            for (int j = 0; j < count; ++j) {
                const QuestionProgress* otherProgress = progressAt(j);
                if (otherProgress == nullptr) {
                    continue;
                }
                const std::vector<std::string> otherTags = tagsAt(j);
                if (std::find(otherTags.begin(), otherTags.end(), tag) == otherTags.end()) {
                    continue;
                }
                tagAsked += otherProgress->rightCount + otherProgress->wrongCount;
                tagRight += otherProgress->rightCount;
            }
            const float tagMastery =
                (static_cast<float>(tagRight) + 1.5f) / (static_cast<float>(tagAsked) + 3.0f);
            weakestTagMastery = std::min(weakestTagMastery, tagMastery);
        }
        const float tagFactor =
            clampValue(0.55f + 1.05f * (1.0f - weakestTagMastery), 0.55f, 1.6f);
        weight *= tagFactor;
    }

    return clampValue(weight, 0.05f, 4.0f);
}

void QuizSession::beginWeightedRound(unsigned int seed, int count, int appendEvery) {
    const int bankCount = static_cast<int>(bank_ != nullptr ? bank_->questions.size() : 0);
    reciteMode_ = false;
    wrongOnlyRound_ = false;
    examMode_ = false;
    // 会补到重复题，所以完成度按「轮内位置」记（doneAt_）
    allowRepeat_ = true;
    liveWeighted_ = true;
    liveAppendEvery_ = appendEvery > 0 ? appendEvery : 5;
    liveSubmitted_ = 0;
    liveRng_ = seed != 0 ? seed : 2463534242u;   // xorshift 不能从 0 起步
    order_.clear();
    planWasDone_.clear();
    doneAt_.clear();

    int initial = count > 0 ? count : bankCount;
    if (initial > bankCount) {
        initial = bankCount;
    }
    for (int i = 0; i < initial; ++i) {
        // 开局这一批不重复
        std::vector<int> pool;
        pool.reserve(static_cast<size_t>(bankCount));
        for (int k = 0; k < bankCount; ++k) {
            if (std::find(order_.begin(), order_.end(), k) == order_.end()) {
                pool.push_back(k);
            }
        }
        const int picked = pickWeighted(pool);
        if (picked < 0) {
            break;
        }
        order_.push_back(picked);
        planWasDone_.push_back(0);
        doneAt_.push_back(0);
    }
    if (order_.empty()) {
        resetOrder();   // 题库空了：退回顺序（防御）
        return;
    }
    syncCursorToCurrent();
}

unsigned int QuizSession::liveNextRandom() {
    unsigned int x = liveRng_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    liveRng_ = x;
    return x;
}

int QuizSession::pickWeighted(const std::vector<int>& candidates) {
    if (candidates.empty()) {
        return -1;
    }
    // 权重用**当前**掌握度：刚答错的题会立刻变得更重
    std::vector<double> weights;
    weights.reserve(candidates.size());
    double sum = 0.0;
    for (int index : candidates) {
        const double weight = static_cast<double>(weightedWeight(index));
        weights.push_back(weight);
        sum += weight;
    }
    if (sum <= 0.0) {
        return candidates.front();
    }
    const double point = (static_cast<double>(liveNextRandom()) / 4294967296.0) * sum;
    double accumulated = 0.0;
    for (size_t k = 0; k < candidates.size(); ++k) {
        accumulated += weights[k];
        if (point < accumulated) {
            return candidates[k];
        }
    }
    return candidates.back();
}

void QuizSession::appendWeightedQuestion() {
    const int bankCount = static_cast<int>(bank_ != nullptr ? bank_->questions.size() : 0);
    if (bankCount <= 0) {
        return;
    }
    // 避开最近 5 道（同一题别紧接着又出来）；题库比这个还小就放开
    constexpr int kRepeatGap = 5;
    const int from = static_cast<int>(order_.size()) > kRepeatGap
                         ? static_cast<int>(order_.size()) - kRepeatGap
                         : 0;
    std::vector<int> pool;
    pool.reserve(static_cast<size_t>(bankCount));
    for (int k = 0; k < bankCount; ++k) {
        bool recent = false;
        for (int p = from; p < static_cast<int>(order_.size()); ++p) {
            if (order_[static_cast<size_t>(p)] == k) {
                recent = true;
                break;
            }
        }
        if (!recent) {
            pool.push_back(k);
        }
    }
    if (pool.empty()) {
        for (int k = 0; k < bankCount; ++k) {
            pool.push_back(k);
        }
    }
    const int picked = pickWeighted(pool);
    if (picked < 0) {
        return;
    }
    order_.push_back(picked);
    planWasDone_.push_back(0);
    doneAt_.push_back(0);
}
void QuizSession::markRoundPositionDone(int index) {
    if (!allowRepeat_ || doneAt_.empty()) {
        return;
    }
    // 优先记在当前光标那个位置；否则找第一个还没做过的同名位置
    if (cursor_ >= 0 && cursor_ < static_cast<int>(order_.size()) && order_[cursor_] == index) {
        doneAt_[static_cast<size_t>(cursor_)] = 1;
        return;
    }
    for (size_t p = 0; p < order_.size(); ++p) {
        if (order_[p] == index && doneAt_[p] == 0) {
            doneAt_[p] = 1;
            return;
        }
    }
}

bool QuizSession::isRoundDone(int index) const {
    const QuestionProgress* item = progressAt(index);
    if (item == nullptr || !item->answered) {
        return false;
    }
    if (roundStartAt_.empty()) {
        return true;   // 没记本轮开始时间：答过就算
    }
    const int at = orderPositionOf(index);
    const bool wasDone = at >= 0 && at < static_cast<int>(planWasDone_.size()) &&
                         planWasDone_[static_cast<size_t>(at)] != 0;
    if (!wasDone) {
        return true;   // 本轮开始时还没做的：答了就算做完
    }
    // 本轮开始时就已经答过的（例如错题本里的错题）：得「重新答过」才算。
    // 时间戳只精确到秒，所以用严格晚于，避免同一秒内的旧作答被算进来。
    return item->answeredAt > roundStartAt_;
}

bool QuizSession::planCompleted() const {
    if (order_.empty() || progress_ == nullptr) {
        return false;
    }
    if (allowRepeat_) {
        // 权重刷题：同一道题会出现多次，所以按「轮内位置」判完成
        for (char done : doneAt_) {
            if (!done) {
                return false;
            }
        }
        return true;
    }
    for (int index : order_) {
        if (!isRoundDone(index)) {
            return false;
        }
    }
    return true;
}

int QuizSession::planRemaining() const {
    if (allowRepeat_) {
        int left = 0;
        for (char done : doneAt_) {
            if (!done) {
                ++left;
            }
        }
        return left;
    }
    int remaining = 0;
    for (int index : order_) {
        if (!isRoundDone(index)) {
            ++remaining;
        }
    }
    return remaining;
}

void QuizSession::setOrder(const std::vector<int>& order, int limit) {
    reciteMode_ = false;      // 换了计划就不再是背题（要背题请走 resetOrderRecite）
    liveWeighted_ = false;    // 也不再是权重刷题（要它请走 beginWeightedRound）
    allowRepeat_ = false;     // 计划里不再允许重复（权重刷题的 doneAt_ 随之作废）
    doneAt_.clear();
    examMode_ = false;        // 也不再是考试模式（要考试请走 resetOrderExam）
    wrongOnlyRound_ = false;  // 也不再是「从错题本进来的那一轮」
    const int count = static_cast<int>(bank_ != nullptr ? bank_->questions.size() : 0);
    order_.clear();
    order_.reserve(order.size());
    for (int index : order) {
        // 只收合法且不重复的下标；没被收进来的就是「这一轮不刷」
        if (index >= 0 && index < count &&
            std::find(order_.begin(), order_.end(), index) == order_.end()) {
            order_.push_back(index);
        }
    }
    if (order_.empty()) {
        for (int i = 0; i < count; ++i) {
            order_.push_back(i);   // 兜底：空顺序就退回顺序刷
        }
    }
    // 「每轮题数」：放在兜底之后 —— 兜底出来的整库复习也照样受限（这一轮就是 N 道）
    if (limit > 0 && static_cast<int>(order_.size()) > limit) {
        order_.resize(static_cast<size_t>(limit));
    }
    // 记下「生成本轮时哪些题已经答过」：这些题（比如错题本里的错题）
    // 必须在本轮开始之后重新答过，才算这一轮做完
    planWasDone_.assign(order_.size(), 0);
    for (size_t k = 0; k < order_.size(); ++k) {
        const QuestionProgress* item = progressAt(order_[k]);
        planWasDone_[k] = (item != nullptr && item->answered) ? 1 : 0;
    }
    syncCursorToCurrent();
}

int QuizSession::orderPositionOf(int index) const {
    const auto it = std::find(order_.begin(), order_.end(), index);
    return it == order_.end() ? -1 : static_cast<int>(it - order_.begin());
}

void QuizSession::syncCursorToCurrent() {
    if (order_.empty()) {
        cursor_ = 0;
        return;
    }
    const int at = orderPositionOf(currentIndex_);
    cursor_ = at >= 0 ? at : 0;
    currentIndex_ = order_[static_cast<size_t>(cursor_)];
    if (progress_ != nullptr) {
        progress_->position = currentIndex_;
    }
}

void QuizSession::resetDrafts() {
    newAnswers_.clear();   // 换题库/重新开始时，旧的新提交不再记
    const size_t count = bank_ != nullptr ? bank_->questions.size() : 0;
    drafts_.assign(count, Draft{});
    revealed_.assign(count, 0);
    submitted_.assign(count, 0);
}

bool QuizSession::valid() const {
    return bank_ != nullptr && progress_ != nullptr && !bank_->empty();
}

size_t QuizSession::questionCount() const {
    return bank_ != nullptr ? bank_->questions.size() : 0;
}

const Question* QuizSession::questionAt(int index) const {
    if (bank_ == nullptr || index < 0 || index >= static_cast<int>(bank_->questions.size())) {
        return nullptr;
    }
    return &bank_->questions[static_cast<size_t>(index)];
}

const Question* QuizSession::currentQuestion() const {
    return questionAt(currentIndex_);
}

QuestionProgress* QuizSession::progressAt(int index) {
    if (progress_ == nullptr || index < 0 || index >= static_cast<int>(progress_->questions.size())) {
        return nullptr;
    }
    return &progress_->questions[static_cast<size_t>(index)];
}

const QuestionProgress* QuizSession::progressAt(int index) const {
    return const_cast<QuizSession*>(this)->progressAt(index);
}

QuizSession::Draft& QuizSession::draftAt(int index) {
    if (index < 0 || index >= static_cast<int>(drafts_.size())) {
        static Draft fallback;
        fallback = Draft{};
        return fallback;
    }
    return drafts_[static_cast<size_t>(index)];
}

const QuizSession::Draft& QuizSession::draftAt(int index) const {
    return const_cast<QuizSession*>(this)->draftAt(index);
}

// ---------------------------------------------------------------------------
// 作答
// ---------------------------------------------------------------------------
void QuizSession::selectOptionAt(int index, int optionIndex) {
    const Question* question = questionAt(index);
    if (question == nullptr || isSubmittedAt(index)) {
        return;   // 已提交的题不能改（想重做请「重做错题」或重新开始）
    }
    if (question->type == QuestionType::Fill || question->type == QuestionType::Ordered) {
        return;   // 这两种题型不走「点选项」
    }
    const int optionCount =
        question->type == QuestionType::Judge ? 2 : static_cast<int>(question->options.size());
    if (optionIndex < 0 || optionIndex >= optionCount) {
        return;
    }
    std::vector<int>& choice = draftAt(index).choice;
    if (question->type == QuestionType::Multiple || question->type == QuestionType::Indeterminate) {
        const auto it = std::find(choice.begin(), choice.end(), optionIndex);
        if (it != choice.end()) {
            choice.erase(it);
        } else {
            choice.push_back(optionIndex);
            std::sort(choice.begin(), choice.end());
        }
    } else {
        choice.assign(1, optionIndex);
    }
}

void QuizSession::selectOption(int optionIndex) {
    selectOptionAt(currentIndex_, optionIndex);
}

void QuizSession::clearSelection() {
    if (isSubmittedAt(currentIndex_)) {
        return;
    }
    Draft& draft = draftAt(currentIndex_);
    draft.choice.clear();
    draft.fill.clear();
}

bool QuizSession::isSelectedAt(int index, int optionIndex) const {
    const std::vector<int>& choice = selectionAt(index);
    return std::find(choice.begin(), choice.end(), optionIndex) != choice.end();
}

bool QuizSession::isSelected(int optionIndex) const {
    return isSelectedAt(currentIndex_, optionIndex);
}



void QuizSession::setOrderedSlotAt(int index, int slot, int optionIndex) {
    const Question* question = questionAt(index);
    if (question == nullptr || isSubmittedAt(index) || slot < 0) {
        return;
    }
    const int optionCount = static_cast<int>(question->options.size());
    if (optionIndex >= optionCount) {
        return;
    }
    const int slots = question->slotCount();
    if (slot >= slots) {
        return;
    }
    std::vector<int>& choice = draftAt(index).choice;
    if (static_cast<int>(choice.size()) < slots) {
        choice.resize(static_cast<size_t>(slots), -1);
    }
    choice[static_cast<size_t>(slot)] = optionIndex;
}

void QuizSession::setOrderedSlot(int slot, int optionIndex) {
    setOrderedSlotAt(currentIndex_, slot, optionIndex);
}

int QuizSession::orderedSlotAt(int index, int slot) const {
    const std::vector<int>& choice = selectionAt(index);
    if (slot < 0 || slot >= static_cast<int>(choice.size())) {
        return -1;
    }
    return choice[static_cast<size_t>(slot)];
}

int QuizSession::orderedSlot(int slot) const {
    return orderedSlotAt(currentIndex_, slot);
}

int QuizSession::orderedSlotCount() const {
    const Question* question = currentQuestion();
    return question != nullptr ? question->slotCount() : 0;
}

bool QuizSession::hasAnswerInputAt(int index) const {
    const Question* question = questionAt(index);
    if (question == nullptr || isSubmittedAt(index)) {
        return false;
    }
    const Draft& draft = draftAt(index);
    if (question->type == QuestionType::Fill) {
        const int blanks = question->fillBlankCount();
        if (blanks <= 0 || static_cast<int>(draft.fill.size()) < blanks) {
            return false;
        }
        for (int i = 0; i < blanks; ++i) {
            if (normalizeFillText(draft.fill[static_cast<size_t>(i)]).empty()) {
                return false;   // 有空没填：还不让交
            }
        }
        return true;
    }
    if (question->type == QuestionType::Ordered) {
        const int slots = question->slotCount();
        if (slots <= 0 || static_cast<int>(draft.choice.size()) < slots) {
            return false;
        }
        for (int i = 0; i < slots; ++i) {
            if (draft.choice[static_cast<size_t>(i)] < 0) {
                return false;
            }
        }
        return true;
    }
    return !draft.choice.empty();
}

bool QuizSession::hasAnswerInput() const {
    return hasAnswerInputAt(currentIndex_);
}

void QuizSession::submitAt(int index, const std::string& timestamp) {
    if (examMode_) {
        // 考试模式：答题期间不判分、不写进度（交卷时由 finishExam() 统一判）。
        // 于是同一道题可以反复改答案，界面也不会提前泄露对错。
        return;
    }
    const Question* question = questionAt(index);
    QuestionProgress* item = progressAt(index);
    // 判「交过没有」看的是本次会话的标记，不是进度里的 answered：
    // 进度里的 answered 是历史记录，重新打开题库后这道题应该还能再答一次。
    if (question == nullptr || item == nullptr || isSubmittedAt(index) || !hasAnswerInputAt(index)) {
        return;
    }

    // 判分规则都在 Scoring.h 里（界面、命令行、测试共用同一套）；
    // 这里是唯一把判分结果写进进度的地方。
    const Draft& draft = draftAt(index);
    Score score;
    if (question->type == QuestionType::Fill) {
        score = scoreFill(*question, draft.fill);
    } else if (question->type == QuestionType::Ordered) {
        score = scoreOrdered(*question, draft.choice);
    } else {
        score = scoreChoice(*question, draft.choice);
    }

    item->answered = true;
    item->correct = score.full;
    item->score = score.value;
    item->answeredAt = timestamp;
    // 结算：把这次的对/错记进次数（作答内容本身不落盘，只留次数）
    if (score.full) {
        ++item->rightCount;
    } else {
        ++item->wrongCount;
    }
    if (index < static_cast<int>(submitted_.size())) {
        submitted_[static_cast<size_t>(index)] = 1;
    }
    if (index < static_cast<int>(revealed_.size())) {
        revealed_[static_cast<size_t>(index)] = 1;
    }
    markRoundPositionDone(index);   // 权重刷题按「轮内位置」记完成度
    newAnswers_.push_back({index, item->correct});   // 留给累计统计

    // 权重刷题：每提交 M 题，在队尾按**刚更新过的**对错次数补抽一道
    if (liveWeighted_) {
        ++liveSubmitted_;
        if (liveAppendEvery_ > 0 && liveSubmitted_ % liveAppendEvery_ == 0) {
            appendWeightedQuestion();
        }
    }
}

std::vector<std::pair<int, bool>> QuizSession::takeNewAnswers() {
    std::vector<std::pair<int, bool>> out;
    out.swap(newAnswers_);
    return out;
}

int QuizSession::submitPage(const std::vector<int>& indexes, const std::string& timestamp) {
    int submitted = 0;
    for (int index : indexes) {
        if (isSubmittedAt(index) || !hasAnswerInputAt(index)) {
            continue;   // 交过的、还没填的都不动
        }
        submitAt(index, timestamp);
        if (isSubmittedAt(index)) {
            ++submitted;
        }
    }
    return submitted;
}

bool QuizSession::hasDraftAt(int index) const {
    if (index < 0 || index >= static_cast<int>(drafts_.size())) {
        return false;
    }
    const Draft& draft = drafts_[static_cast<size_t>(index)];
    for (int choice : draft.choice) {
        if (choice >= 0) {
            return true;
        }
    }
    for (const std::string& text : draft.fill) {
        if (!text.empty()) {
            return true;
        }
    }
    return false;
}

void QuizSession::submit(const std::string& timestamp) {
    submitAt(currentIndex_, timestamp);
}

void QuizSession::reveal() {
    if (questionAt(currentIndex_) != nullptr && currentIndex_ < static_cast<int>(revealed_.size())) {
        revealed_[static_cast<size_t>(currentIndex_)] = 1;
    }
}

// ---------------------------------------------------------------------------
// 状态查询
// ---------------------------------------------------------------------------
bool QuizSession::isSubmittedAt(int index) const {
    return index >= 0 && index < static_cast<int>(submitted_.size()) &&
           submitted_[static_cast<size_t>(index)] != 0;
}

bool QuizSession::isCorrectAt(int index) const {
    const QuestionProgress* item = progressAt(index);
    return item != nullptr && item->answered && item->correct;
}

float QuizSession::scoreAt(int index) const {
    const QuestionProgress* item = progressAt(index);
    if (item == nullptr || !item->answered) {
        return 0.0f;
    }
    return item->score > 0.0f ? item->score : (item->correct ? 1.0f : 0.0f);
}

const std::vector<int>& QuizSession::selectionAt(int index) const {
    if (index < 0 || index >= static_cast<int>(drafts_.size())) {
        return emptyChoice();
    }
    return drafts_[static_cast<size_t>(index)].choice;
}

const std::string& QuizSession::fillTextAt(int index, int blank) const {
    if (index < 0 || index >= static_cast<int>(drafts_.size()) || blank < 0 ||
        blank >= static_cast<int>(drafts_[static_cast<size_t>(index)].fill.size())) {
        return emptyText();
    }
    return drafts_[static_cast<size_t>(index)].fill[static_cast<size_t>(blank)];
}

std::string& QuizSession::fillTextAt(int index, int blank) {
    Draft& draft = draftAt(index);
    if (blank < 0) {
        static std::string fallback;
        fallback.clear();
        return fallback;
    }
    if (static_cast<int>(draft.fill.size()) <= blank) {
        draft.fill.resize(static_cast<size_t>(blank) + 1);
    }
    return draft.fill[static_cast<size_t>(blank)];
}

bool QuizSession::submitted() const {
    return isSubmittedAt(currentIndex_);
}

bool QuizSession::revealed() const {
    return currentIndex_ >= 0 && currentIndex_ < static_cast<int>(revealed_.size()) &&
           revealed_[static_cast<size_t>(currentIndex_)] != 0;
}

bool QuizSession::isCorrect() const {
    return isCorrectAt(currentIndex_);
}

float QuizSession::lastScore() const {
    return scoreAt(currentIndex_);
}

const std::vector<int>& QuizSession::selection() const {
    return selectionAt(currentIndex_);
}

std::vector<int> QuizSession::materialGroupOf(int index) const {
    std::vector<int> group;
    const Question* question = questionAt(index);
    if (question == nullptr || question->materialId.empty() || bank_ == nullptr) {
        return group;
    }
    for (size_t i = 0; i < bank_->questions.size(); ++i) {
        if (bank_->questions[i].materialId == question->materialId) {
            group.push_back(static_cast<int>(i));
        }
    }
    return group;
}

int QuizSession::nextGroupIndex(int index) const {
    const Question* question = questionAt(index);
    if (question == nullptr || bank_ == nullptr) {
        return -1;
    }
    // 沿着**练习顺序**找下一题：乱序的时候「下一段材料」才不会跳错
    const int start = orderPositionOf(index);
    const int count = static_cast<int>(order_.size());
    if (question->materialId.empty()) {
        return start >= 0 && start + 1 < count ? order_[static_cast<size_t>(start + 1)] : -1;
    }
    for (int k = start >= 0 ? start + 1 : 0; k < count; ++k) {
        const int candidate = order_[static_cast<size_t>(k)];
        if (bank_->questions[static_cast<size_t>(candidate)].materialId != question->materialId) {
            return candidate;
        }
    }
    return -1;
}

int QuizSession::prevGroupIndex(int index) const {
    const Question* question = questionAt(index);
    if (question == nullptr || bank_ == nullptr) {
        return -1;
    }
    const int start = orderPositionOf(index);
    if (start <= 0) {
        return -1;
    }
    int k = start - 1;
    if (question->materialId.empty()) {
        // 当前是普通题：上一题如果在某段材料里，就退到那段材料的第一题
        const std::string& previousMaterial =
            bank_->questions[static_cast<size_t>(order_[static_cast<size_t>(k)])].materialId;
        while (k > 0 && !previousMaterial.empty() &&
               bank_->questions[static_cast<size_t>(order_[static_cast<size_t>(k - 1)])].materialId ==
                   previousMaterial) {
            --k;
        }
        return order_[static_cast<size_t>(k)];
    }
    // 当前在材料里：往回退到「不属于这段材料」的上一题
    while (k >= 0 && bank_->questions[static_cast<size_t>(order_[static_cast<size_t>(k)])].materialId ==
                         question->materialId) {
        --k;
    }
    return k >= 0 ? order_[static_cast<size_t>(k)] : -1;
}

const std::vector<int>& QuizSession::correctAnswer() const {
    const Question* question = currentQuestion();
    return question != nullptr ? question->answer : emptyChoice();
}

SessionStats QuizSession::stats() const {
    SessionStats stats;
    if (bank_ == nullptr) {
        return stats;
    }
    stats.total = static_cast<int>(bank_->questions.size());
    if (progress_ != nullptr) {
        stats.answered = progress_->answeredCount();
        stats.correct = progress_->correctCount();
        stats.wrong = progress_->wrongCount();
        for (const auto& item : progress_->questions) {
            if (item.marked) {
                ++stats.marked;
            }
            if (item.partiallyCorrect()) {
                ++stats.partial;
            }
        }
    }
    return stats;
}

bool QuizSession::allAnswered() const {
    if (!valid()) {
        return false;
    }
    for (const auto& item : progress_->questions) {
        if (!item.answered) {
            return false;
        }
    }
    return true;
}

int QuizSession::firstWrongIndex() const {
    if (progress_ == nullptr) {
        return -1;
    }
    for (size_t i = 0; i < progress_->questions.size(); ++i) {
        if (progress_->questions[i].answered && !progress_->questions[i].correct) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool QuizSession::isMarked(int index) const {
    const QuestionProgress* item = progressAt(index);
    return item != nullptr && item->marked;
}

// ---------------------------------------------------------------------------
// 导航
// ---------------------------------------------------------------------------
void QuizSession::next() {
    if (!valid()) {
        return;
    }
    if (cursor_ + 1 < static_cast<int>(order_.size())) {
        ++cursor_;
        currentIndex_ = order_[static_cast<size_t>(cursor_)];
        progress_->position = currentIndex_;
    }
}

void QuizSession::prev() {
    if (!valid()) {
        return;
    }
    if (cursor_ > 0) {
        --cursor_;
        currentIndex_ = order_[static_cast<size_t>(cursor_)];
        progress_->position = currentIndex_;
    }
}

void QuizSession::jumpTo(int index) {
    if (!valid()) {
        return;
    }
    const int at = orderPositionOf(index);
    if (at < 0) {
        return;   // 这一轮不刷这道题
    }
    cursor_ = at;
    currentIndex_ = order_[static_cast<size_t>(cursor_)];
    progress_->position = currentIndex_;
}

void QuizSession::toggleMarkAt(int index) {
    QuestionProgress* item = progressAt(index);
    if (item != nullptr) {
        item->marked = !item->marked;
    }
}

void QuizSession::toggleMark() {
    toggleMarkAt(currentIndex_);
}

// ---------------------------------------------------------------------------
// 重新开始
// ---------------------------------------------------------------------------
void QuizSession::restart() {
    if (progress_ == nullptr) {
        return;
    }
    for (auto& item : progress_->questions) {
        item = QuestionProgress{};
    }
    progress_->position = 0;
    currentIndex_ = 0;
    resetDrafts();
    syncCursorToCurrent();
}

void QuizSession::restartWrongOnly() {
    if (progress_ == nullptr) {
        return;
    }
    int firstWrong = -1;
    for (size_t i = 0; i < progress_->questions.size(); ++i) {
        QuestionProgress& item = progress_->questions[i];
        if (item.answered && !item.correct) {
            // 只把「答过 / 对错 / 分数 / 时间」清掉，让这道题重新进本轮；
            // 收藏、自己加的标签、答对答错的次数都是历史，留着。
            const bool marked = item.marked;
            const std::vector<std::string> tags = item.tags;
            const int rightCount = item.rightCount;
            const int wrongCount = item.wrongCount;
            item = QuestionProgress{};
            item.marked = marked;
            item.tags = tags;
            item.rightCount = rightCount;
            item.wrongCount = wrongCount;
            if (firstWrong < 0) {
                firstWrong = static_cast<int>(i);
            }
        }
    }
    if (firstWrong >= 0) {
        currentIndex_ = firstWrong;
        progress_->position = firstWrong;
    }
    resetDrafts();
    syncCursorToCurrent();
}

}  // namespace quiz
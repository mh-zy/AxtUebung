#include "quiz/app/AppContext.h"

#include <algorithm>

#include "quiz/platform/Platform.h"
#include "quiz/progress/ProgressStore.h"
#include "quiz/util/Log.h"

namespace quiz {

namespace {

/// "en.lang" → "en"
std::string stripSuffix(const std::string& name, const std::string& suffix) {
    if (io::endsWith(name, suffix)) {
        return name.substr(0, name.size() - suffix.size());
    }
    return name;
}

/// 从语言包文本里取出显示名（[lang] name = ...），失败时返回代码本身
std::string peekLanguageName(const std::string& text, const std::string& fallback) {
    std::vector<io::Issue> issues;
    const std::vector<io::Line> lines = io::parse(text, issues);
    std::string section;
    for (const io::Line& line : lines) {
        if (line.isSection) {
            section = line.section;
            continue;
        }
        if (section == "lang" && line.key == "name" && !io::trim(line.value).empty()) {
            return io::trim(line.value);
        }
    }
    return fallback;
}

}  // namespace

bool AppContext::initialize() {
    platform::writeLog(0, (std::string("数据目录：") + platform::dataDirectory()).c_str());

    scanLanguagePacks();
    loadSettings();
    loadStats();
    reloadPrompts();
    reloadThemePresets();

    // 默认语言：先试语言包（数据目录 → assets），失败则用内置中文文案
    std::string error;
    if (!loadLanguagePack("zh-CN", error)) {
        i18n_.useBuiltin();
        logWarn("未加载到语言包，使用内置文案：%s", error.c_str());
    }

    reloadBanks();
    if (!banks_.empty()) {
        openBank(0);
    }
    logInfo("初始化完成：题库 %d 个，语言 %s", static_cast<int>(banks_.size()),
            i18n_.language().c_str());
    return !banks_.empty();
}

void AppContext::reloadBanks() {
    const std::string previousId =
        (activeBankIndex_ >= 0 && activeBankIndex_ < static_cast<int>(banks_.size()))
            ? banks_[static_cast<size_t>(activeBankIndex_)].bank.meta.id
            : std::string();

    abandonExam();   // 题库列表要重建：正在进行的考试作废（题目可能都没了）

    banks_.clear();

    // 1) 打包进程序的题库（assets/banks）
    for (const auto& name : platform::listAssetFiles("banks", ".quiz")) {
        std::string text;
        if (!platform::readAssetText("banks/" + name, text)) {
            continue;
        }
        io::LoadResult result = io::loadBank(text, name);
        // 注意：一定要在 std::move(result.bank) 之前算好 ok，
        // 因为 LoadResult::ok() 需要检查 bank.questions 是否为空，
        // 而 move 之后 result 里的题目已经被搬走了（这个坑就是“开始练习按钮一直灰”的根因）。
        const bool bankOk = result.ok();
        BankEntry entry;
        entry.sourceName = name;
        entry.fromDataDirectory = false;
        entry.bank = std::move(result.bank);
        entry.issues = std::move(result.issues);
        entry.ok = bankOk;
        loadProgressFor(entry);
        banks_.push_back(std::move(entry));
    }

    // 2) 用户放在数据目录里的题库（data/banks）：同名文件覆盖 assets 版本
    for (const auto& name : platform::listDataFiles("banks", ".quiz")) {
        std::string text;
        const std::string path = platform::joinPath(platform::joinPath(platform::dataDirectory(), "banks"), name);
        if (!platform::readTextFile(path, text)) {
            continue;
        }
        io::LoadResult result = io::loadBank(text, name);
        const bool bankOk = result.ok();   // 同上：先取结果，再搬数据
        BankEntry entry;
        entry.sourceName = name;
        entry.fromDataDirectory = true;
        entry.bank = std::move(result.bank);
        entry.issues = std::move(result.issues);
        entry.ok = bankOk;
        loadProgressFor(entry);

        const auto existing = std::find_if(
            banks_.begin(), banks_.end(),
            [&](const BankEntry& item) { return item.sourceName == entry.sourceName; });
        if (existing != banks_.end()) {
            *existing = std::move(entry);   // 用户目录优先
        } else {
            banks_.push_back(std::move(entry));
        }
    }

    // 3) 稳定排序：按标题
    std::sort(banks_.begin(), banks_.end(), [](const BankEntry& a, const BankEntry& b) {
        return a.bank.meta.title < b.bank.meta.title;
    });

    // 4) 尽量恢复原来打开的题库
    activeBankIndex_ = banks_.empty() ? -1 : 0;
    if (!previousId.empty()) {
        for (size_t i = 0; i < banks_.size(); ++i) {
            if (banks_[i].bank.meta.id == previousId) {
                activeBankIndex_ = static_cast<int>(i);
                break;
            }
        }
    }

    // 5) 重新绑定会话：banks_ 刚刚重建过，会话里指向旧元素的指针已经失效。
    //    （以前这里是悬着的：导出 / 删除题库之后继续答题等于读已释放的内存。）
    if (activeBankIndex_ >= 0) {
        BankEntry& entry = banks_[static_cast<size_t>(activeBankIndex_)];
        session_.bind(&entry.bank, &entry.progress);
        session_.jumpToRoundStart();
    }
}

bool AppContext::deleteBank(int index, std::string& outError) {
    if (index < 0 || index >= static_cast<int>(banks_.size())) {
        outError = i18n_.text("ui.home.delete_failed");
        return false;
    }
    const BankEntry& entry = banks_[static_cast<size_t>(index)];
    const std::string title = entry.bank.meta.title;
    if (!entry.fromDataDirectory) {
        // 打包进程序的题库删了下次启动还在，干脆不给删
        outError = i18n_.format("ui.home.delete_builtin", {{"name", title}});
        return false;
    }
    const std::string file = platform::joinPath(
        platform::joinPath(platform::dataDirectory(), "banks"), entry.sourceName);
    const std::string bankId = entry.bank.meta.id;
    if (!platform::removeFile(file)) {
        outError = i18n_.format("ui.home.delete_failed", {{"name", title}});
        return false;
    }
    platform::removeFile(progressFilePath(bankId));   // 答题数据一起删
    logInfo("已删除题库文件：%s", file.c_str());
    reloadBanks();   // 列表重建，会话在这里面重新绑定
    return true;
}

std::string AppContext::progressFilePath(const std::string& bankId) const {
    return platform::joinPath(platform::joinPath(platform::dataDirectory(), "progress"), bankId + ".progress");
}

bool AppContext::loadProgressFor(BankEntry& entry) {
    const int questionCount = static_cast<int>(entry.bank.questions.size());
    entry.progress.bankId = entry.bank.meta.id;
    entry.progress.resizeTo(questionCount);

    std::string text;
    if (platform::readTextFile(progressFilePath(entry.bank.meta.id), text)) {
        entry.progress = progress::deserialize(text, entry.bank.meta.id, questionCount);
        logInfo("已读取进度：%s（已答 %d 题）", entry.bank.meta.id.c_str(), entry.progress.answeredCount());
    }
    return true;
}

bool AppContext::openBank(int index, OrderSource source) {
    if (index < 0 || index >= static_cast<int>(banks_.size())) {
        return false;
    }
    abandonExam();   // 换题库/重新进这个题库：上一场考试作废
    activeBankIndex_ = index;
    BankEntry& entry = banks_[static_cast<size_t>(index)];
    session_.bind(&entry.bank, &entry.progress);
    // 用哪套顺序看「从哪儿进来」：首页入口用首页选的刷题模式，
    // 统计页的明细用设置里的刷题顺序（两处互不干扰）
    const std::string& order =
        (source == OrderSource::Home) ? settings_.homeOrder : settings_.practiceOrder;
    // 决定这一轮练哪些题、按什么顺序（bind 里已经默认成「顺序 + 跳过已答对」）
    // 「每轮题数」：顺序 / 乱序 / 权重三种都用它（考试模式还没做，错题训练是整本错题）
    const int roundSize = settings_.roundSize;
    const unsigned int seed = static_cast<unsigned int>(platform::monotonicSeconds() * 1000.0);
    if (order == "shuffle") {
        session_.shuffleOrder(seed, roundSize);
    } else if (order == "wrong") {
        session_.resetOrderWrongOnly(settings_.wrongBookRatio);
    } else if (order == "weighted") {
        // 权重刷题：先抽 min(每轮题数, 题库大小) 道，之后每提交 N 题在队尾补一道
        // （补题用的是刚更新过的对错次数）
        session_.beginWeightedRound(seed, roundSize, settings_.weightedAppendEvery);
    } else if (order == "recite") {
        // 背题：整库按顺序全排进来，不受「每轮题数」限制（界面会直接把答案亮出来）
        session_.resetOrderRecite();
    } else {
        session_.resetOrder(roundSize);   // 顺序：本轮取前 N 道
    }
    // 换过顺序（乱序/错题）之后，光标要重新对到本轮第一题，
    // 否则会沿用「顺序刷题」那份计划里的起始题号。
    session_.jumpToRoundStart();
    session_.setRoundStart(platform::nowString());   // 这一轮从现在算起
    restartTimer();
    logInfo("打开题库：%s（%d 题）", entry.bank.meta.title.c_str(),
            static_cast<int>(entry.bank.questions.size()));
    return true;
}

const std::vector<ExamSpec>& AppContext::activeBankExams() const {
    static const std::vector<ExamSpec> kEmpty;
    if (activeBankIndex_ < 0 || activeBankIndex_ >= static_cast<int>(banks_.size())) {
        return kEmpty;
    }
    return banks_[static_cast<size_t>(activeBankIndex_)].bank.exams;
}

void AppContext::abandonExam() {
    examActive_ = false;
    examStartedSeconds_ = 0.0;
    examDeadlineSeconds_ = 0.0;
    examPaper_ = io::ExamPaper{};
    examSummary_ = ExamSummary{};
    hasExamSummary_ = false;
}

bool AppContext::startExam(int examIndex, std::string& outError) {
    if (activeBankIndex_ < 0 || activeBankIndex_ >= static_cast<int>(banks_.size())) {
        outError = i18n_.text("ui.exam.no_bank");
        return false;
    }
    BankEntry& entry = banks_[static_cast<size_t>(activeBankIndex_)];
    if (examIndex < 0 || examIndex >= static_cast<int>(entry.bank.exams.size())) {
        outError = i18n_.text("ui.exam.no_paper");
        return false;
    }
    const ExamSpec& spec = entry.bank.exams[static_cast<size_t>(examIndex)];
    // 卷里写了 seed → 每次抽到同一套题（模拟卷要可复现）；没写 → 每次重抽
    const int seed = spec.seed != 0 ? spec.seed
                                    : static_cast<int>(platform::monotonicSeconds() * 1000.0);
    examPaper_ = io::buildExamPaper(entry.bank, spec, seed);

    session_.bind(&entry.bank, &entry.progress);
    std::vector<int> order;
    order.reserve(examPaper_.items.size());
    for (const io::ExamPaperItem& item : examPaper_.items) {
        order.push_back(item.questionIndex);
    }
    session_.resetOrderExam(order);
    session_.jumpToRoundStart();
    session_.setRoundStart(platform::nowString());
    restartTimer();

    examActive_ = true;
    hasExamSummary_ = false;
    examSummary_ = ExamSummary{};
    examStartedSeconds_ = platform::monotonicSeconds();
    examDeadlineSeconds_ =
        spec.minutes > 0 ? examStartedSeconds_ + static_cast<double>(spec.minutes) * 60.0 : 0.0;
    logInfo("开始考试：%s（%d 题 / 满分 %d，%d 条抽题规则）", examPaper_.title.c_str(),
            static_cast<int>(examPaper_.items.size()), examPaper_.totalScore(),
            static_cast<int>(spec.rules.size()));
    for (const io::Issue& issue : examPaper_.issues) {
        logWarn("抽题提示（第 %d 行）：%s", issue.line, issue.message.c_str());
    }
    return true;
}

double AppContext::examElapsedSeconds() const {
    if (examStartedSeconds_ <= 0.0) {
        return 0.0;
    }
    const double now = platform::monotonicSeconds();
    return now > examStartedSeconds_ ? now - examStartedSeconds_ : 0.0;
}

int AppContext::examRemainingSeconds() const {
    if (!examActive_ || examDeadlineSeconds_ <= 0.0) {
        return -1;   // 不限时
    }
    const double left = examDeadlineSeconds_ - platform::monotonicSeconds();
    if (left <= 0.0) {
        return 0;
    }
    return static_cast<int>(left + 0.999);   // 向上取整：一开始显示满格
}

const ExamSummary& AppContext::finishExam() {
    if (hasExamSummary_) {
        return examSummary_;   // 已经交过卷了：别重复判分（错题本会多记一次）
    }
    ExamSummary summary;
    summary.examId = examPaper_.examId;
    summary.title = examPaper_.title;
    summary.totalScore = examPaper_.totalScore();
    summary.questionCount = static_cast<int>(examPaper_.items.size());
    summary.usedSeconds = examElapsedSeconds();
    summary.hasPass = examPaper_.passPercent >= 0;

    const std::vector<ExamQuestionResult> results = session_.finishExam(platform::nowString());
    for (const io::ExamPaperItem& item : examPaper_.items) {
        ExamLine line;
        line.index = item.questionIndex;
        line.position = static_cast<int>(summary.lines.size()) + 1;
        line.score = item.score;
        for (const ExamQuestionResult& result : results) {
            if (result.index != item.questionIndex) {
                continue;
            }
            line.answered = result.answered;
            line.correct = result.correct;
            line.got = static_cast<float>(item.score) * result.score;
            break;
        }
        if (line.answered) {
            ++summary.answeredCount;
        }
        if (line.correct) {
            ++summary.correctCount;
        }
        summary.earnedScore += line.got;
        summary.lines.push_back(line);
    }
    // 及格线是百分比：得分 / 满分 ≥ 及格线就是及格（例如 60 分线：得分 ≥ 满分的 60%）
    summary.passed = summary.hasPass && summary.totalScore > 0 &&
                     summary.earnedScore * 100.0f >=
                         static_cast<float>(examPaper_.passPercent) *
                             static_cast<float>(summary.totalScore);

    examActive_ = false;
    examDeadlineSeconds_ = 0.0;
    examSummary_ = summary;
    hasExamSummary_ = true;
    saveProgress();   // 错题进本 + 累计统计 + 进度落盘
    logInfo("交卷：%s 得分 %.1f / %d（答对 %d / %d）", summary.title.c_str(),
            static_cast<double>(summary.earnedScore), summary.totalScore, summary.correctCount,
            summary.questionCount);
    return examSummary_;
}

void AppContext::saveProgress() {
    // 先汇总累计统计：这部分和「当前打开了哪个题库」无关
    flushStats();

    if (activeBankIndex_ >= 0 && activeBankIndex_ < static_cast<int>(banks_.size())) {
        BankEntry& entry = banks_[static_cast<size_t>(activeBankIndex_)];
        entry.progress.updatedAt = platform::nowString();
        const std::string path = progressFilePath(entry.bank.meta.id);
        if (!platform::writeTextFile(path, progress::serialize(entry.progress))) {
            logError("保存进度失败：%s", path.c_str());
        }

        // 这个题库刚好做完（每题都答了且全对）？记一笔「完成过」
        const int total = static_cast<int>(entry.bank.questions.size());
        bool complete = total > 0 && static_cast<int>(entry.progress.questions.size()) >= total;
        for (int i = 0; complete && i < total; ++i) {
            const QuestionProgress& item = entry.progress.questions[static_cast<size_t>(i)];
            if (!item.answered || !item.correct) {
                complete = false;
            }
        }
        if (complete) {
            stats_.markBankCompleted(entry.bank.meta.id);
        }
    }
    saveStats();
}

void AppContext::accruePracticeTime(double now) {
    if (!quizPageActive_) {
        quizPageTickSeconds_ = now;
        return;
    }
    double delta = now - quizPageTickSeconds_;
    quizPageTickSeconds_ = now;
    if (delta <= 0.0) {
        return;
    }
    // 睡着 / 切后台留下的空档：一次最多算 5 分钟（正常每帧只有十几毫秒）
    if (delta > 300.0) {
        delta = 300.0;
    }
    roundPracticeSeconds_ += delta;
    pendingPracticeSeconds_ += delta;
}

void AppContext::setQuizPageActive(bool active) {
    const double now = platform::monotonicSeconds();
    accruePracticeTime(now);   // 先把刚才那一段（如果之前在刷题页）结算掉
    quizPageActive_ = active;
    quizPageTickSeconds_ = now;
}

void AppContext::flushStats() {
    // 做题时长：只累计刷题页面开着的那一段（首页、设置、统计页都不算）
    accruePracticeTime(platform::monotonicSeconds());
    double elapsed = pendingPracticeSeconds_;
    pendingPracticeSeconds_ = 0.0;
    if (elapsed > 1800.0) {
        elapsed = 1800.0;   // 一次最多记半小时，防止系统时间跳变
    }
    stats_.addPracticeSeconds(elapsed);

    // 统计不依赖当前题库：刚提交的题在这里记账（题库清空也不回退）
    const std::vector<std::pair<int, bool>> fresh = session_.takeNewAnswers();
    logInfo("统计汇总：新提交 %d 题，时长 +%.1f 秒", static_cast<int>(fresh.size()), elapsed);
    for (const auto& answer : fresh) {
        stats_.recordAnswer(answer.second, platform::nowString());
    }
    if (stats_.firstUsedAt.empty()) {
        stats_.firstUsedAt = platform::nowString();
    }
}

std::string AppContext::statsFilePath() const {
    return platform::joinPath(platform::dataDirectory(), "stats.txt");
}

void AppContext::loadStats() {
    std::string text;
    if (platform::readTextFile(statsFilePath(), text)) {
        stats_ = Stats::parse(text);
        logInfo("已读取累计统计：共 %d 题，用时 %.0f 秒，连续 %d 天",
                stats_.totalSubmitted, stats_.totalSeconds, stats_.currentStreakDays);
    }
}

void AppContext::resetStats() {
    stats_ = Stats{};
    stats_.firstUsedAt = platform::nowString();
    saveStats();
    logInfo("累计统计已重置");
}

bool AppContext::saveStats() {
    const std::string path = statsFilePath();
    if (!platform::writeTextFile(path, stats_.serialize())) {
        logError("保存统计失败：%s", path.c_str());
        return false;
    }
    return true;
}

void AppContext::resetActiveProgress() {
    session_.restart();
    saveProgress();
}

int AppContext::wrongCountOf(int index) const {
    if (index < 0 || index >= static_cast<int>(banks_.size())) {
        return 0;
    }
    const BankEntry& entry = banks_[static_cast<size_t>(index)];
    int wrong = 0;
    for (const QuestionProgress& item : entry.progress.questions) {
        if (isWrongBookItem(item, settings_.wrongBookRatio)) {
            ++wrong;
        }
    }
    return wrong;
}

void AppContext::clearBankProgress(int index) {
    if (index < 0 || index >= static_cast<int>(banks_.size())) {
        return;
    }
    BankEntry& entry = banks_[static_cast<size_t>(index)];
    for (auto& item : entry.progress.questions) {
        item = QuestionProgress{};
    }
    entry.progress.position = 0;
    entry.progress.updatedAt = platform::nowString();
    // 清的可能不是当前题库，所以直接写它的进度文件，不走 saveProgress()
    const std::string path = progressFilePath(entry.bank.meta.id);
    if (!platform::writeTextFile(path, progress::serialize(entry.progress))) {
        logError("清除题库数据失败：%s", path.c_str());
    }
    if (index == activeBankIndex_) {
        session_.bind(&entry.bank, &entry.progress);   // 会话重新开始，本轮计划也重建
    }
    logInfo("已清除题库数据：%s", entry.bank.meta.id.c_str());
}

double AppContext::elapsedSeconds() const {
    double total = roundPracticeSeconds_;
    if (quizPageActive_) {
        total += platform::monotonicSeconds() - quizPageTickSeconds_;   // 正在刷题：把进行中的这一段也算上
    }
    return total > 0.0 ? total : 0.0;
}

void AppContext::restartTimer() {
    flushStats();   // 先把上一段时长记进累计，再重新开始计时
    roundPracticeSeconds_ = 0.0;
    quizPageTickSeconds_ = platform::monotonicSeconds();
}

QuestionBank AppContext::buildExportBank(ExportKind kind) const {
    QuestionBank toWrite;
    if (activeBankIndex_ < 0 || activeBankIndex_ >= static_cast<int>(banks_.size())) {
        return toWrite;
    }
    const BankEntry& entry = banks_[static_cast<size_t>(activeBankIndex_)];
    switch (kind) {
        case ExportKind::WholeBank:
            toWrite = entry.bank;
            break;
        case ExportKind::WrongOnly:
            toWrite = io::buildWrongOnlyBank(entry.bank, entry.progress, settings_.wrongBookRatio);
            break;
        case ExportKind::MarkedOnly:
            toWrite = io::buildMarkedOnlyBank(entry.bank, entry.progress);
            break;
    }
    return toWrite;
}

bool AppContext::exportActive(ExportKind kind, std::string& outPath, std::string& outError) {
    if (activeBankIndex_ < 0 || activeBankIndex_ >= static_cast<int>(banks_.size())) {
        outError = i18n_.text("ui.export.no_bank");
        return false;
    }
    QuestionBank toWrite = buildExportBank(kind);
    return writeExported(toWrite, outPath, outError);
}

std::string AppContext::exportDirectory() const {
    return platform::joinPath(platform::dataDirectory(), "export");
}

std::string AppContext::suggestedExportName(ExportKind kind) const {
    const QuestionBank toWrite = buildExportBank(kind);
    const std::string baseId = toWrite.meta.id.empty() ? std::string("bank") : toWrite.meta.id;
    return baseId + "-" + platform::nowStampCompact() + ".quiz";
}

std::string AppContext::suggestedTaggedName() const {
    std::string baseId = "bank";
    if (activeBankIndex_ >= 0 && activeBankIndex_ < static_cast<int>(banks_.size())) {
        baseId = banks_[static_cast<size_t>(activeBankIndex_)].bank.meta.id + "-tags";
    }
    return baseId + "-" + platform::nowStampCompact() + ".quiz";
}

bool AppContext::exportActiveTo(ExportKind kind, const std::string& path, std::string& outError) {
    const QuestionBank toWrite = buildExportBank(kind);
    if (toWrite.questions.empty()) {
        outError = i18n_.text("ui.export.empty");
        return false;
    }
    return writeBankFile(toWrite, path, outError);
}

bool AppContext::exportTaggedTo(const std::vector<std::string>& tags, bool bakeUserTags,
                                const std::string& path, std::string& outError) {
    if (activeBankIndex_ < 0 || activeBankIndex_ >= static_cast<int>(banks_.size())) {
        outError = i18n_.text("ui.export.no_bank");
        return false;
    }
    if (tags.empty()) {
        outError = i18n_.text("ui.export.tags_pick_empty");
        return false;
    }
    const BankEntry& entry = banks_[static_cast<size_t>(activeBankIndex_)];
    const QuestionBank toWrite = io::buildTaggedBank(entry.bank, entry.progress, tags, bakeUserTags);
    if (toWrite.questions.empty()) {
        outError = i18n_.text("ui.export.empty");
        return false;
    }
    return writeBankFile(toWrite, path, outError);
}

bool AppContext::writeBankFile(const QuestionBank& toWrite, const std::string& path,
                               std::string& outError) const {
    if (!platform::writeTextFile(path, io::saveBank(toWrite))) {
        outError = i18n_.text("ui.export.failed_write");
        return false;
    }
    logInfo("已导出题库：%s（%d 题）", path.c_str(), static_cast<int>(toWrite.questions.size()));
    return true;
}

bool AppContext::importBankFiles(const std::vector<std::string>& paths,
                                 std::vector<std::string>& outNames, std::string& outError) {
    outNames.clear();
    const std::string banksDir = platform::joinPath(platform::dataDirectory(), "banks");
    int failed = 0;
    for (const std::string& path : paths) {
        std::string text;
        if (!platform::readTextFile(path, text)) {
            ++failed;
            logWarn("导入失败（读不到文件）：%s", path.c_str());
            continue;
        }
        const std::string fileName = io::normalizeBankFileName(path);
        // 先解析一遍：坏文件不进题库目录，免得启动后到处是报错卡片
        io::LoadResult parsed = io::loadBank(text, fileName);
        if (parsed.hasFatalError || !parsed.ok()) {
            ++failed;
            logWarn("导入失败（不是有效的题库）：%s", path.c_str());
            continue;
        }
        const std::string target = platform::joinPath(banksDir, fileName);
        if (!platform::writeTextFile(target, text)) {
            ++failed;
            logWarn("导入失败（写不进题库目录）：%s", target.c_str());
            continue;
        }
        const std::string title = parsed.bank.meta.title.empty() ? fileName : parsed.bank.meta.title;
        outNames.push_back(title);
        logInfo("已导入题库：%s -> %s", path.c_str(), target.c_str());
    }
    if (!outNames.empty()) {
        reloadBanks();
    }
    if (outNames.empty()) {
        outError = failed > 0 ? i18n_.text("ui.home.import_invalid")
                              : i18n_.text("ui.home.import_none");
        return false;
    }
    return true;
}

std::vector<AppContext::TagInfo> AppContext::activeBankTags() const {
    std::vector<TagInfo> out;
    if (activeBankIndex_ < 0 || activeBankIndex_ >= static_cast<int>(banks_.size())) {
        return out;
    }
    const BankEntry& entry = banks_[static_cast<size_t>(activeBankIndex_)];
    for (size_t i = 0; i < entry.bank.questions.size(); ++i) {
        std::vector<std::string> names = entry.bank.questions[i].tags;
        if (i < entry.progress.questions.size()) {
            for (const std::string& tag : entry.progress.questions[i].tags) {
                names.push_back(tag);
            }
        }
        for (const std::string& tag : names) {
            auto it = std::find_if(out.begin(), out.end(),
                                   [&tag](const TagInfo& info) { return info.name == tag; });
            if (it == out.end()) {
                out.push_back(TagInfo{tag, 1});
            } else {
                ++it->count;
            }
        }
    }
    std::sort(out.begin(), out.end(),
              [](const TagInfo& a, const TagInfo& b) { return a.name < b.name; });
    return out;
}

bool AppContext::exportTagged(const std::vector<std::string>& tags, bool bakeUserTags,
                              std::string& outPath, std::string& outError) {
    if (activeBankIndex_ < 0 || activeBankIndex_ >= static_cast<int>(banks_.size())) {
        outError = "没有打开的题库";
        return false;
    }
    if (tags.empty()) {
        outError = i18n_.text("ui.export.tags_pick_empty");
        return false;
    }
    const BankEntry& entry = banks_[static_cast<size_t>(activeBankIndex_)];
    QuestionBank toWrite = io::buildTaggedBank(entry.bank, entry.progress, tags, bakeUserTags);
    return writeExported(toWrite, outPath, outError);
}

bool AppContext::writeExported(QuestionBank& toWrite, std::string& outPath, std::string& outError) {
    if (toWrite.questions.empty()) {
        outError = i18n_.text("ui.export.empty");
        return false;
    }
    const std::string directory = platform::joinPath(platform::dataDirectory(), "export");
    const std::string fileName = toWrite.meta.id + "-" + platform::nowStampCompact() + ".quiz";
    outPath = platform::joinPath(directory, fileName);
    if (!platform::writeTextFile(outPath, io::saveBank(toWrite))) {
        outError = "写入文件失败";
        return false;
    }
    logInfo("已导出题库：%s（%d 题）", outPath.c_str(), static_cast<int>(toWrite.questions.size()));
    // 导出的题库同时放进数据目录的 banks/，下次启动就能直接选用
    const std::string banksDir =
        platform::joinPath(platform::joinPath(platform::dataDirectory(), "banks"), fileName);
    platform::writeTextFile(banksDir, io::saveBank(toWrite));
    return true;
}

std::vector<std::string> AppContext::availableLanguages() const {
    std::vector<std::string> codes;
    codes.reserve(languages_.size());
    for (const auto& item : languages_) {
        codes.push_back(item.first);
    }
    return codes;
}

std::string AppContext::languageName(const std::string& code) const {
    for (const auto& item : languages_) {
        if (item.first == code) {
            return item.second;
        }
    }
    return code;
}

void AppContext::scanLanguagePacks() {
    languages_.clear();
    for (const auto& code : i18n::I18n::builtinLanguageCandidates()) {
        languages_.emplace_back(code, code);
    }

    const auto add = [&](const std::string& fileName, bool fromData) {
        const std::string code = stripSuffix(fileName, ".lang");
        if (code.empty()) {
            return;
        }
        std::string text;
        const bool loaded = fromData
                                ? platform::readTextFile(platform::joinPath(
                                      platform::joinPath(platform::dataDirectory(), "lang"), fileName), text)
                                : platform::readAssetText("lang/" + fileName, text);
        if (!loaded) {
            return;
        }
        const std::string displayName = peekLanguageName(text, code);
        for (auto& item : languages_) {
            if (item.first == code) {
                item.second = displayName;
                return;
            }
        }
        languages_.emplace_back(code, displayName);
    };

    for (const auto& name : platform::listAssetFiles("lang", ".lang")) {
        add(name, false);
    }
    for (const auto& name : platform::listDataFiles("lang", ".lang")) {
        add(name, true);
    }
}

void AppContext::reloadThemePresets() {
    themePresets_.clear();

    // 数据目录优先（用户可以在 data/presets/themes.txt 里覆盖或追加）
    std::string text;
    const std::string dataPath =
        platform::joinPath(platform::joinPath(platform::dataDirectory(), "presets"), "themes.txt");
    if (platform::readTextFile(dataPath, text) ||
        platform::readAssetText("presets/themes.txt", text)) {
        themePresets_ = parseThemePresets(text);
        logInfo("已加载主题预设：%d 套", static_cast<int>(themePresets_.size()));
    }
}

void AppContext::reloadPrompts() {
    prompts_.clear();

    const auto add = [&](const std::string& fileName, bool fromData) {
        std::string text;
        const bool loaded =
            fromData ? platform::readTextFile(
                           platform::joinPath(platform::joinPath(platform::dataDirectory(), "prompt"), fileName),
                           text)
                     : platform::readAssetText("prompt/" + fileName, text);
        if (!loaded || text.empty()) {
            return;
        }
        PromptEntry entry;
        entry.name = stripSuffix(fileName, ".txt");
        entry.text = text;
        entry.fromDataDirectory = fromData;
        // 数据目录优先：同名覆盖 assets 里的那一份
        for (auto& item : prompts_) {
            if (item.name == entry.name) {
                item = entry;
                return;
            }
        }
        prompts_.push_back(entry);
    };

    for (const auto& name : platform::listAssetFiles("prompt", ".txt")) {
        add(name, false);
    }
    for (const auto& name : platform::listDataFiles("prompt", ".txt")) {
        add(name, true);
    }

    std::sort(prompts_.begin(), prompts_.end(),
              [](const PromptEntry& a, const PromptEntry& b) { return a.name < b.name; });
    logInfo("已加载提示词：%d 个", static_cast<int>(prompts_.size()));
}

bool AppContext::loadLanguagePack(const std::string& code, std::string& outError) {
    std::string text;
    // 1) 用户数据目录里的语言包（可以自己新增语言，不改程序）
    const std::string dataPath =
        platform::joinPath(platform::joinPath(platform::dataDirectory(), "lang"), code + ".lang");
    if (platform::readTextFile(dataPath, text)) {
        if (i18n_.loadPackText(text, "data/lang/" + code + ".lang", &outError)) {
            return true;
        }
    }
    // 2) 打包在 assets 里的语言包
    if (platform::readAssetText("lang/" + code + ".lang", text)) {
        if (i18n_.loadPackText(text, "assets/lang/" + code + ".lang", &outError)) {
            return true;
        }
    }
    // 3) 内置默认（简体中文）
    if (code == "zh-CN") {
        i18n_.useBuiltin();
        return true;
    }
    outError = "找不到语言包：" + code;
    return false;
}

bool AppContext::switchLanguage(const std::string& code, std::string& outError) {
    return loadLanguagePack(code, outError);
}

// ---------------------------------------------------------------------------
// 设置：存在数据目录的 settings.txt，纯文本，可手工改
// ---------------------------------------------------------------------------
std::string AppContext::settingsFilePath() const {
    return platform::joinPath(platform::dataDirectory(), "settings.txt");
}

void AppContext::loadSettings() {
    std::string text;
    if (platform::readTextFile(settingsFilePath(), text)) {
        settings_ = Settings::parse(text);
        logInfo("已读取设置：答对自动跳转=%d，单选即提交=%d",
                settings_.autoNextOnCorrect ? 1 : 0, settings_.autoSubmitSingle ? 1 : 0);
    }
}

bool AppContext::saveSettings() {
    const std::string path = settingsFilePath();
    if (!platform::writeTextFile(path, settings_.serialize())) {
        logError("保存设置失败：%s", path.c_str());
        return false;
    }
    return true;
}

}  // namespace quiz

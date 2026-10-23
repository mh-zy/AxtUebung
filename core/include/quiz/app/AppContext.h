#pragma once

#include <map>
#include <string>
#include <vector>

#include "quiz/app/Settings.h"
#include "quiz/app/Stats.h"
#include "quiz/app/ThemePreset.h"
#include "quiz/i18n/I18n.h"
#include "quiz/io/BankFormat.h"
#include "quiz/io/ExamBuilder.h"
#include "quiz/model/Progress.h"
#include "quiz/model/QuestionBank.h"
#include "quiz/session/QuizSession.h"

// ============================================================================
// 应用上下文：把「题库、进度、会话、语言包、导出」这些零件拼成一个整体
// ----------------------------------------------------------------------------
// UI 只和 AppContext 打交道，不直接碰文件与平台接口，这样：
//   * UI 代码很好读（只有界面逻辑）
//   * 换存储方式（例如以后改成数据库）只改这里
//   * 单元测试可以直接构造 AppContext 做端到端逻辑验证
// ============================================================================
namespace quiz {

/// 「提示词」设置页里的一项
struct PromptEntry {
    std::string name;    // 文件名（去掉 .txt），界面显示用
    std::string text;    // 提示词正文
    bool fromDataDirectory = false;
};

/// 题库列表里的一个条目（首页展示用）
struct BankEntry {
    std::string sourceName;          // 文件名，例如 demo-basic.quiz
    bool fromDataDirectory = false;  // true=来自用户数据目录（优先级更高）
    QuestionBank bank;
    std::vector<io::Issue> issues;
    BankProgress progress;
    bool ok = false;                 // 题库解析是否可用

    int answeredCount() const { return progress.answeredCount(); }
    int correctCount() const { return progress.correctCount(); }
};

/// 成绩单里的一道题
struct ExamLine {
    int index = -1;      // 题库里的原始题号
    int position = 0;    // 卷面第几题（1 开始）
    int score = 1;       // 这道题的分值
    float got = 0.0f;    // 这道题拿到的分（分值 × 0~1 得分）
    bool answered = false;
    bool correct = false;
};

/// 一份试卷的成绩单（交卷后生成）
struct ExamSummary {
    std::string examId;
    std::string title;
    int totalScore = 0;        // 满分
    float earnedScore = 0.0f;  // 得分（部分分按比例）
    int questionCount = 0;
    int answeredCount = 0;
    int correctCount = 0;
    bool hasPass = false;      // 卷里写了及格线吗
    bool passed = false;
    double usedSeconds = 0.0;  // 从开考到交卷
    std::vector<ExamLine> lines;

    /// 得分率（0~1）
    float percent() const {
        return totalScore > 0 ? earnedScore / static_cast<float>(totalScore) : 0.0f;
    }
    /// 按题数算的正确率（0~1）
    float accuracy() const {
        return questionCount > 0 ? static_cast<float>(correctCount) / static_cast<float>(questionCount)
                                 : 0.0f;
    }
};
class AppContext {
public:
    /// 启动时调用：扫描题库、加载语言包与进度
    bool initialize();

    // ---- 题库 ----
    const std::vector<BankEntry>& banks() const { return banks_; }
    int activeBank() const { return activeBankIndex_; }
    /// 从哪儿进的题库 —— 决定这一轮用哪套顺序设置
    enum class OrderSource {
        Home,   // 首页（开始练习 / 题库卡片）→ 用首页选的「刷题模式」
        Stats,  // 数据统计页的明细 → 用设置 → 答题里的「刷题顺序」
    };
    bool openBank(int index, OrderSource source = OrderSource::Home);   // 读取该题库的进度并绑定会话
    void reloadBanks();                    // 重新扫描（导出/新增题库后可用）

    // ---- 会话与进度 ----
    QuizSession& session() { return session_; }
    const QuizSession& session() const { return session_; }
    void saveProgress();                   // 把当前进度写回磁盘
    void resetActiveProgress();            // 清空当前题库进度
    /// 清空指定题库的答题数据（收藏也一起清；**不动**累计统计）
    void clearBankProgress(int index);
    /// 删除题库：删掉 data/banks 里的那个 .quiz 文件 + 它的进度文件（累计统计不动）。
    /// 打包进程序的题库删不掉，这时返回 false 并在 outError 里说明。
    bool deleteBank(int index, std::string& outError);
    /// 这个题库有几道错题（答过但没答对）
    int wrongCountOf(int index) const;
    /// 本轮**做题时长**（只算刷题页面开着的时间；首页 / 设置 / 统计页都不算）
    double elapsedSeconds() const;
    /// 本轮重新计时（换题库、重做错题时）
    void restartTimer();
    /// 界面每帧告诉上下文「现在是不是在刷题页」（考试页、材料卷面也算）
    void setQuizPageActive(bool active);

    // ---- 导出 ----
    enum class ExportKind { WholeBank, WrongOnly, MarkedOnly };
    /// 导出成功返回 true，并把路径写到 outPath
    bool exportActive(ExportKind kind, std::string& outPath, std::string& outError);

    /// 当前题库里出现过的标签（题库自带的 tags + 用户自己加的），按名字排序、带题目数
    struct TagInfo {
        std::string name;
        int count = 0;
    };
    std::vector<TagInfo> activeBankTags() const;

    /// 按标签导出：选中的标签是「或」的关系（可多选）。
    /// bakeUserTags = 把用户自己加的标签也写进导出的题目（固化），
    /// 这样导出的文件自带标签、换台设备也认。
    bool exportTagged(const std::vector<std::string>& tags, bool bakeUserTags, std::string& outPath,
                      std::string& outError);

    /// 默认导出目录（data/export）
    std::string exportDirectory() const;
    /// 导出的默认文件名（给「保存」对话框做默认值，例如 demo-basic-wrong-20261014-120000.quiz）
    std::string suggestedExportName(ExportKind kind) const;
    /// 按标签导出的默认文件名
    std::string suggestedTaggedName() const;
    /// 导出到**指定路径**（用户在系统「保存」对话框里挑的位置）
    bool exportActiveTo(ExportKind kind, const std::string& path, std::string& outError);
    bool exportTaggedTo(const std::vector<std::string>& tags, bool bakeUserTags,
                        const std::string& path, std::string& outError);

    /// 导入题库文件：拷进 data/banks（.txt/.text 等后缀会统一改成 .quiz），
    /// 导入前先解析一遍，坏文件不会进题库目录。
    /// 返回 true 表示至少成功一个；outNames 是导入进来的题库标题。
    bool importBankFiles(const std::vector<std::string>& paths, std::vector<std::string>& outNames,
                         std::string& outError);

    // ---- 首页背景 ----
    /// 导入 PNG/JPEG/BMP 到 data/background/，成功后更新设置。
    bool importHomeBackground(const std::string& sourcePath, std::string& outError);
    /// 读取当前背景图片的二进制内容（内置 assets/background 或 data/background）。
    bool readHomeBackground(std::string& outBytes, std::string& outError) const;
    /// 清除背景设置并删除用户导入的背景文件。
    bool clearHomeBackground(std::string& outError);

    // ---- 考试模式 ----
    /// 当前题库里定义的卷（首页「考试」用；题库没写 [exam] 就是空的）
    const std::vector<ExamSpec>& activeBankExams() const;
    /// 绑定当前题库并开始一场考试：按 [draw] 规则抽题、进考试会话（答题只记草稿）
    bool startExam(int examIndex, std::string& outError);
    /// 现在是不是考试中（界面据此显示倒计时与交卷）
    bool examActive() const { return examActive_; }
    /// 正在做的这套卷（题号 + 分值 + 时长 + 及格线）
    const io::ExamPaper& examPaper() const { return examPaper_; }
    /// 交卷：统一判分、写进度、记累计统计，并留下成绩单（重复调用返回同一份）
    const ExamSummary& finishExam();
    /// 有没有交过卷（界面据此把「成绩单」入口点亮）
    bool hasExamSummary() const { return hasExamSummary_; }
    const ExamSummary& examSummary() const { return examSummary_; }
    /// 放弃这场考试：清掉考试状态（不写任何进度）
    void abandonExam();
    /// 这场考试还剩几秒（向上取整）；不限时返回 -1
    int examRemainingSeconds() const;
    /// 这场考试已经用了多少秒
    double examElapsedSeconds() const;

    // ---- 语言包 ----
    i18n::I18n& i18n() { return i18n_; }
    const i18n::I18n& i18n() const { return i18n_; }
    std::vector<std::string> availableLanguages() const;   // 内置候选 + 扫描到的语言包
    std::string languageName(const std::string& code) const;  // 语言的显示名（取自语言包）
    bool switchLanguage(const std::string& code, std::string& outError);

    /// 读取内置使用手册 Markdown；找不到当前语言时回退简体中文。
    std::string manualPage(const std::string& page) const;

    // ---- 主题预设（设置页「主题配色」用）----
    /// 来自 assets/presets/themes.txt（数据目录同名文件优先）
    const std::vector<ThemePreset>& themePresets() const { return themePresets_; }
    void reloadThemePresets();

    // ---- 提示词（设置页「提示词」用）----
    /// 打包在 assets/prompt 与用户放在 data/prompt 的提示词（同名时数据目录优先）
    const std::vector<PromptEntry>& prompts() const { return prompts_; }
    void reloadPrompts();

    // ---- 界面提示（导出成功、语言切换等一次性消息）----
    void setMessage(const std::string& message) { message_ = message; }
    const std::string& message() const { return message_; }
    void clearMessage() { message_.clear(); }

    // ---- 设置（落到数据目录的 settings.txt，重启后仍然生效）----
    Settings& settings() { return settings_; }
    const Settings& settings() const { return settings_; }

    // ---- 累计统计（跨题库；题库删了/进度清了也不回退）----
    const Stats& stats() const { return stats_; }
    /// 把累计统计清零（统计页的「重置统计」按钮用）
    void resetStats();
    /// 把当前设置写回磁盘；设置页改完开关后立刻调用
    bool saveSettings();

private:
    void loadSettings();
    void loadStats();
    /// 把「这次练习的时长」和「刚提交的题」记进累计统计
    void flushStats();
    /// 结算到 now 为止的刷题页时间（只记页面开着的那一段）
    void accruePracticeTime(double now);
    /// 把统计写回磁盘
    bool saveStats();
    /// 按 kind 拼出要导出的题库（没有打开的题库时返回空题库）
    QuestionBank buildExportBank(ExportKind kind) const;
    /// 把一个题库写到指定路径（不建目录以外的副作用）
    bool writeBankFile(const QuestionBank& toWrite, const std::string& path,
                       std::string& outError) const;
    /// 默认导出（写进 data/export/，并在 data/banks/ 也放一份）
    bool writeExported(QuestionBank& toWrite, std::string& outPath, std::string& outError);
    std::string settingsFilePath() const;
    std::string statsFilePath() const;
    std::string progressFilePath(const std::string& bankId) const;
    bool loadProgressFor(BankEntry& entry);
    bool loadLanguagePack(const std::string& code, std::string& outError);
    void scanLanguagePacks();

    std::vector<BankEntry> banks_;
    std::vector<PromptEntry> prompts_;
    std::vector<ThemePreset> themePresets_;
    int activeBankIndex_ = -1;
    QuizSession session_;
    i18n::I18n i18n_;
    std::vector<std::pair<std::string, std::string>> languages_;  // (语言代码, 显示名)
    std::string message_;
    Settings settings_;
    Stats stats_;
    mutable std::string homeBackgroundCache_;
    mutable std::string homeBackgroundCacheKey_;
    mutable std::map<std::string, std::string> manualCache_;
    // 做题时长：只算刷题页面开着的时间（以前是「进题库之后的全部时间」，把首页/设置也算进去了）
    bool quizPageActive_ = false;         // 现在是不是在刷题页
    double quizPageTickSeconds_ = 0.0;    // 上次结算的时刻
    double roundPracticeSeconds_ = 0.0;   // 本轮在刷题页上的累计（结算页「用时」用它）
    double pendingPracticeSeconds_ = 0.0; // 还没写进累计统计的那部分
    // 考试状态：examPaper_ 是正在做的那套卷（题号 + 分值），examSummary_ 是交卷后的成绩单
    bool examActive_ = false;
    bool hasExamSummary_ = false;
    io::ExamPaper examPaper_;
    ExamSummary examSummary_;
    double examStartedSeconds_ = 0.0;   // 开考的时刻（monotonic 秒）
    double examDeadlineSeconds_ = 0.0;  // 交卷截止时刻；0 = 不限时
};

}  // namespace quiz

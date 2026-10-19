#pragma once

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

// ============================================================================
// 累计统计（跟数据目录走：data/stats.txt）
// ----------------------------------------------------------------------------
// 和「进度」的区别：
//   * 进度是每个题库一份，可以清空；题库删了进度也就没了；
//   * 这里是**跨题库的累计账本**：一共刷过多少题、用了多久、连续刷了几天…
//     题库被删、进度被清，这里都不回退 —— 这正是它存在的意义。
// ============================================================================
namespace quiz {

struct Stats {
    std::string firstUsedAt;      // 第一次使用的时间
    std::string lastAnsweredAt;   // 最近一次提交的时间
    int totalSubmitted = 0;       // 累计提交过多少题（重复作答也算）
    int totalCorrect = 0;         // 其中全对的
    double totalSeconds = 0.0;    // 累计使用时长（秒）
    int currentStreakDays = 0;    // 当前连续答题天数
    int bestStreakDays = 0;       // 最长连续答题天数
    std::string lastAnsweredDay;  // "YYYY-MM-DD"
    std::map<std::string, int> dailySubmitted;  // 每天提交了多少题（只留最近一段）
    std::set<std::string> completedBanks;       // 完成过（错题清空）的题库 id

    /// 记一次提交：累计数、连续天数、当天计数
    void recordAnswer(bool correct, const std::string& timestamp);
    /// 记一段练习时长
    void addPracticeSeconds(double seconds);
    /// 某个题库刚刚完成（错题本空了）
    void markBankCompleted(const std::string& bankId);

    /// 累计正确率（没有记录时是 0）
    float accuracy() const;
    /// 最近 days 天每天提交了多少（从最早到今天，没有记录的天是 0）
    std::vector<std::pair<std::string, int>> recentDays(int days, const std::string& today) const;

    std::string serialize() const;
    static Stats parse(const std::string& text);
};

/// 从 "YYYY-MM-DD HH:MM:SS" 里取出日期部分（不合法就返回空串）
std::string dayOf(const std::string& timestamp);
/// "YYYY-MM-DD" → 第几天（算连续天数和「最近 7 天」用）
int dayNumber(const std::string& day);
/// 第几天 → "YYYY-MM-DD"（画最近 7 天的柱子用）
std::string dayText(int number);

}  // namespace quiz

#include "quiz/app/Stats.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "quiz/io/TextDocument.h"

namespace quiz {

namespace {

/// 只保留最近这么多天的每日计数，文件不至于越写越长
constexpr int kKeepDays = 21;

int toInt(const std::string& text, int fallback) {
    const std::string value = io::trim(text);
    if (value.empty()) {
        return fallback;
    }
    return std::atoi(value.c_str());
}

double toDouble(const std::string& text, double fallback) {
    const std::string value = io::trim(text);
    if (value.empty()) {
        return fallback;
    }
    return std::atof(value.c_str());
}

}  // namespace

std::string dayOf(const std::string& timestamp) {
    const std::string value = io::trim(timestamp);
    if (value.size() < 10 || value[4] != '-' || value[7] != '-') {
        return std::string();
    }
    for (size_t i : {0u, 1u, 2u, 3u, 5u, 6u, 8u, 9u}) {
        if (value[i] < '0' || value[i] > '9') {
            return std::string();
        }
    }
    return value.substr(0, 10);
}

int dayNumber(const std::string& day) {
    if (day.size() < 10) {
        return 0;
    }
    const int y = std::atoi(day.substr(0, 4).c_str());
    const int m = std::atoi(day.substr(5, 2).c_str());
    const int d = std::atoi(day.substr(8, 2).c_str());
    if (y <= 0 || m <= 0 || m > 12 || d <= 0 || d > 31) {
        return 0;
    }
    // Howard Hinnant 的 days_from_civil：把日期折算成「第几天」，跨月跨年都好算
    const int yy = m <= 2 ? y - 1 : y;
    const int era = (yy >= 0 ? yy : yy - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(yy - era * 400);
    const unsigned doy = (153u * static_cast<unsigned>(m > 2 ? m - 3 : m + 9) + 2u) / 5u +
                         static_cast<unsigned>(d) - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + static_cast<int>(doe) - 719468;
}

std::string dayText(int number) {
    // days_from_civil 的逆运算
    int z = number + 719468;
    const int era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    const int y = static_cast<int>(yoe) + era * 400;
    const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const unsigned mp = (5u * doy + 2u) / 153u;
    const unsigned d = doy - (153u * mp + 2u) / 5u + 1u;
    const unsigned m = mp < 10u ? mp + 3u : mp - 9u;
    const int year = y + (m <= 2u ? 1 : 0);
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02u-%02u", year, m, d);
    return std::string(buffer);
}

void Stats::recordAnswer(bool correct, const std::string& timestamp) {
    ++totalSubmitted;
    if (correct) {
        ++totalCorrect;
    }
    if (!timestamp.empty()) {
        lastAnsweredAt = timestamp;
    }
    const std::string day = dayOf(timestamp);
    if (day.empty()) {
        return;
    }
    if (lastAnsweredDay != day) {
        const int today = dayNumber(day);
        const int previous = dayNumber(lastAnsweredDay);
        if (!lastAnsweredDay.empty() && today - previous == 1) {
            ++currentStreakDays;      // 昨天也刷过 → 连上了
        } else {
            currentStreakDays = 1;    // 断档（或第一次）→ 从今天重新开始
        }
        lastAnsweredDay = day;
        bestStreakDays = std::max(bestStreakDays, currentStreakDays);
    }
    ++dailySubmitted[day];

    // 清掉太老的每天记录
    const int cutoff = dayNumber(day) - kKeepDays;
    for (auto it = dailySubmitted.begin(); it != dailySubmitted.end();) {
        if (dayNumber(it->first) < cutoff) {
            it = dailySubmitted.erase(it);
        } else {
            ++it;
        }
    }
}

void Stats::addPracticeSeconds(double seconds) {
    if (seconds > 0.0) {
        totalSeconds += seconds;
    }
}

void Stats::markBankCompleted(const std::string& bankId) {
    if (!bankId.empty()) {
        completedBanks.insert(bankId);
    }
}

float Stats::accuracy() const {
    return totalSubmitted > 0 ? static_cast<float>(totalCorrect) / static_cast<float>(totalSubmitted)
                             : 0.0f;
}

std::vector<std::pair<std::string, int>> Stats::recentDays(int days, const std::string& today) const {
    std::vector<std::pair<std::string, int>> out;
    if (days <= 0) {
        return out;
    }
    const int end = dayNumber(today);
    if (end <= 0) {
        return out;
    }
    for (int offset = days - 1; offset >= 0; --offset) {
        const std::string day = dayText(end - offset);
        const auto it = dailySubmitted.find(day);
        out.push_back({day, it != dailySubmitted.end() ? it->second : 0});
    }
    return out;
}

std::string Stats::serialize() const {
    std::string out;
    out += "# 刷题累计统计（纯文本，可手工编辑）\n";
    out += "# 说明：题库删了、进度清了，这里的累计数都不会回退\n\n";
    out += "[stats]\n";
    out += "first_used = " + firstUsedAt + "\n";
    out += "last_answered = " + lastAnsweredAt + "\n";
    out += "total_submitted = " + std::to_string(totalSubmitted) + "\n";
    out += "total_correct = " + std::to_string(totalCorrect) + "\n";
    out += "streak_days = " + std::to_string(currentStreakDays) + "\n";
    out += "best_streak_days = " + std::to_string(bestStreakDays) + "\n";
    out += "last_day = " + lastAnsweredDay + "\n";
    char seconds[32];
    std::snprintf(seconds, sizeof(seconds), "%.0f", totalSeconds);
    out += std::string("total_seconds = ") + seconds + "\n";

    out += "\n[day]\n";
    for (const auto& entry : dailySubmitted) {
        out += entry.first + " = " + std::to_string(entry.second) + "\n";
    }

    out += "\n[completed]\n";
    for (const auto& id : completedBanks) {
        out += "bank = " + id + "\n";
    }
    return out;
}

Stats Stats::parse(const std::string& text) {
    Stats result;
    std::vector<io::Issue> issues;
    const std::vector<io::Line> lines = io::parse(text, issues);

    std::string section;
    for (const io::Line& line : lines) {
        if (line.isSection) {
            section = line.section;
            continue;
        }
        if (section == "stats") {
            if (line.key == "first_used") {
                result.firstUsedAt = io::trim(line.value);
            } else if (line.key == "last_answered") {
                result.lastAnsweredAt = io::trim(line.value);
            } else if (line.key == "total_submitted") {
                result.totalSubmitted = toInt(line.value, 0);
            } else if (line.key == "total_correct") {
                result.totalCorrect = toInt(line.value, 0);
            } else if (line.key == "streak_days") {
                result.currentStreakDays = toInt(line.value, 0);
            } else if (line.key == "best_streak_days") {
                result.bestStreakDays = toInt(line.value, 0);
            } else if (line.key == "last_day") {
                result.lastAnsweredDay = io::trim(line.value);
            } else if (line.key == "total_seconds") {
                result.totalSeconds = toDouble(line.value, 0.0);
            }
        } else if (section == "day") {
            const std::string day = io::trim(line.key);
            const int count = toInt(line.value, 0);
            if (!day.empty()) {
                result.dailySubmitted[day] = count;
            }
        } else if (section == "completed") {
            if (line.key == "bank") {
                const std::string id = io::trim(line.value);
                if (!id.empty()) {
                    result.completedBanks.insert(id);
                }
            }
        }
    }
    result.totalCorrect = std::min(result.totalCorrect, result.totalSubmitted);
    return result;
}

}  // namespace quiz

// ============================================================================
// 核心逻辑单元测试（不依赖平台、不开窗口）
// 运行：build\win\bin\quiz_tests.exe   或   ctest --test-dir build/win
// ============================================================================
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "quiz/app/Settings.h"
#include "quiz/app/Stats.h"
#include "quiz/app/ThemePreset.h"
#include "quiz/i18n/I18n.h"
#include "quiz/io/BankFormat.h"
#include "quiz/io/DocumentText.h"
#include "quiz/io/ExamBuilder.h"
#include "quiz/model/Scoring.h"
#include "quiz/progress/ProgressStore.h"
#include "quiz/render/SvgIcon.h"
#include "quiz/session/QuizSession.h"
#include "quiz/text/MathText.h"

namespace {

int g_checks = 0;
int g_failures = 0;
/// 测试程序自己所在的目录（从 argv[0] 推出来）：用来找 exe 旁的 assets/
/// （构建脚本会把 assets 复制到 exe 旁边）
std::string g_exeDirectory = ".";

/// 取路径里的目录部分。这里不碰 std::filesystem：Windows 下 path::string() 走 ANSI 代码页，
/// 而 std::fopen 要的正好是 ANSI 路径，直接对 argv[0] 做字符串处理最省事
std::string directoryOf(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

void report(bool ok, const char* expression, const char* file, int line) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  [失败] %s:%d  %s\n", file, line, expression);
    }
}

#define CHECK(cond) report((cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ(a, b) report((a) == (b), #a " == " #b, __FILE__, __LINE__)

const char* kSampleBank =
    "[meta]\n"
    "id = test-bank\n"
    "title = 测试题库\n"
    "lang = zh-CN\n"
    "desc = 单元测试用\n"
    "\n"
    "[q]\n"
    "id = q1\n"
    "type = single\n"
    "stem = 1+1=?\n"
    "option = 2\n"
    "option = 3\n"
    "answer = A\n"
    "analysis = 因为 1+1=2\n"
    "tags = 数学,基础\n"
    "difficulty = 1\n"
    "\n"
    "[q]\n"
    "type = multiple\n"
    "stem = 选偶数\n"
    "option = 1\n"
    "option = 2\n"
    "option = 4\n"
    "answer = B,C\n"
    "\n"
    "[q]\n"
    "type = judge\n"
    "stem = 天空是蓝色的\n"
    "answer = 对\n"
    "stem.en = The sky is blue\n";

void testParseBank() {
    std::printf("解析题库\n");
    quiz::io::LoadResult result = quiz::io::loadBank(kSampleBank, "sample.quiz");
    CHECK(result.ok());
    CHECK_EQ(result.bank.meta.id, std::string("test-bank"));
    CHECK_EQ(result.bank.meta.title, std::string("测试题库"));
    CHECK_EQ(result.bank.questions.size(), size_t(3));

    const quiz::Question& first = result.bank.questions[0];
    CHECK_EQ(first.type, quiz::QuestionType::Single);
    CHECK_EQ(first.options.size(), size_t(2));
    CHECK_EQ(first.answer.size(), size_t(1));
    CHECK_EQ(first.answer[0], 0);
    CHECK_EQ(first.tags.size(), size_t(2));
    CHECK_EQ(first.difficulty, 1);

    const quiz::Question& second = result.bank.questions[1];
    CHECK_EQ(second.type, quiz::QuestionType::Multiple);
    CHECK_EQ(second.answer.size(), size_t(2));
    CHECK_EQ(second.answer[1], 2);
    CHECK_EQ(second.id, std::string("q2"));   // 没写 id 时自动生成

    const quiz::Question& third = result.bank.questions[2];
    CHECK_EQ(third.type, quiz::QuestionType::Judge);
    CHECK_EQ(third.answer[0], 0);             // 「对」= 正确
    CHECK_EQ(third.stemFor("en"), std::string("The sky is blue"));
    CHECK_EQ(third.stemFor("fr"), third.stem);  // 没有翻译时回退原文
}

void testAnswerVariants() {
    std::printf("答案写法兼容\n");
    const std::string text =
        "[meta]\nid = t\ntitle = t\n"
        "[q]\ntype = single\nstem = a\noption = x\noption = y\nanswer = 2\n"
        "[q]\ntype = judge\nstem = b\nanswer = F\n";
    quiz::io::LoadResult result = quiz::io::loadBank(text, "t.quiz");
    CHECK(result.ok());
    CHECK_EQ(result.bank.questions[0].answer[0], 1);   // 「2」表示第二个选项
    CHECK_EQ(result.bank.questions[1].answer[0], 1);   // F = 错误
}

void testTypeDiagnostics() {
    std::printf("题型提示\n");

    // 填空题：答案是文字，不再需要 option
    const std::string text =
        "[meta]\nid = t\ntitle = t\nlang = zh-CN\n"
        "[q]\ntype = 填空题\nstem = 水的化学式是____\nanswer = H2O|水\n";
    const quiz::io::LoadResult result = quiz::io::loadBank(text, "fill.quiz");
    CHECK(result.ok());
    CHECK_EQ(result.bank.questions[0].type, quiz::QuestionType::Fill);

    // 材料题写了 type 却没给材料：给可操作的提示，而且不挡住整份题库
    const quiz::io::LoadResult material = quiz::io::loadBank(
        "[meta]\nid = t2\ntitle = t2\n"
        "[q]\ntype = 材料题\nstem = 阅读材料回答问题\noption = a\noption = b\nanswer = A\n",
        "material-hint.quiz");
    int hint = 0;
    int unknown = 0;
    for (const auto& issue : material.issues) {
        if (issue.message.find("[material]") != std::string::npos) {
            ++hint;
        }
        if (issue.message.find("未知题型") != std::string::npos) {
            ++unknown;
        }
    }
    CHECK_EQ(hint, 1);
    CHECK_EQ(unknown, 0);
    CHECK(material.ok());

    // 真正打错字的仍然按「未知题型」报
    const quiz::io::LoadResult typo = quiz::io::loadBank(
        "[meta]\nid = t2\ntitle = t2\n[q]\ntype = singel\nstem = s\noption = a\noption = b\nanswer = A\n",
        "typo.quiz");
    int typoUnknown = 0;
    for (const auto& issue : typo.issues) {
        if (issue.message.find("未知题型") != std::string::npos) {
            ++typoUnknown;
        }
    }
    CHECK_EQ(typoUnknown, 1);
}

void testInvalidBank() {
    std::printf("错误题库\n");
    const std::string text =
        "[meta]\nid = bad\ntitle = bad\n"
        "[q]\ntype = single\nstem = 缺答案\noption = a\noption = b\nunknown_key = 1\n";
    quiz::io::LoadResult result = quiz::io::loadBank(text, "bad.quiz");
    CHECK(result.hasFatalError);
    CHECK(!result.issues.empty());
}

void testRoundTrip() {
    std::printf("导出与再次解析\n");
    quiz::io::LoadResult original = quiz::io::loadBank(kSampleBank, "sample.quiz");
    const std::string exported = quiz::io::saveBank(original.bank);
    quiz::io::LoadResult reloaded = quiz::io::loadBank(exported, "exported.quiz");
    CHECK(reloaded.ok());
    CHECK_EQ(reloaded.bank.questions.size(), original.bank.questions.size());
    for (size_t i = 0; i < original.bank.questions.size(); ++i) {
        CHECK_EQ(reloaded.bank.questions[i].stem, original.bank.questions[i].stem);
        CHECK_EQ(reloaded.bank.questions[i].answer, original.bank.questions[i].answer);
        CHECK_EQ(reloaded.bank.questions[i].type, original.bank.questions[i].type);
    }
    // 多语言内容也要能往返
    CHECK_EQ(reloaded.bank.questions[2].stemFor("en"), std::string("The sky is blue"));
}

void testSession() {
    std::printf("答题会话\n");
    quiz::io::LoadResult result = quiz::io::loadBank(kSampleBank, "sample.quiz");
    quiz::QuestionBank bank = result.bank;
    quiz::BankProgress progress;
    quiz::QuizSession session;
    session.bind(&bank, &progress);

    CHECK(session.valid());
    CHECK_EQ(session.questionCount(), size_t(3));
    CHECK(!session.submitted());

    // 第一题：选对
    session.selectOption(0);
    CHECK(session.isSelected(0));
    session.submit("2026-10-02 10:00:00");
    CHECK(session.submitted());
    CHECK(session.isCorrect());

    // 第二题：多选，故意选错
    session.next();
    CHECK_EQ(session.currentIndex(), 1);
    session.selectOption(0);
    session.selectOption(1);
    session.submit("2026-10-02 10:01:00");
    CHECK(!session.isCorrect());

    quiz::SessionStats stats = session.stats();
    CHECK_EQ(stats.total, 3);
    CHECK_EQ(stats.answered, 2);
    CHECK_EQ(stats.correct, 1);
    CHECK_EQ(stats.wrong, 1);
    CHECK_EQ(session.firstWrongIndex(), 1);

    // 收藏第三题
    session.jumpTo(2);
    session.toggleMark();
    CHECK(session.isMarked(2));
    CHECK_EQ(session.stats().marked, 1);

    // 重做错题：只把错题清空
    session.restartWrongOnly();
    CHECK_EQ(session.stats().answered, 1);
    CHECK_EQ(session.stats().wrong, 0);
    CHECK_EQ(session.currentIndex(), 1);

    // 全部重来
    session.restart();
    CHECK_EQ(session.stats().answered, 0);
    CHECK_EQ(session.stats().marked, 0);
}

void testProgressRoundTrip() {
    std::printf("进度序列化\n");
    quiz::BankProgress progress;
    progress.bankId = "test-bank";
    progress.position = 2;
    progress.questions.resize(3);
    progress.questions[0].answered = true;
    progress.questions[0].correct = true;
    progress.questions[0].rightCount = 2;
    progress.questions[0].answeredAt = "2026-10-02 10:00:00";
    progress.questions[1].answered = true;
    progress.questions[1].correct = false;
    progress.questions[1].wrongCount = 1;
    progress.questions[2].marked = true;

    const std::string text = quiz::progress::serialize(progress);
    quiz::BankProgress restored = quiz::progress::deserialize(text, "test-bank", 3);

    CHECK_EQ(restored.bankId, std::string("test-bank"));
    CHECK_EQ(restored.position, 2);
    CHECK_EQ(restored.answeredCount(), 2);
    CHECK_EQ(restored.correctCount(), 1);
    CHECK_EQ(restored.wrongCount(), 1);
    CHECK(restored.questions[2].marked);
    CHECK_EQ(restored.questions[0].rightCount, 2);
    CHECK_EQ(restored.questions[1].wrongCount, 1);
    // 作答内容不入库：文件里不该出现「选了什么 / 填了什么」
    CHECK(text.find("chosen") == std::string::npos);
    CHECK(text.find("\ntext = ") == std::string::npos);
}

void testNoAnswerReplay() {
    std::printf("作答不回放（重进题库是干净的一道题）\n");
    quiz::io::LoadResult result = quiz::io::loadBank(kSampleBank, "sample.quiz");
    quiz::QuestionBank bank = result.bank;
    quiz::BankProgress progress;
    quiz::QuizSession session;
    session.bind(&bank, &progress);

    // 第一题答案 A，故意选 B 交上去
    session.selectOption(1);
    session.submit("2026-10-02 10:00:00");
    CHECK(!session.isCorrect());
    CHECK_EQ(progress.questions[0].wrongCount, 1);
    CHECK_EQ(progress.questions[0].rightCount, 0);
    CHECK(session.submitted());

    // 落盘再读回、用新会话打开：历史结论还在，但界面状态必须是「没答过」
    const std::string text = quiz::progress::serialize(progress);
    quiz::BankProgress reloaded = quiz::progress::deserialize(text, "sample", 3);
    quiz::QuizSession again;
    again.bind(&bank, &reloaded);
    CHECK(again.valid());
    CHECK(!again.submitted());          // 之前答错的状态不回放
    CHECK(!again.isSelected(1));        // 选项也不回放
    CHECK(!again.isSubmittedAt(0));
    CHECK_EQ(reloaded.questions[0].wrongCount, 1);   // 次数记着
    CHECK(reloaded.questions[0].answered);

    // 再答一次（这次答对）：次数累加，历史结论跟着更新
    again.selectOption(0);
    again.submit("2026-10-02 10:05:00");
    CHECK(again.isCorrect());
    CHECK_EQ(reloaded.questions[0].rightCount, 1);
    CHECK_EQ(reloaded.questions[0].wrongCount, 1);
    CHECK_EQ(again.stats().correct, 1);
}

void appendU16Le(std::string& out, uint16_t value) {
    out.push_back(static_cast<char>(value & 0xFF));
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
}

void appendU32Le(std::string& out, uint32_t value) {
    out.push_back(static_cast<char>(value & 0xFF));
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
    out.push_back(static_cast<char>((value >> 16) & 0xFF));
    out.push_back(static_cast<char>((value >> 24) & 0xFF));
}

std::string decodeBase64(const std::string& text) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int bits = 0;
    int value = 0;
    std::string out;
    for (char ch : text) {
        if (ch == '=') {
            break;
        }
        const char* found = std::strchr(alphabet, ch);
        if (found == nullptr) {
            continue;
        }
        value = (value << 6) | static_cast<int>(found - alphabet);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((value >> bits) & 0xFF));
        }
    }
    return out;
}

std::string makeZipWithWordDocument(const std::string& compressed, uint32_t rawSize) {
    const std::string name = "word/document.xml";
    std::string zip;
    // 本地文件头
    appendU32Le(zip, 0x04034B50u);
    appendU16Le(zip, 20);
    appendU16Le(zip, 0);
    appendU16Le(zip, 8);   // deflate
    appendU16Le(zip, 0);
    appendU16Le(zip, 0);
    appendU32Le(zip, 0);   // CRC 不参与正文提取
    appendU32Le(zip, static_cast<uint32_t>(compressed.size()));
    appendU32Le(zip, rawSize);
    appendU16Le(zip, static_cast<uint16_t>(name.size()));
    appendU16Le(zip, 0);
    zip += name;
    zip += compressed;

    // 中央目录
    const uint32_t centralOffset = static_cast<uint32_t>(zip.size());
    appendU32Le(zip, 0x02014B50u);
    appendU16Le(zip, 20);
    appendU16Le(zip, 20);
    appendU16Le(zip, 0);
    appendU16Le(zip, 8);
    appendU16Le(zip, 0);
    appendU16Le(zip, 0);
    appendU32Le(zip, 0);
    appendU32Le(zip, static_cast<uint32_t>(compressed.size()));
    appendU32Le(zip, rawSize);
    appendU16Le(zip, static_cast<uint16_t>(name.size()));
    appendU16Le(zip, 0);
    appendU16Le(zip, 0);
    appendU16Le(zip, 0);
    appendU16Le(zip, 0);
    appendU32Le(zip, 0);
    appendU32Le(zip, 0);
    zip += name;
    const uint32_t centralSize = static_cast<uint32_t>(zip.size()) - centralOffset;

    // 中央目录结束记录
    appendU32Le(zip, 0x06054B50u);
    appendU16Le(zip, 0);
    appendU16Le(zip, 0);
    appendU16Le(zip, 1);
    appendU16Le(zip, 1);
    appendU32Le(zip, centralSize);
    appendU32Le(zip, centralOffset);
    appendU16Le(zip, 0);
    return zip;
}

void testImportHelpers() {
    std::printf("导入：文件名规范化与后缀判断\n");
    CHECK(quiz::io::isBankTextFileName("demo.quiz"));
    CHECK(quiz::io::isBankTextFileName("题库.TXT"));
    CHECK(quiz::io::isBankTextFileName("a.text"));
    CHECK(quiz::io::isBankTextFileName("题库.DOC"));
    CHECK(quiz::io::isBankTextFileName("题库.docx"));
    CHECK(quiz::io::isBankTextFileName("模板.dotm"));
    CHECK(!quiz::io::isBankTextFileName("demo.json"));
    CHECK(!quiz::io::isBankTextFileName("quiz"));

    CHECK_EQ(quiz::io::normalizeBankFileName("demo.quiz"), std::string("demo.quiz"));
    CHECK_EQ(quiz::io::normalizeBankFileName("D:\\下载\\我的题.txt"), std::string("我的题.quiz"));
    CHECK_EQ(quiz::io::normalizeBankFileName("D:\\下载\\题库.docx"), std::string("题库.quiz"));
    CHECK_EQ(quiz::io::normalizeBankFileName("/tmp/a.b.text"), std::string("a.b.quiz"));
    CHECK_EQ(quiz::io::normalizeBankFileName("  .hidden  "), std::string("hidden.quiz"));
    CHECK_EQ(quiz::io::normalizeBankFileName("a:b*c?.txt"), std::string("a_b_c_.quiz"));
    CHECK_EQ(quiz::io::normalizeBankFileName(""), std::string("imported.quiz"));

    // 规范化后的名字能被题库解析器接受（模拟“导入一个 .txt”的完整链路）
    const std::string fileName = quiz::io::normalizeBankFileName("我的题库.txt");
    quiz::io::LoadResult result = quiz::io::loadBank(kSampleBank, fileName);
    CHECK(result.ok());
    CHECK_EQ(result.bank.meta.id, std::string("test-bank"));

    // 旧 .doc 只要内容本身是可读题库文本，也允许进入同一套解析器。
    const quiz::io::DocumentTextResult plainDoc =
        quiz::io::extractBankTextDocument("题库.doc", kSampleBank);
    CHECK(plainDoc.ok);
    CHECK(quiz::io::loadBank(plainDoc.text, "题库.quiz").ok());

    std::string legacyBinary("\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8);
    legacyBinary.append("\0\x01\x02", 3);
    legacyBinary += kSampleBank;
    const quiz::io::DocumentTextResult recoveredDoc =
        quiz::io::extractBankTextDocument("旧版题库.doc", legacyBinary);
    CHECK(recoveredDoc.ok);
    CHECK(quiz::io::loadBank(recoveredDoc.text, "旧版题库.quiz").ok());

    // 真实 DOCX 的 word/document.xml 通常是 deflate。这里用固定压缩样本覆盖
    // ZIP 中央目录、inflate、XML 段落和实体还原整条路径。
    const std::string compressedWordXml = decodeBase64(
        "jZExTgMxEEWvMtqCLuslBcWy3ohcgiKicNZDYuHxGHtgNweg5RbUHACOQ+6BDEJISGjT/Jni/a8Z/"
        "W41kYdHTNlx0NV53VSrvhtby8MDYRCYyIfcjrrai8RWqTzskUyuOWKYyN9yIiO55rRTIycbEw+Ysws78mrZ"
        "NBeKjAtVidyyPZQZi6Qi0m8Ixdx0quxF05fGv5izoKHEL7Ym3M3i4sQjaLjmZOHMULyE9Sm+zf38KXKIJbp"
        "86HGWzoIEGj7enr+POb6+H59eZm0cxXEADVeno+tZ1IQ8YvonVf0UpH7L7z8B");
    const std::string docx = makeZipWithWordDocument(compressedWordXml, 542);
    const quiz::io::DocumentTextResult wordDocument =
        quiz::io::extractBankTextDocument("Word 题库.docx", docx);
    CHECK(wordDocument.ok);
    const quiz::io::LoadResult wordBank =
        quiz::io::loadBank(wordDocument.text, "Word 题库.quiz");
    CHECK(wordBank.ok());
    CHECK_EQ(wordBank.bank.meta.title, std::string("Word & Bank"));
    CHECK_EQ(wordBank.bank.questions.size(), size_t(1));
    if (!wordBank.bank.questions.empty()) {
        CHECK_EQ(wordBank.bank.questions[0].stem, std::string("从 Word 导入"));
    }
}

void testWeightedPractice() {
    std::printf("权重刷题（掌握度 / 加权抽样 / 按位置判完成）\n");
    quiz::io::LoadResult result = quiz::io::loadBank(kSampleBank, "sample.quiz");
    quiz::QuestionBank bank = result.bank;
    quiz::BankProgress progress;
    quiz::QuizSession session;
    session.bind(&bank, &progress);

    // 没做过：掌握度 0。第 2 题没有标签和难度，仍应严格走旧的权重公式；
    // 第 1 题自带标签和难度，会额外乘上标签 / 难度因子（见下面专门测试）。
    CHECK_EQ(session.masteryAt(0), 0.0f);
    CHECK(std::abs(session.weightedWeight(1) - 1.15f) < 0.001f);

    // 答错过一次：掌握度 0.15
    progress.questions[0].answered = true;
    progress.questions[0].correct = false;
    progress.questions[0].wrongCount = 1;
    CHECK(std::abs(session.masteryAt(0) - 0.15f) < 0.001f);

    // 后来答对两次：掌握度 0.75，权重仍高于复习底数、但低于没做过的题
    progress.questions[0].correct = true;
    progress.questions[0].rightCount = 2;
    CHECK(std::abs(session.masteryAt(0) - 0.75f) < 0.001f);
    CHECK(session.weightedWeight(0) > 0.15f);
    CHECK(session.weightedWeight(0) < session.weightedWeight(1));

    // 前面几行是为了测掌握度、故意写过 progress 的；先清掉，下面要看本轮自己的统计
    progress = quiz::BankProgress{};
    session.bind(&bank, &progress);

    // 权重刷题：先抽 min(6, 3) = 3 道（这一批不重复），之后每提交 2 题在队尾补一道
    session.beginWeightedRound(2026, 6, 2);
    CHECK(session.isLiveWeightedRound());
    CHECK_EQ(session.order().size(), size_t(3));
    CHECK_EQ(session.planSize(), 3);
    CHECK_EQ(session.roundStats().answered, 0);
    CHECK_EQ(session.planRemaining(), 3);

    // 第 1 题（单选，A 对）：提交后立刻记一次正确；还没到 2 题，不补
    session.jumpTo(session.order()[0]);
    session.selectOption(0);
    session.submit("2026-10-14 10:00:00");
    CHECK_EQ(progress.questions[session.order()[0]].rightCount, 1);
    CHECK_EQ(session.order().size(), size_t(3));
    CHECK_EQ(session.planRemaining(), 2);

    // 第 2 题：故意选一个错的（题型不定），提交时立刻记错，并在队尾补一道
    const int second = session.order()[1];
    const quiz::Question& secondQuestion = bank.questions[static_cast<size_t>(second)];
    int wrongPick = 0;
    if (secondQuestion.type == quiz::QuestionType::Judge) {
        wrongPick = secondQuestion.isJudgeTrue() ? 1 : 0;
    } else {
        for (int i = 0; i < static_cast<int>(secondQuestion.options.size()); ++i) {
            if (std::find(secondQuestion.answer.begin(), secondQuestion.answer.end(), i) ==
                secondQuestion.answer.end()) {
                wrongPick = i;
                break;
            }
        }
    }
    session.jumpTo(second);
    session.selectOption(wrongPick);
    session.submit("2026-10-14 10:01:00");
    CHECK_EQ(progress.questions[second].wrongCount, 1);
    CHECK_EQ(session.order().size(), size_t(4));   // 每 2 题补一道
    CHECK_EQ(session.planSize(), 4);
    CHECK_EQ(session.planRemaining(), 2);          // 4 道里做完 2 道
    CHECK(!session.planCompleted());
}

void testWeightedTagDifficulty() {
    std::printf("权重刷题（标签 / 难度 / 即时刷新）\n");
    quiz::QuestionBank bank;
    bank.meta.id = "weighted-tag-difficulty";
    const auto addQuestion = [&](const char* id, const std::vector<std::string>& tags,
                                 int difficulty) {
        quiz::Question question;
        question.id = id;
        question.type = quiz::QuestionType::Single;
        question.stem = id;
        question.options = {"甲", "乙"};
        question.answer = {0};
        question.tags = tags;
        question.difficulty = difficulty;
        bank.questions.push_back(question);
        return static_cast<int>(bank.questions.size()) - 1;
    };

    // 0：无标签、无难度 —— 旧公式兼容性 + “答错后立即变重”的测试目标
    const int plain = addQuestion("plain", {}, 0);
    // 1~4：两个同掌握度的目标题，分别挂弱标签和强标签；统计记录放在另外两题上
    const int weakTarget = addQuestion("weak-target", {"weak"}, 0);
    const int weakHistory = addQuestion("weak-history", {}, 0);
    const int strongTarget = addQuestion("strong-target", {"strong"}, 0);
    const int strongHistory = addQuestion("strong-history", {}, 0);
    // 5~8：无标签，只比较难度层统计。d1 历史 2 对 1 错，d5 历史 0 对 3 错
    const int easyTarget = addQuestion("easy-target", {}, 1);
    const int easyHistory = addQuestion("easy-history", {}, 1);
    const int hardTarget = addQuestion("hard-target", {}, 5);
    const int hardHistory = addQuestion("hard-history", {}, 5);

    quiz::BankProgress progress;
    quiz::QuizSession session;
    session.bind(&bank, &progress);

    // 没有标签 / 难度：与旧公式逐值一致
    CHECK(std::abs(session.weightedWeight(plain) - 1.15f) < 0.001f);

    // 用户自己加的标签也参与统计：弱标签 0/3，强标签 3/3。
    // 两个目标题都未作答、无难度，所以掌握度与难度因子完全相同，只剩标签差异。
    progress.questions[weakHistory].tags = {"weak"};
    progress.questions[weakHistory].answered = true;
    progress.questions[weakHistory].correct = false;
    progress.questions[weakHistory].wrongCount = 3;
    progress.questions[strongHistory].tags = {"strong"};
    progress.questions[strongHistory].answered = true;
    progress.questions[strongHistory].correct = true;
    progress.questions[strongHistory].rightCount = 3;
    const float weakWeight = session.weightedWeight(weakTarget);
    const float strongWeight = session.weightedWeight(strongTarget);
    CHECK(weakWeight > strongWeight * 1.5f);

    // 难度层统计：样本 >=3 后使用真实正确率；未作答的简单题（历史 2/3）比
    // 同层目标更接近 0.65 的目标概率，因此权重应显著高于历史 0/3 的 d5 目标。
    progress.questions[easyHistory].answered = true;
    progress.questions[easyHistory].rightCount = 2;
    progress.questions[easyHistory].wrongCount = 1;
    progress.questions[hardHistory].answered = false;
    progress.questions[hardHistory].rightCount = 0;
    progress.questions[hardHistory].wrongCount = 3;
    CHECK(session.weightedWeight(easyTarget) > session.weightedWeight(hardTarget) * 2.0f);

    // 即时刷新：先有两对记录，再答错一次。weightedWeight 必须读到刚更新的次数，
    // 不能等下一次重建会话；旧权重从 0.40 左右升到 0.90。
    progress.questions[plain].answered = true;
    progress.questions[plain].correct = true;
    progress.questions[plain].rightCount = 2;
    const float beforeWrong = session.weightedWeight(plain);
    session.jumpTo(plain);
    session.selectOption(1);   // 正确答案是 A，这里故意选 B
    session.submit("2026-10-18 21:00:00");
    const float afterWrong = session.weightedWeight(plain);
    CHECK_EQ(progress.questions[plain].wrongCount, 1);
    CHECK(afterWrong > beforeWrong);
}
void testTaggedExport() {
    std::printf("按标签导出\n");
    quiz::io::LoadResult result = quiz::io::loadBank(kSampleBank, "sample.quiz");
    const quiz::QuestionBank& bank = result.bank;
    quiz::BankProgress progress;
    progress.questions.resize(bank.questions.size());
    progress.questions[1].tags = {"易错"};   // 用户自己给第 2 题加的标签

    // 题库自带的标签（第 1 题 tags = 数学,基础）
    const quiz::QuestionBank math = quiz::io::buildTaggedBank(bank, progress, {"数学"}, false);
    CHECK_EQ(math.questions.size(), size_t(1));
    CHECK_EQ(math.questions[0].stem, bank.questions[0].stem);
    CHECK(math.questions[0].tags.size() == 2);

    // 用户加的标签也能被选中；不固化时导出的题不带它
    const quiz::QuestionBank byUser = quiz::io::buildTaggedBank(bank, progress, {"易错"}, false);
    CHECK_EQ(byUser.questions.size(), size_t(1));
    CHECK_EQ(byUser.questions[0].tags.size(), size_t(0));

    // 固化：用户标签写进导出的题目，落盘再读回来还在
    const quiz::QuestionBank baked = quiz::io::buildTaggedBank(bank, progress, {"易错"}, true);
    CHECK_EQ(baked.questions.size(), size_t(1));
    CHECK_EQ(baked.questions[0].tags.size(), size_t(1));
    CHECK_EQ(baked.questions[0].tags[0], std::string("易错"));
    quiz::io::LoadResult reloaded = quiz::io::loadBank(quiz::io::saveBank(baked), "tagged.quiz");
    CHECK(reloaded.ok());
    CHECK_EQ(reloaded.bank.questions[0].tags.size(), size_t(1));
    CHECK_EQ(reloaded.bank.questions[0].tags[0], std::string("易错"));

    // 多选 = 或
    const quiz::QuestionBank both = quiz::io::buildTaggedBank(bank, progress, {"数学", "易错"}, false);
    CHECK_EQ(both.questions.size(), size_t(2));

    // 一个都没选：结果为空（调用方会提示「还没选标签」）
    CHECK(quiz::io::buildTaggedBank(bank, progress, {}, true).questions.empty());

    // 没命中任何题：也是空
    CHECK(quiz::io::buildTaggedBank(bank, progress, {"不存在的标签"}, false).questions.empty());
}

void testWrongOnlyExport() {
    std::printf("导出错题本\n");
    quiz::io::LoadResult result = quiz::io::loadBank(kSampleBank, "sample.quiz");
    quiz::QuestionBank bank = result.bank;
    quiz::BankProgress progress;
    progress.resizeTo(3);
    progress.questions[1].answered = true;
    progress.questions[1].correct = false;

    quiz::QuestionBank wrongOnly = quiz::io::buildWrongOnlyBank(bank, progress, 3);
    CHECK_EQ(wrongOnly.questions.size(), size_t(1));
    CHECK_EQ(wrongOnly.questions[0].stem, bank.questions[1].stem);
    CHECK(wrongOnly.meta.id.find("wrong") != std::string::npos);
}

void testI18n() {
    std::printf("语言包\n");
    quiz::i18n::I18n i18n;
    CHECK(i18n.usingBuiltin());
    CHECK_EQ(std::string(i18n.text("ui.app_title")), std::string("AxtUebung"));
    CHECK_EQ(std::string(i18n.text("不存在的键")), std::string("不存在的键"));

    const std::string pack =
        "[lang]\ncode = en\nname = English\n"
        "[text]\nui.app_title = AxeQuiz\nui.home.start = Start\n";
    std::string error;
    CHECK(i18n.loadPackText(pack, "en.lang", &error));
    CHECK_EQ(i18n.language(), std::string("en"));
    CHECK_EQ(std::string(i18n.text("ui.app_title")), std::string("AxeQuiz"));
    CHECK_EQ(std::string(i18n.text("ui.home.start")), std::string("Start"));
    // 语言包里没写的键回退到内置中文
    CHECK_EQ(std::string(i18n.text("ui.quiz.submit")), std::string("提交答案"));

    const std::string formatted = i18n.format("ui.result.summary",
                                              {{"total", "8"}, {"answered", "5"}, {"correct", "4"},
                                               {"rate", "80%"}});
    CHECK(formatted.find("80%") != std::string::npos);

    // 空语言包应当被拒绝
    quiz::i18n::I18n another;
    CHECK(!another.loadPackText("[lang]\ncode = xx\n", "xx.lang", &error));
}

void testSettings() {
    std::printf("设置读写\n");

    // 默认值：两个开关都关，停顿时间在合理范围
    const quiz::Settings defaults;
    CHECK(!defaults.autoNextOnCorrect);
    CHECK(!defaults.autoSubmitSingle);
    CHECK(!defaults.paperSubmitWholePage);
    CHECK(defaults.autoNextDelay() > 0.9f);
    CHECK(defaults.autoNextDelay() < 1.5f);
    // 权重刷题补题间隔：默认 5，写歪了夹到 2~100
    CHECK_EQ(defaults.weightedAppendEvery, 5);
    const quiz::Settings appendEdited = quiz::Settings::parse("[ui]\nweighted_append_every = 1\n");
    CHECK_EQ(appendEdited.weightedAppendEvery, quiz::Settings::kMinWeightedAppend);
    const quiz::Settings appendBig = quiz::Settings::parse("[ui]\nweighted_append_every = 999\n");
    CHECK_EQ(appendBig.weightedAppendEvery, quiz::Settings::kMaxWeightedAppend);
    CHECK(defaults.serialize().find("weighted_append_every = 5") != std::string::npos);   // 写盘也要带上这个键

    // 开 → 存 → 读，值要原样回来
    quiz::Settings value;
    value.autoNextOnCorrect = true;
    value.autoSubmitSingle = true;
    value.paperSubmitWholePage = true;
    value.practiceOrder = "shuffle";
    value.autoNextDelaySeconds = 2.5f;
    const quiz::Settings loaded = quiz::Settings::parse(value.serialize());
    CHECK(loaded.autoNextOnCorrect);
    CHECK(loaded.autoSubmitSingle);
    CHECK(loaded.paperSubmitWholePage);
    CHECK_EQ(loaded.practiceOrder, std::string("shuffle"));
    CHECK(std::fabs(loaded.autoNextDelay() - 2.5f) < 0.01f);

    // 关掉也要能存回去（不能只写得进 true）
    quiz::Settings off;
    const quiz::Settings reloaded = quiz::Settings::parse(off.serialize());
    CHECK(!reloaded.autoNextOnCorrect);
    CHECK(!reloaded.autoSubmitSingle);

    // 手改配置文件：写得离谱的值夹到合理范围，别的键不受影响
    const quiz::Settings edited =
        quiz::Settings::parse("[ui]\nauto_submit_single = yes\nauto_next_delay = 99\n");
    CHECK(edited.autoSubmitSingle);
    CHECK(!edited.autoNextOnCorrect);
    CHECK(edited.autoNextDelay() <= 5.0f);
    CHECK(edited.autoNextDelay() >= 0.3f);

    // 空文件 / 段落不对：全部走默认值，不能崩
    const quiz::Settings empty = quiz::Settings::parse("");
    CHECK(!empty.autoNextOnCorrect);
    CHECK(!empty.autoSubmitSingle);
    const quiz::Settings otherSection = quiz::Settings::parse("[other]\nauto_next_on_correct = 1\n");
    CHECK(!otherSection.autoNextOnCorrect);

    // 记事本「另存为 UTF-8」会带 BOM，不能因此把整份设置读丢
    const std::string withBom = std::string("\xEF\xBB\xBF") + "[ui]\nauto_next_on_correct = 1\n";
    CHECK(quiz::Settings::parse(withBom).autoNextOnCorrect);

    // 「每轮题数」：存成 round_size；老配置里的 weighted_round_size 也认；越界夹回 5~200
    quiz::Settings sized;
    sized.roundSize = 42;
    CHECK_EQ(quiz::Settings::parse(sized.serialize()).roundSize, 42);
    CHECK_EQ(quiz::Settings::parse("[ui]\nround_size = 3\n").roundSize, 5);
    CHECK_EQ(quiz::Settings::parse("[ui]\nround_size = 999\n").roundSize, 200);
    CHECK_EQ(quiz::Settings::parse("[ui]\nweighted_round_size = 33\n").roundSize, 33);

    // 首页背景设置：来源、模式、透明度、铺放与三段覆盖
    CHECK_EQ(defaults.homeBackgroundSource, std::string("none"));
    CHECK_EQ(defaults.homeBackgroundMode, std::string("auto"));
    CHECK_EQ(defaults.homeBackgroundOpacity, 22);
    CHECK_EQ(defaults.homeBackgroundFit, std::string("cover"));
    CHECK(defaults.homeBackgroundTop);
    CHECK(defaults.homeBackgroundBottom);
    quiz::Settings background;
    background.homeBackgroundSource = "asset:Doppelaxt.png";
    background.homeBackgroundMode = "tint";
    background.homeBackgroundOpacity = 64;
    background.homeBackgroundFit = "contain";
    background.homeBackgroundTop = false;
    background.homeBackgroundBottom = false;
    const quiz::Settings backgroundBack = quiz::Settings::parse(background.serialize());
    CHECK_EQ(backgroundBack.homeBackgroundSource, std::string("asset:Doppelaxt.png"));
    CHECK_EQ(backgroundBack.homeBackgroundMode, std::string("tint"));
    CHECK_EQ(backgroundBack.homeBackgroundOpacity, 64);
    CHECK_EQ(backgroundBack.homeBackgroundFit, std::string("contain"));
    CHECK(!backgroundBack.homeBackgroundTop);
    CHECK(!backgroundBack.homeBackgroundBottom);
    CHECK(!defaults.manualIntroDismissed);
    background.manualIntroDismissed = true;
    CHECK(quiz::Settings::parse(background.serialize()).manualIntroDismissed);
    CHECK_EQ(quiz::Settings::parse("[ui]\nhome_background_opacity = 999\n")
                 .homeBackgroundOpacity,
             quiz::Settings::kMaxBackgroundOpacity);

    // 界面比例：默认跟随窗口；合法比例能读回来；乱写的当作跟随窗口
    int aspectWidth = 0;
    int aspectHeight = 0;
    CHECK(!defaults.uiAspectRatio(aspectWidth, aspectHeight));

    quiz::Settings aspect;
    aspect.uiAspect = "16:9";
    const quiz::Settings aspectLoaded = quiz::Settings::parse(aspect.serialize());
    CHECK_EQ(aspectLoaded.uiAspect, std::string("16:9"));
    CHECK(aspectLoaded.uiAspectRatio(aspectWidth, aspectHeight));
    CHECK_EQ(aspectWidth, 16);
    CHECK_EQ(aspectHeight, 9);

    const quiz::Settings autoAspect = quiz::Settings::parse("[ui]\nui_aspect = auto\n");
    CHECK(!autoAspect.uiAspectRatio(aspectWidth, aspectHeight));
    const quiz::Settings badAspect = quiz::Settings::parse("[ui]\nui_aspect = 随便写\n");
    CHECK(!badAspect.uiAspectRatio(aspectWidth, aspectHeight));

    // 配色：合法写法能解析（大小写都行、# 可有可无），非法写法一律拒绝
    unsigned int rgb = 0;
    CHECK(quiz::Settings::parseHexColor("96DCB4", rgb));
    CHECK_EQ(rgb, 0x96DCB4u);
    CHECK(quiz::Settings::parseHexColor("#f08c82", rgb));
    CHECK_EQ(rgb, 0xF08C82u);
    CHECK(!quiz::Settings::parseHexColor("96DCB", rgb));
    CHECK(!quiz::Settings::parseHexColor("gggggg", rgb));
    CHECK(!quiz::Settings::parseHexColor("", rgb));

    quiz::Settings colored;
    colored.uiColorAccent = "123456";
    colored.uiColorScrollbar = "8D969D";
    const quiz::Settings coloredLoaded = quiz::Settings::parse(colored.serialize());
    CHECK_EQ(coloredLoaded.uiColorAccent, std::string("123456"));
    CHECK_EQ(coloredLoaded.uiColorScrollbar, std::string("8D969D"));}


void testThemePresets() {
    std::printf("主题预设\n");

    // 现在的格式：键名与 settings.txt 一致（ui_color_*）
    const std::string text =
        "# 主题预设\n\nid = 1\nname = 现代简约\nui_color_bg = F4F5F7\nui_color_panel = FFFFFF\n"
        "ui_color_text = 333333\nui_color_button = C0D8F0\nui_color_accent = 50E3C2\n"
        "ui_color_correct = 52C41A\nui_color_wrong = F5222D\n\n"
        "id = 2\nname = 暗色护眼\nui_color_bg = 1E1E1E\nui_color_text = E0E0E0\n";

    const std::vector<quiz::ThemePreset> presets = quiz::parseThemePresets(text);
    CHECK_EQ(presets.size(), static_cast<size_t>(2));
    CHECK_EQ(presets[0].name, std::string("现代简约"));
    CHECK_EQ(presets[0].id, std::string("1"));
    CHECK_EQ(presets[0].color("ui_color_bg"), std::string("F4F5F7"));
    CHECK_EQ(presets[0].color("ui_color_accent"), std::string("50E3C2"));
    CHECK_EQ(presets[0].colors.size(), static_cast<size_t>(7));
    CHECK_EQ(presets[1].color("ui_color_wrong"), std::string());   // 这一套没写 wrong

    // 早期那套短键名（bg / panel / ...）仍然认
    const std::vector<quiz::ThemePreset> legacy =
        quiz::parseThemePresets("id=9\nname = 老格式\nbg = 111111\naccent = 222222\n");
    CHECK_EQ(legacy.size(), static_cast<size_t>(1));
    CHECK_EQ(legacy[0].color("ui_color_bg"), std::string("111111"));
    CHECK_EQ(legacy[0].color("ui_color_accent"), std::string("222222"));

    // 7 项都合法 → 全部套用
    quiz::Settings settings;
    CHECK_EQ(quiz::applyThemePreset(presets[0], settings), 7);
    CHECK_EQ(settings.uiColorBackground, std::string("F4F5F7"));
    CHECK_EQ(settings.uiColorText, std::string("333333"));
    CHECK_EQ(settings.uiColorAccent, std::string("50E3C2"));

    // 其中一项写坏了 → 只套用 6 项，坏的那项保持原值
    quiz::ThemePreset broken = presets[0];
    for (quiz::ThemeColor& entry : broken.colors) {
        if (entry.key == "ui_color_accent") {
            entry.value = "GGGGGG";
        }
    }
    const std::string previousAccent = settings.uiColorAccent;
    CHECK_EQ(quiz::applyThemePreset(broken, settings), 6);
    CHECK_EQ(settings.uiColorAccent, previousAccent);
    CHECK_EQ(settings.uiColorBackground, std::string("F4F5F7"));

    // 没有 name 的块会被丢掉；不认识的键名忽略
    CHECK_EQ(quiz::parseThemePresets("id=9\nui_color_bg = 000000\n").size(), static_cast<size_t>(0));
    CHECK_EQ(quiz::parseThemePresets("name = 未知键\nwhatever = 000000\n")[0].colors.size(),
             static_cast<size_t>(0));
}

// 真文件也要能套上颜色。
// 为什么专门加这一条：themes.txt 曾经从「7 个短键名」改成「16 个 ui_color_*」，
// 而解析器没跟着改 —— 结果 11 套内置预设一个颜色都没应用（只记住了名字），
// 单测里用的又是老格式，谁都没发现。这条测试直接对着 exe 旁边那份 assets 跑。
void testThemePresetFile() {
    std::printf("主题预设文件（assets/presets/themes.txt）\n");
    const std::string path = g_exeDirectory + "/assets/presets/themes.txt";
    std::FILE* file = std::fopen(path.c_str(), "rb");
    CHECK(file != nullptr);
    if (file == nullptr) {
        std::printf("  读不到 %s（先构建一次，assets 会被复制到 exe 旁边）\n", path.c_str());
        return;
    }
    std::string text;
    char buffer[4096];
    size_t read = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        text.append(buffer, read);
    }
    std::fclose(file);

    const std::vector<std::string>& keys = quiz::themeColorKeys();
    CHECK_EQ(keys.size(), static_cast<size_t>(18));

    const std::vector<quiz::ThemePreset> presets = quiz::parseThemePresets(text);
    CHECK(presets.size() >= static_cast<size_t>(5));
    for (const quiz::ThemePreset& preset : presets) {
        CHECK(!preset.name.empty());
        CHECK_EQ(preset.colors.size(), keys.size());   // 每套都要写全 18 项
        for (const std::string& key : keys) {
            CHECK(!preset.color(key).empty());
        }
        quiz::Settings settings;
        CHECK_EQ(quiz::applyThemePreset(preset, settings), static_cast<int>(keys.size()));
    }
}

}  // namespace

// 错题本：被动移出（对/错次数比例）+ 主动移出（重置成 0 错 1 对）+ 本轮统计
void testWrongBookRule() {
    std::printf("错题本：被动 / 主动移出 + 本轮统计\n");

    // ---- 判据：只看次数比例，不看「最后一次对不对」 ----
    quiz::QuestionProgress item;
    CHECK(!quiz::isWrongBookItem(item, 3));   // 没答过
    item.answered = true;
    item.correct = false;
    CHECK(quiz::isWrongBookItem(item, 3));    // 第一次就答错 → 进错题本

    item.wrongCount = 1;
    item.correct = true;
    item.rightCount = 1;
    CHECK(quiz::isWrongBookItem(item, 3));    // 答对 1 次还不够（1 < 3×1）
    item.rightCount = 2;
    CHECK(quiz::isWrongBookItem(item, 3));    // 2 < 3 → 仍在
    item.rightCount = 3;
    CHECK(!quiz::isWrongBookItem(item, 3));   // 达到 3 倍 → 被动移出
    CHECK(quiz::isWrongBookItem(item, 4));    // 门槛调高 → 又算错题
    CHECK(!quiz::isWrongBookItem(item, 1));   // 门槛调低 → 也是移出

    // ---- 会话：本轮统计 + 主动移出 ----
    const std::string text =
        "[meta]\nid = wb\ntitle = wb\n"
        "[q]\ntype = single\nstem = a\noption = 甲\noption = 乙\nanswer = A\n"
        "[q]\ntype = single\nstem = b\noption = 甲\noption = 乙\nanswer = A\n";
    const quiz::io::LoadResult result = quiz::io::loadBank(text, "wb.quiz");
    CHECK(result.ok());
    if (!result.ok()) {
        return;
    }
    quiz::BankProgress progress;
    progress.resizeTo(2);
    progress.questions[0].answered = true;
    progress.questions[0].correct = false;
    progress.questions[0].wrongCount = 1;

    quiz::QuizSession session;
    session.bind(&result.bank, &progress);
    session.setRoundStart("2026-10-16 10:00:00");

    // 新一轮还没动过：本轮统计必须是 0（以前这里显示的是题库历史数据）
    CHECK_EQ(session.roundStats().answered, 0);
    CHECK_EQ(session.roundStats().correct, 0);

    // 只刷错题：这一轮只有第 1 题，并且被标记为「从错题本进来」
    session.resetOrderWrongOnly(3);
    CHECK(session.isWrongOnlyRound());
    CHECK_EQ(session.order().size(), size_t(1));
    CHECK_EQ(session.order()[0], 0);

    // 答对这道题 → 本轮统计 +1，然后主动移出
    session.jumpTo(0);
    session.selectOption(0);
    session.submit("2026-10-16 10:00:05");
    CHECK(session.isCorrect());
    CHECK_EQ(session.roundStats().answered, 1);
    CHECK_EQ(session.roundStats().correct, 1);
    CHECK(session.removeFromWrongBook(0, "2026-10-16 10:00:06"));
    CHECK(progress.questions[0].answered);
    CHECK(progress.questions[0].correct);
    CHECK_EQ(progress.questions[0].rightCount, 1);
    CHECK_EQ(progress.questions[0].wrongCount, 0);
    CHECK(!quiz::isWrongBookItem(progress.questions[0], 3));   // 移出后不再是错题

    // 换计划之后就不再是「从错题本进来的那一轮」
    session.resetOrder(0);
    CHECK(!session.isWrongOnlyRound());
}

// 背题：整库顺序全排（不受每轮题数限制）、界面直接亮答案、
// 以前没做对过的记成错题；不计入累计统计
void testReciteMode() {
    std::printf("背题模式\n");
    const std::string text =
        "[meta]\nid = recite\ntitle = recite\n"
        "[q]\nid = r1\ntype = single\nstem = a\noption = 甲\noption = 乙\nanswer = B\n"
        "[q]\nid = r2\ntype = single\nstem = b\noption = 甲\noption = 乙\nanswer = A\n"
        "[q]\nid = r3\ntype = fill\nstem = c【1】\nanswer = 甲乙|甲乙丙\n";
    const quiz::io::LoadResult result = quiz::io::loadBank(text, "recite.quiz");
    CHECK(result.ok());
    if (!result.ok()) {
        return;
    }

    quiz::BankProgress progress;
    progress.resizeTo(3);
    // 第 2 题以前做对过：背题不该把它改坏
    progress.questions[1].answered = true;
    progress.questions[1].correct = true;
    progress.questions[1].rightCount = 3;
    progress.questions[1].answeredAt = "2026-10-15 08:00:00";

    quiz::QuizSession session;
    session.bind(&result.bank, &progress);
    session.resetOrderRecite();
    CHECK(session.isReciteMode());
    CHECK_EQ(session.order().size(), size_t(3));   // 整库按题号顺序，不受每轮题数限制
    CHECK_EQ(session.order()[0], 0);
    CHECK_EQ(session.order()[2], 2);

    // 第 1 题（没做过）：亮答案 + 记成错题
    CHECK(session.reciteRevealAt(0, "2026-10-16 00:00:00"));
    CHECK(session.isSubmittedAt(0));                 // 界面据此显示「已交卷」的样子
    CHECK(progress.questions[0].answered);
    CHECK(!progress.questions[0].correct);           // 记为错题 → 会进错题本
    CHECK_EQ(progress.questions[0].wrongCount, 0);   // 但不算「答错一次」
    CHECK_EQ(session.selectionAt(0).size(), size_t(1));
    CHECK_EQ(session.selectionAt(0)[0], 1);          // 标准答案是 B
    CHECK_EQ(session.takeNewAnswers().size(), size_t(0));   // 背题不计入累计统计

    // 第 2 题（以前做对过）：只更新「本轮过了一遍」的时间，对错与次数都不动
    CHECK(session.reciteRevealAt(1, "2026-10-16 00:00:01"));
    CHECK(progress.questions[1].answered);
    CHECK(progress.questions[1].correct);
    CHECK_EQ(progress.questions[1].rightCount, 3);
    CHECK_EQ(progress.questions[1].answeredAt, std::string("2026-10-16 00:00:01"));

    // 第 3 题（填空）：草稿里直接填上标准答案（第一个可接受写法）
    CHECK(session.reciteRevealAt(2, "2026-10-16 00:00:02"));
    CHECK_EQ(session.fillTextAt(2, 0), std::string("甲乙"));

    // 同一道题再叫一次：不重复处理
    CHECK(!session.reciteRevealAt(2, "2026-10-16 00:00:03"));

    // 换计划（乱序）之后就不再是背题
    session.shuffleOrder(7);
    CHECK(!session.isReciteMode());
}

// 「正在作答但还没提交」（题目总览的蓝色状态就是看它）
void testDraftState() {
    std::printf("正在作答（未提交）状态\n");
    const std::string text =
        "[meta]\nid = draft\ntitle = draft\n"
        "[q]\ntype = single\nstem = a\noption = 甲\noption = 乙\nanswer = A\n";
    const quiz::io::LoadResult result = quiz::io::loadBank(text, "draft.quiz");
    CHECK(result.ok());
    if (!result.ok()) {
        return;
    }
    quiz::BankProgress progress;
    progress.resizeTo(1);
    quiz::QuizSession session;
    session.bind(&result.bank, &progress);
    CHECK(!session.hasDraftAt(0));      // 刚进来：什么都没动 → 灰
    session.selectOption(0);
    CHECK(session.hasDraftAt(0));       // 选了但没提交 → 蓝
    session.clearSelection();
    CHECK(!session.hasDraftAt(0));
    session.submit("2026-10-16 21:00:00");
    CHECK(!session.hasDraftAt(0));      // 提交后（草稿清掉不算数）：状态改看提交结果
}

void testPracticeOrder() {
    std::printf("练习顺序（顺序 / 乱序）\n");
    const std::string text =
        "[meta]\nid = order\ntitle = order\n"
        "[q]\ntype = single\nstem = a\noption = 甲\noption = 乙\nanswer = A\n"
        "[q]\ntype = single\nstem = b\noption = 甲\noption = 乙\nanswer = A\n"
        "[q]\ntype = single\nstem = c\noption = 甲\noption = 乙\nanswer = A\n"
        "[q]\ntype = single\nstem = d\noption = 甲\noption = 乙\nanswer = A\n";
    quiz::io::LoadResult result = quiz::io::loadBank(text, "order.quiz");
    CHECK(result.ok());
    if (!result.ok()) {
        return;
    }
    quiz::BankProgress progress;
    quiz::QuizSession session;
    session.bind(&result.bank, &progress);

    // 默认顺序：0,1,2,3（一道都没做过，整库都在这一轮）
    CHECK_EQ(session.order().size(), size_t(4));
    CHECK_EQ(session.currentIndex(), 0);
    CHECK(session.planPosition() == 1);
    CHECK_EQ(session.planRemaining(), 4);
    CHECK(!session.planCompleted());
    session.next();
    CHECK_EQ(session.currentIndex(), 1);
    session.prev();
    CHECK_EQ(session.currentIndex(), 0);

    // 乱序：同样的 seed 结果一样，内容还是这四个
    session.shuffleOrder(1234);
    const std::vector<int> shuffled = session.order();
    CHECK_EQ(shuffled.size(), size_t(4));
    int sum = 0;
    for (int index : shuffled) {
        sum += index;
    }
    CHECK_EQ(sum, 6);   // 0+1+2+3：既不重复也不漏
    session.shuffleOrder(1234);
    CHECK_EQ(session.order(), shuffled);

    // 「每轮题数」：顺序 / 乱序都只取前 N 道（权重刷题本来就按它抽题）。
    // 这里用单独的 session + 干净的进度来测，免得把上面那份计划换掉、影响后面的断言
    {
        quiz::BankProgress fresh;
        quiz::QuizSession limited;
        limited.bind(&result.bank, &fresh);
        CHECK_EQ(limited.order().size(), size_t(4));   // 默认还是不限制
        limited.resetOrder(2);                         // 顺序：本轮只看前 2 道
        CHECK_EQ(limited.order().size(), size_t(2));
        CHECK_EQ(limited.order()[0], 0);
        CHECK_EQ(limited.order()[1], 1);
        limited.shuffleOrder(1234, 3);                 // 乱序：洗好后取 3 道
        CHECK_EQ(limited.order().size(), size_t(3));
        limited.resetOrder(99);                        // 比题库总数还大 → 整库
        CHECK_EQ(limited.order().size(), size_t(4));
        limited.resetOrder();                          // 不带参数 = 不限制（老行为）
        CHECK_EQ(limited.order().size(), size_t(4));
    }

    // 乱序下 next/prev 走的是顺序表，jumpTo 认的是原始下标
    session.jumpTo(0);
    CHECK_EQ(session.currentIndex(), 0);
    CHECK(session.orderPositionOf(0) >= 0);   // 乱序后 0 在第几位不固定，能跳到就行
    session.jumpTo(shuffled.back());
    CHECK_EQ(session.currentIndex(), shuffled.back());
    CHECK_EQ(session.orderPositionOf(shuffled.back()), 3);
    session.next();   // 已经是最后一个，不动
    CHECK_EQ(session.currentIndex(), shuffled.back());

    // 筛题目：这一轮只练 0 和 2（权重/错题模式将来就是这么塞进来的）
    session.setOrder({0, 2});
    CHECK_EQ(session.order().size(), size_t(2));
    session.jumpTo(2);
    CHECK_EQ(session.currentIndex(), 2);
    session.jumpTo(1);   // 1 不在这一轮里 → 不动
    CHECK_EQ(session.currentIndex(), 2);
    session.next();
    CHECK_EQ(session.currentIndex(), 2);

    // 空顺序兜底成顺序刷，并且保留当前这道题（不回退到第一题）
    session.setOrder({});
    CHECK_EQ(session.order().size(), size_t(4));
    CHECK_EQ(session.currentIndex(), 2);

    // 「跳过已答对」：把 0、2 答对，1 答错，3 没做 → 这一轮只剩 1 和 3
    progress.questions[0].answered = true;
    progress.questions[0].correct = true;
    progress.questions[1].answered = true;
    progress.questions[1].correct = false;
    progress.questions[2].answered = true;
    progress.questions[2].correct = true;
    session.resetOrder();
    CHECK_EQ(session.order().size(), size_t(2));
    CHECK_EQ(session.order()[0], 1);
    CHECK_EQ(session.order()[1], 3);

    // 只刷错题：只有 1
    session.resetOrderWrongOnly(3);
    CHECK_EQ(session.order().size(), size_t(1));
    CHECK_EQ(session.order()[0], 1);

    // 乱序：全库都进这一轮（含已经答对的 0、2 当复习），只把新题排在前面一点
    session.shuffleOrder(99);
    CHECK_EQ(session.order().size(), size_t(4));
    // 权重差距 ≤10%：新 1.00 > 错 0.95 > 对 0.90（只影响先后，不影响出现与否）
    CHECK(std::abs(session.shuffleWeight(3) - 1.00f) < 0.001f);   // 3 没做过
    CHECK(std::abs(session.shuffleWeight(1) - 0.95f) < 0.001f);   // 1 答错过
    CHECK(std::abs(session.shuffleWeight(0) - 0.90f) < 0.001f);   // 0 已答对
    CHECK(session.shuffleWeight(3) > session.shuffleWeight(1));
    CHECK(session.shuffleWeight(1) > session.shuffleWeight(0));
    int poolSum = 0;
    for (int index : session.order()) {
        poolSum += index;
    }
    CHECK_EQ(poolSum, 6);   // 0 + 1 + 2 + 3

    // 本轮进度：把计划里的题都答了 → 计划完成
    for (int index : session.order()) {
        progress.questions[static_cast<size_t>(index)].answered = true;
        progress.questions[static_cast<size_t>(index)].correct = true;
    }
    CHECK(session.planCompleted());
    CHECK_EQ(session.planRemaining(), 0);

    // 全库都答对之后：顺序模式这一轮为空 → 兜底回整库（复习）
    progress.questions[1].correct = true;
    progress.questions[3].answered = true;
    progress.questions[3].correct = true;
    session.resetOrder();
    CHECK_EQ(session.order().size(), size_t(4));

    // 乱序也要从「本轮第一题」开始（光标对到新顺序的第一个）
    session.shuffleOrder(7);
    session.jumpToRoundStart();
    CHECK_EQ(session.currentIndex(), session.order().front());
    CHECK(session.hasNextInPlan());

    // 进入题库时从「本轮第一题」开始：编号最前的没做/做错的那道，而不是上次停的那道
    {
        quiz::BankProgress saved;
        saved.position = 2;
        saved.questions.resize(4);
        saved.questions[0].answered = true;
        saved.questions[0].correct = true;    // 0 已答对 → 不进本轮
        saved.questions[2].answered = true;
        saved.questions[2].correct = false;   // 2 答错 → 在本轮里，但排在 1 后面
        quiz::QuizSession fresh;
        fresh.bind(&result.bank, &saved);
        CHECK_EQ(fresh.order().size(), size_t(3));   // 1、2、3
        CHECK_EQ(fresh.currentIndex(), 1);           // 从编号最前的那个开始
        CHECK_EQ(fresh.planPosition(), 1);
    }

    // 「本轮从什么时候开始」：开始时间之后的作答才算这一轮做完的。
    // （不然进错题本时，那些早就答过的错题会被当成已完成，答一题就跳结算页）
    session.setOrder({0, 1});
    session.setRoundStart("2030-01-01 00:00:00");   // 未来的时间：之前的作答都不算
    CHECK(!session.planCompleted());
    CHECK_EQ(session.planRemaining(), 2);
    session.setRoundStart("");                      // 不设就按「答过就算」
    CHECK(session.planCompleted());
    CHECK_EQ(session.planRemaining(), 0);
}

void testStats() {
    std::printf("累计统计\n");

    quiz::Stats stats;
    CHECK_EQ(stats.totalSubmitted, 0);
    CHECK(std::abs(stats.accuracy() - 0.0f) < 0.001f);

    // 第一天：答 4 题（2 对 1 错 1 对）
    stats.recordAnswer(true, "2026-10-01 09:00:00");
    stats.recordAnswer(false, "2026-10-01 09:01:00");
    stats.recordAnswer(true, "2026-10-01 09:02:00");
    stats.recordAnswer(true, "2026-10-01 22:00:00");
    CHECK_EQ(stats.totalSubmitted, 4);
    CHECK_EQ(stats.totalCorrect, 3);
    CHECK_EQ(stats.currentStreakDays, 1);
    CHECK_EQ(stats.dailySubmitted["2026-10-01"], 4);

    // 第二天连上 → 2 天；隔了几天再答 → 断档重新数
    stats.recordAnswer(true, "2026-10-02 08:00:00");
    CHECK_EQ(stats.currentStreakDays, 2);
    stats.recordAnswer(true, "2026-10-05 08:00:00");
    CHECK_EQ(stats.currentStreakDays, 1);
    CHECK_EQ(stats.bestStreakDays, 2);

    // 时长：负数忽略
    stats.addPracticeSeconds(90.0);
    stats.addPracticeSeconds(-5.0);
    CHECK(std::abs(stats.totalSeconds - 90.0) < 0.01);

    // 完成过的题库：重复记也只算一个
    stats.markBankCompleted("bank-a");
    stats.markBankCompleted("bank-a");
    stats.markBankCompleted("bank-b");
    CHECK_EQ(stats.completedBanks.size(), size_t(2));

    // 最近 3 天（从最早到今天）
    const std::vector<std::pair<std::string, int>> recent = stats.recentDays(3, "2026-10-05");
    CHECK_EQ(recent.size(), size_t(3));
    CHECK_EQ(recent[0].first, std::string("2026-10-03"));
    CHECK_EQ(recent[0].second, 0);
    CHECK_EQ(recent[2].first, std::string("2026-10-05"));
    CHECK_EQ(recent[2].second, 1);

    // 存 → 读
    const quiz::Stats back = quiz::Stats::parse(stats.serialize());
    CHECK_EQ(back.totalSubmitted, 6);
    CHECK_EQ(back.totalCorrect, 5);
    CHECK_EQ(back.bestStreakDays, 2);
    CHECK(std::abs(back.totalSeconds - 90.0) < 0.01);
    CHECK_EQ(back.completedBanks.size(), size_t(2));
    CHECK_EQ(back.dailySubmitted.at("2026-10-01"), 4);
    CHECK_EQ(back.lastAnsweredDay, std::string("2026-10-05"));
    CHECK_EQ(quiz::dayOf("2026-10-05 08:00:00"), std::string("2026-10-05"));
    CHECK_EQ(quiz::dayOf("坏数据"), std::string(""));
    CHECK_EQ(quiz::dayNumber("2026-10-05") - quiz::dayNumber("2026-10-04"), 1);
    CHECK_EQ(quiz::dayText(quiz::dayNumber("2026-12-31") + 1), std::string("2027-01-01"));
}

void testFillQuestion() {
    std::printf("填空题\n");
    const std::string text =
        "[meta]\nid = fill\ntitle = fill\n"
        "[q]\ntype = fill\nstem = 水的化学式是____\nanswer = H2O|水\n";
    const quiz::io::LoadResult result = quiz::io::loadBank(text, "fill.quiz");
    CHECK(result.ok());
    if (!result.ok()) {
        return;
    }
    const quiz::Question& question = result.bank.questions[0];
    CHECK_EQ(question.type, quiz::QuestionType::Fill);
    CHECK_EQ(question.fillBlankCount(), 1);               // 老写法：一个空
    CHECK_EQ(question.fillAnswers[0].size(), size_t(2));  // 两个可接受答案
    CHECK(quiz::fillBlankMatches(question, 0, "h2o"));     // 大小写不敏感
    CHECK(quiz::fillBlankMatches(question, 0, "  H2O  ")); // 首尾空白
    CHECK(quiz::fillBlankMatches(question, 0, "Ｈ２Ｏ"));   // 全角字符
    CHECK(quiz::fillBlankMatches(question, 0, "水"));      // 另一个可接受答案
    CHECK(!quiz::fillBlankMatches(question, 0, "CO2"));
    CHECK(!quiz::fillBlankMatches(question, 0, ""));
}

void testFillMultiBlank() {
    std::printf("多空填空\n");
    const std::string text =
        "[meta]\nid = fill2\ntitle = fill2\n"
        "[q]\ntype = fill\nblanks = 2\nstem = 水的化学式是【1】____，沸点是【2】____\n"
        "answer = H2O|水 ; 100\n";
    const quiz::io::LoadResult result = quiz::io::loadBank(text, "fill2.quiz");
    CHECK(result.ok());
    if (!result.ok()) {
        return;
    }
    const quiz::Question& question = result.bank.questions[0];
    CHECK_EQ(question.fillBlankCount(), 2);
    CHECK_EQ(question.fillAnswers[0].size(), size_t(2));   // 第一个空两个可接受答案
    CHECK_EQ(question.fillAnswers[1].size(), size_t(1));   // 第二个空一个

    // 全对
    const quiz::Score all = quiz::scoreFill(question, {"水", "100"});
    CHECK(all.full);
    CHECK(std::abs(all.value - 1.0f) < 0.001f);
    // 只对一个空：给部分分
    const quiz::Score half = quiz::scoreFill(question, {"H2O", "200"});
    CHECK(!half.full);
    CHECK(std::abs(half.value - 0.5f) < 0.001f);
    // 全错
    const quiz::Score none = quiz::scoreFill(question, {"CO2", "200"});
    CHECK(std::abs(none.value - 0.0f) < 0.001f);

    // blanks 写少了：以答案为准并提示
    const quiz::io::LoadResult mismatch = quiz::io::loadBank(
        "[meta]\nid = m2\ntitle = m2\n"
        "[q]\ntype = fill\nblanks = 3\nstem = 三个空____\nanswer = 甲 ; 乙\n",
        "m2.quiz");
    CHECK_EQ(mismatch.bank.questions[0].fillBlankCount(), 3);   // 补上缺的那个空
    bool reported = false;
    for (const auto& issue : mismatch.issues) {
        if (issue.message.find("第 3 个空没有答案") != std::string::npos) {
            reported = true;
        }
    }
    CHECK(reported);
    CHECK(mismatch.hasFatalError);   // 有空没答案 → 这份题库不能用
}

void testIndeterminateQuestion() {
    std::printf("不定项（少选给部分分）\n");
    quiz::Question question;
    question.type = quiz::QuestionType::Indeterminate;
    question.options = {"甲", "乙", "丙"};
    question.answer = {0, 1};

    const quiz::Score full = quiz::scoreChoice(question, {0, 1});
    CHECK(full.full);
    CHECK(std::abs(full.value - 1.0f) < 0.001f);

    const quiz::Score half = quiz::scoreChoice(question, {1});
    CHECK(!half.full);
    CHECK(std::abs(half.value - 0.5f) < 0.001f);

    const quiz::Score zero = quiz::scoreChoice(question, {0, 2});   // 选到了错的
    CHECK(!zero.full);
    CHECK(std::abs(zero.value - 0.0f) < 0.001f);
}

void testOrderedQuestion() {
    std::printf("多项有序（七选五）\n");
    const std::string text =
        "[meta]\nid = ordered\ntitle = ordered\n"
        "[q]\ntype = ordered\nstem = 七选五\n"
        "option = 甲\noption = 乙\noption = 丙\noption = 丁\n"
        "blank = 空 1\nblank = 空 2\nblank = 空 3\n"
        "answer = C,A,D\n";
    const quiz::io::LoadResult result = quiz::io::loadBank(text, "ordered.quiz");
    CHECK(result.ok());
    if (!result.ok()) {
        return;
    }
    const quiz::Question& question = result.bank.questions[0];
    CHECK_EQ(question.type, quiz::QuestionType::Ordered);
    CHECK_EQ(question.slotCount(), 3);
    // 顺序必须保住：C,A,D 不能被排序成 A,C,D
    CHECK_EQ(question.answer.size(), size_t(3));
    CHECK_EQ(question.answer[0], 2);
    CHECK_EQ(question.answer[1], 0);
    CHECK_EQ(question.answer[2], 3);

    const quiz::Score allRight = quiz::scoreOrdered(question, {2, 0, 3});
    CHECK(allRight.full);
    const quiz::Score twoOfThree = quiz::scoreOrdered(question, {2, 1, 3});
    CHECK(!twoOfThree.full);
    CHECK(std::abs(twoOfThree.value - 2.0f / 3.0f) < 0.001f);
}

void testMaterialQuestion() {
    std::printf("材料题（可以含任意题型）\n");

    // 一段材料下面挂六种题型，全部都要能正常解析
    const std::string text =
        "[meta]\nid = mat\ntitle = mat\n"
        "[material]\nid = m1\ntitle = 短文\ntext = 第一行\\n第二行\n"
        "[q]\ntype = single\nmaterial = m1\nstem = 小题一\noption = a\noption = b\nanswer = A\n"
        "[q]\ntype = multiple\nmaterial = m1\nstem = 小题二\noption = a\noption = b\noption = c\nanswer = A,B\n"
        "[q]\ntype = judge\nmaterial = m1\nstem = 小题三\nanswer = F\n"
        "[q]\ntype = fill\nmaterial = m1\nstem = 小题四\nanswer = H2O|水\n"
        "[q]\ntype = indeterminate\nmaterial = m1\nstem = 小题五\n"
        "option = a\noption = b\noption = c\nanswer = A,B\n"
        "[q]\ntype = ordered\nmaterial = m1\nstem = 小题六\n"
        "option = a\noption = b\noption = c\nblank = 空 1\nblank = 空 2\nanswer = C,A\n";
    const quiz::io::LoadResult result = quiz::io::loadBank(text, "material.quiz");
    CHECK(result.ok());
    if (!result.ok()) {
        return;
    }
    CHECK_EQ(result.bank.materials.size(), size_t(1));
    const quiz::QuestionType expected[] = {
        quiz::QuestionType::Single,        quiz::QuestionType::Multiple,
        quiz::QuestionType::Judge,         quiz::QuestionType::Fill,
        quiz::QuestionType::Indeterminate, quiz::QuestionType::Ordered,
    };
    CHECK_EQ(result.bank.questions.size(), size_t(6));
    for (size_t i = 0; i < result.bank.questions.size(); ++i) {
        const quiz::Question& question = result.bank.questions[i];
        CHECK_EQ(question.type, expected[i]);
        CHECK_EQ(question.materialId, std::string("m1"));
        CHECK_EQ(question.materialPart, static_cast<int>(i) + 1);
        CHECK_EQ(question.materialCount, 6);
    }
    CHECK_EQ(result.bank.questions[3].fillBlankCount(), 1);              // 一个空
    CHECK_EQ(result.bank.questions[3].fillAnswers[0].size(), size_t(2)); // 两个可接受答案
    CHECK_EQ(result.bank.questions[5].answer.size(), size_t(2));        // 有序题两个空
    CHECK_EQ(result.bank.questions[5].answer[0], 2);                    // 顺序没被排序

    const quiz::Material* material = result.bank.materialById("m1");
    CHECK(material != nullptr);
    if (material != nullptr) {
        CHECK_EQ(material->title, std::string("短文"));
        CHECK(material->text.find("第二行") != std::string::npos);   // \n 被还原成换行
    }

    // 只写 type = 材料题（不写具体题型）时，按数据自己判断题型
    const quiz::io::LoadResult autoTypes = quiz::io::loadBank(
        "[meta]\nid = auto\ntitle = auto\n"
        "[material]\nid = m1\ntext = 材料\n"
        "[q]\ntype = 材料题\nmaterial = m1\nstem = a\noption = 甲\noption = 乙\nanswer = A\n"
        "[q]\ntype = 材料题\nmaterial = m1\nstem = b\n"
        "option = 甲\noption = 乙\noption = 丙\nanswer = A,C\n"
        "[q]\ntype = 材料题\nmaterial = m1\nstem = c\nanswer = T\n"
        "[q]\ntype = 材料题\nmaterial = m1\nstem = d\nanswer = 李白\n",
        "auto.quiz");
    CHECK(autoTypes.ok());
    if (autoTypes.ok()) {
        CHECK_EQ(autoTypes.bank.questions.size(), size_t(4));
        CHECK_EQ(autoTypes.bank.questions[0].type, quiz::QuestionType::Single);
        CHECK_EQ(autoTypes.bank.questions[1].type, quiz::QuestionType::Multiple);
        CHECK_EQ(autoTypes.bank.questions[2].type, quiz::QuestionType::Judge);
        CHECK_EQ(autoTypes.bank.questions[3].type, quiz::QuestionType::Fill);
        CHECK(quiz::fillBlankMatches(autoTypes.bank.questions[3], 0, "李白"));
    }

    // 引用了不存在的材料要报出来
    const quiz::io::LoadResult bad = quiz::io::loadBank(
        "[meta]\nid = bad\ntitle = bad\n[q]\ntype = single\nmaterial = 没有\nstem = s\n"
        "option = a\noption = b\nanswer = A\n",
        "bad.quiz");
    bool reported = false;
    for (const auto& issue : bad.issues) {
        if (issue.message.find("不存在的材料") != std::string::npos) {
            reported = true;
        }
    }
    CHECK(reported);
}

void testMaterialBreak() {
    std::printf("[break] 材料分组\n");
    const std::string text =
        "[meta]\nid = brk\ntitle = brk\n"
        "[material]\nid = m1\ntext = 材料一\n"
        "[q]\ntype = single\nstem = 一之一\noption = a\noption = b\nanswer = A\n"
        "[q]\ntype = judge\nstem = 一之二\nanswer = T\n"
        "[break]\n"
        "[q]\ntype = single\nstem = 材料外\noption = a\noption = b\nanswer = B\n"
        "[material]\nid = m2\ntext = 材料二\n"
        "[q]\ntype = fill\nstem = 二之一\nanswer = 甲\n";
    const quiz::io::LoadResult result = quiz::io::loadBank(text, "brk.quiz");
    CHECK(result.ok());
    if (!result.ok()) {
        return;
    }
    CHECK_EQ(result.bank.materials.size(), size_t(2));
    CHECK_EQ(result.bank.questions.size(), size_t(4));
    // [material] … [break] 之间的小题自动挂上去
    CHECK_EQ(result.bank.questions[0].materialId, std::string("m1"));
    CHECK_EQ(result.bank.questions[1].materialId, std::string("m1"));
    CHECK_EQ(result.bank.questions[1].materialPart, 2);
    CHECK_EQ(result.bank.questions[1].materialCount, 2);
    // [break] 之后就不属于那段材料了
    CHECK_EQ(result.bank.questions[2].materialId, std::string(""));
    CHECK_EQ(result.bank.questions[2].materialPart, 0);
    // 第二段材料换个组，重新从第 1 小题开始
    CHECK_EQ(result.bank.questions[3].materialId, std::string("m2"));
    CHECK_EQ(result.bank.questions[3].materialPart, 1);
    CHECK_EQ(result.bank.questions[3].materialCount, 1);
}

void testPaperDrafts() {
    std::printf("材料题卷面：一页里各题各自的草稿\n");
    const std::string text =
        "[meta]\nid = paper\ntitle = paper\n"
        "[material]\nid = m1\ntext = 材料\n"
        "[q]\ntype = single\nstem = a\noption = 甲\noption = 乙\nanswer = B\n"
        "[q]\ntype = multiple\nstem = b\noption = 甲\noption = 乙\noption = 丙\nanswer = A,C\n"
        "[q]\ntype = fill\nstem = c\nanswer = 甲\n"
        "[break]\n"
        "[q]\ntype = single\nstem = d\noption = 甲\noption = 乙\nanswer = A\n";
    quiz::io::LoadResult result = quiz::io::loadBank(text, "paper.quiz");
    CHECK(result.ok());
    if (!result.ok()) {
        return;
    }
    quiz::BankProgress progress;
    quiz::QuizSession session;
    session.bind(&result.bank, &progress);

    // 前 3 题属于同一段材料，第 4 题（下标 3）在 [break] 之后
    CHECK_EQ(session.materialGroupOf(0).size(), size_t(3));
    CHECK(session.materialGroupOf(3).empty());
    CHECK_EQ(session.nextGroupIndex(0), 3);
    CHECK_EQ(session.prevGroupIndex(3), 0);

    // 同一页里三题各填各的，互不影响
    session.selectOptionAt(0, 1);   // 第 1 题选 B（正确）
    session.selectOptionAt(1, 0);   // 第 2 题先选 A
    session.selectOptionAt(1, 2);   // 再加 C（正确）
    session.fillTextAt(2, 0) = "甲";   // 第 3 题填空
    CHECK_EQ(session.selectionAt(0).size(), size_t(1));
    CHECK_EQ(session.selectionAt(1).size(), size_t(2));
    CHECK_EQ(session.fillTextAt(2, 0), std::string("甲"));
    CHECK(session.hasAnswerInputAt(0));
    CHECK(session.hasAnswerInputAt(1));
    CHECK(session.hasAnswerInputAt(2));
    CHECK(!session.hasAnswerInputAt(3));

    // 交掉哪题，哪题才写进进度
    session.submitAt(0, "t");
    CHECK(session.isSubmittedAt(0));
    CHECK(session.isCorrectAt(0));
    CHECK(!session.isSubmittedAt(1));
    CHECK_EQ(progress.answeredCount(), 1);

    session.submitAt(1, "t");
    session.submitAt(2, "t");
    CHECK(session.isCorrectAt(1));
    CHECK(session.isCorrectAt(2));
    CHECK_EQ(progress.answeredCount(), 3);
    CHECK_EQ(progress.correctCount(), 3);

    // 交过的题不能再改
    session.selectOptionAt(0, 0);
    CHECK_EQ(session.selectionAt(0).size(), size_t(1));
    CHECK_EQ(session.selectionAt(0)[0], 1);

    // 卷面外的题照旧用「当前题」那套接口
    session.jumpTo(3);
    CHECK_EQ(session.currentIndex(), 3);
    CHECK(!session.hasAnswerInput());
    session.selectOption(0);
    session.submit("t");
    CHECK(session.isCorrect());
    CHECK_EQ(progress.correctCount(), 4);

    // 整页提交：把这一页里「已填的」题一起交掉，没填的保持未答
    quiz::QuizSession page;
    quiz::BankProgress pageProgress;
    page.bind(&result.bank, &pageProgress);
    page.selectOptionAt(0, 1);
    page.fillTextAt(2, 0) = "甲";
    CHECK_EQ(page.submitPage({0, 1, 2}, "t"), 2);   // 下标 1 那道还没填
    CHECK(page.isSubmittedAt(0));
    CHECK(!page.isSubmittedAt(1));
    CHECK(page.isSubmittedAt(2));
    CHECK_EQ(pageProgress.answeredCount(), 2);

    // 再填上剩下的那道，第二次「提交本页」只交它
    page.selectOptionAt(1, 0);
    page.selectOptionAt(1, 2);
    CHECK_EQ(page.submitPage({0, 1, 2}, "t"), 1);
    CHECK_EQ(pageProgress.answeredCount(), 3);
    CHECK_EQ(pageProgress.correctCount(), 3);
}

void testNewTypesRoundTrip() {
    std::printf("新题型导出与再解析\n");
    const std::string text =
        "[meta]\nid = types\ntitle = types\n"
        "[material]\nid = m1\ntitle = 材料\ntext = 正文\n"
        "[q]\ntype = fill\nstem = 填空\nanswer = H2O|水\n"
        "[q]\ntype = indeterminate\nstem = 不定项\noption = a\noption = b\noption = c\nanswer = A,B\n"
        "[q]\ntype = ordered\nstem = 有序\noption = a\noption = b\noption = c\n"
        "blank = 空 1\nblank = 空 2\nanswer = C,A\n"
        "[q]\ntype = single\nmaterial = m1\nstem = 材料小题\noption = a\noption = b\nanswer = B\n";
    const quiz::io::LoadResult original = quiz::io::loadBank(text, "types.quiz");
    CHECK(original.ok());
    if (!original.ok()) {
        return;
    }
    const std::string exported = quiz::io::saveBank(original.bank);
    const quiz::io::LoadResult reloaded = quiz::io::loadBank(exported, "exported.quiz");
    CHECK(reloaded.ok());
    if (!reloaded.ok()) {
        return;
    }
    CHECK_EQ(reloaded.bank.questions.size(), original.bank.questions.size());
    CHECK_EQ(reloaded.bank.materials.size(), original.bank.materials.size());
    CHECK_EQ(reloaded.bank.questions[0].fillAnswers, original.bank.questions[0].fillAnswers);
    CHECK_EQ(reloaded.bank.questions[0].type, quiz::QuestionType::Fill);
    CHECK_EQ(reloaded.bank.questions[1].type, quiz::QuestionType::Indeterminate);
    CHECK_EQ(reloaded.bank.questions[2].type, quiz::QuestionType::Ordered);
    CHECK_EQ(reloaded.bank.questions[2].answer, original.bank.questions[2].answer);   // 顺序保住
    CHECK_EQ(reloaded.bank.questions[3].materialId, std::string("m1"));
}

void testProgressWithPartialScore() {
    std::printf("部分分与填空答案的进度读写\n");
    quiz::BankProgress progress;
    progress.bankId = "partial";
    progress.questions.resize(2);
    progress.questions[0].answered = true;
    progress.questions[0].correct = false;
    progress.questions[0].score = 0.5f;
    progress.questions[0].wrongCount = 1;
    progress.questions[1].answered = true;
    progress.questions[1].correct = true;
    progress.questions[1].score = 1.0f;
    progress.questions[1].rightCount = 3;

    const std::string text = quiz::progress::serialize(progress);
    const quiz::BankProgress restored = quiz::progress::deserialize(text, "partial", 2);
    CHECK_EQ(restored.questions.size(), size_t(2));
    CHECK(std::abs(restored.questions[0].score - 0.5f) < 0.001f);
    CHECK(restored.questions[0].partiallyCorrect());
    CHECK_EQ(restored.questions[0].wrongCount, 1);
    CHECK_EQ(restored.questions[1].rightCount, 3);

    // 用户自己加的标签也要能存能读
    quiz::BankProgress tagged;
    tagged.bankId = "tagged";
    tagged.questions.resize(1);
    tagged.questions[0].tags = {"易错", "计算"};
    const quiz::BankProgress taggedBack = quiz::progress::deserialize(
        quiz::progress::serialize(tagged), "tagged", 1);
    CHECK_EQ(taggedBack.questions.size(), size_t(1));
    if (taggedBack.questions[0].tags.size() == 2) {
        CHECK_EQ(taggedBack.questions[0].tags[0], std::string("易错"));
        CHECK_EQ(taggedBack.questions[0].tags[1], std::string("计算"));
    } else {
        CHECK(false);   // 数量不对就直接判失败，别去访问下标
    }
    CHECK(std::abs(restored.questions[1].score - 1.0f) < 0.001f);
}

// ---------------------------------------------------------------------------
// SVG 光栅化：图标必须按目标尺寸画满、居中（曾经的 bug 是只有 1/3 大小、还贴在左上角）
// ---------------------------------------------------------------------------
void inkBounds(const quiz::render::svg::Image& image, int& outMinX, int& outMinY, int& outMaxX,
               int& outMaxY) {
    outMinX = image.width;
    outMinY = image.height;
    outMaxX = -1;
    outMaxY = -1;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const uint32_t pixel = image.pixels[static_cast<size_t>(y) * image.width + x];
            if ((pixel >> 24) < 16) {   // 太透明的当背景
                continue;
            }
            outMinX = std::min(outMinX, x);
            outMinY = std::min(outMinY, y);
            outMaxX = std::max(outMaxX, x);
            outMaxY = std::max(outMaxY, y);
        }
    }
}

void testMathTextCompatibility() {
    std::printf("数学公式兼容层\n");

    // 第一层：常见上下标、希腊字母、运算符、分式和根号。
    CHECK_EQ(quiz::math::latexToUnicode("H_2O"), std::string("H₂O"));
    CHECK_EQ(quiz::math::latexToUnicode("x^2 + y^2 = r^2"), std::string("x² + y² = r²"));
    CHECK_EQ(quiz::math::latexToUnicode("\\alpha+\\beta \\le \\pi"),
             std::string("α+β ≤ π"));
    CHECK_EQ(quiz::math::latexToUnicode("\\frac{1}{2}"), std::string("1/2"));
    CHECK_EQ(quiz::math::latexToUnicode("\\sqrt{a+b}"), std::string("√(a+b)"));
    CHECK_EQ(quiz::math::latexToUnicode("10^{-3}"), std::string("10⁻³"));
    CHECK_EQ(quiz::math::latexToUnicode("x^{n+1}"), std::string("xⁿ⁺¹"));
    CHECK_EQ(quiz::math::latexToUnicode("\\unknown{x}"), std::string("\\unknown{x}"));
    CHECK_EQ(quiz::math::latexToUnicode("x^a"), std::string("x^a"));   // 无法映射时完整回退

    // 第二层：行内、独立公式和普通文字被正确分段，原始公式同时保留。
    const std::vector<quiz::math::MathSegment> mixed =
        quiz::math::splitFormulaSegments("速度 $v=\\frac{s}{t}$；$$a^2+b^2=c^2$$ 完成");
    CHECK_EQ(mixed.size(), size_t(5));
    if (mixed.size() == 5) {
        CHECK(mixed[0].kind == quiz::math::MathSegmentKind::Text);
        CHECK_EQ(mixed[0].text, std::string("速度 "));
        CHECK(mixed[1].kind == quiz::math::MathSegmentKind::InlineFormula);
        CHECK_EQ(mixed[1].source, std::string("v=\\frac{s}{t}"));
        CHECK_EQ(mixed[1].text, std::string("v=s/t"));
        CHECK(mixed[2].kind == quiz::math::MathSegmentKind::Text);
        CHECK(mixed[3].kind == quiz::math::MathSegmentKind::DisplayFormula);
        CHECK_EQ(mixed[3].text, std::string("a²+b²=c²"));
        CHECK(mixed[4].kind == quiz::math::MathSegmentKind::Text);
        CHECK_EQ(mixed[4].text, std::string(" 完成"));
    }

    // 未闭合美元符号和转义美元符号按普通文字显示，不能吞掉后文。
    const std::vector<quiz::math::MathSegment> unclosed =
        quiz::math::splitFormulaSegments("未闭合 $x^2");
    CHECK_EQ(unclosed.size(), size_t(1));
    if (!unclosed.empty()) {
        CHECK(unclosed[0].kind == quiz::math::MathSegmentKind::Text);
        CHECK_EQ(unclosed[0].text, std::string("未闭合 $x^2"));
    }
    CHECK_EQ(quiz::math::toDisplayText("价格 \\$5，公式 $x^2$"),
             std::string("价格 $5，公式 x²"));
    CHECK_EQ(quiz::math::toDisplayText("前 $$x_1$$ 后"), std::string("前 \nx₁\n 后"));
}

void testSvgRasterize() {
    const std::string full =
        "<svg viewBox=\"0 0 24 24\"><rect x=\"0\" y=\"0\" width=\"24\" height=\"24\" "
        "fill=\"currentColor\"/></svg>";
    const std::string half =
        "<svg viewBox=\"0 0 24 24\"><rect x=\"6\" y=\"6\" width=\"12\" height=\"12\" "
        "fill=\"currentColor\"/></svg>";
    const std::string chevron =
        "<svg viewBox=\"0 0 24 24\"><path d=\"M15 5 L8 12 L15 19\" fill=\"none\" "
        "stroke=\"currentColor\" stroke-width=\"2\"/></svg>";

    quiz::render::svg::Image image;
    std::string error;
    int minX = 0;
    int minY = 0;
    int maxX = 0;
    int maxY = 0;

    // 占满 viewBox 的方框：墨迹应当铺满整张图，而且居中
    CHECK(quiz::render::svg::rasterize(full, 64, 0xFFFFFF, image, error));
    CHECK_EQ(image.width, 64);
    inkBounds(image, minX, minY, maxX, maxY);
    CHECK(maxX - minX + 1 >= 60);
    CHECK(maxY - minY + 1 >= 60);
    CHECK(minX <= 3);
    CHECK(minY <= 3);

    // 只占半张画布的方框：原样渲染时墨迹就是一半
    CHECK(quiz::render::svg::rasterize(half, 64, 0xFFFFFF, image, error));
    inkBounds(image, minX, minY, maxX, maxY);
    CHECK(std::abs((maxX - minX + 1) - 32) <= 2);

    // 归一化：不管原图留白多少，最长边都到 size * inkFraction，而且居中
    CHECK(quiz::render::svg::rasterizeFitted(half, 64, 0.75f, 0xFFFFFF, image, error));
    inkBounds(image, minX, minY, maxX, maxY);
    CHECK(std::abs(std::max(maxX - minX + 1, maxY - minY + 1) - 48) <= 2);
    CHECK(std::abs((minX + maxX + 1) - 64) <= 3);
    CHECK(std::abs((minY + maxY + 1) - 64) <= 3);

    // 细长的图标（chevron）归一化后，最长边也要到目标大小
    CHECK(quiz::render::svg::rasterizeFitted(chevron, 64, 0.75f, 0xFFFFFF, image, error));
    inkBounds(image, minX, minY, maxX, maxY);
    CHECK(std::abs((maxY - minY + 1) - 48) <= 3);

    // 颜色不能串道：青色 0x00B4D8 出来还得是青色（曾经 R/B 互换，青的会变成金色）
    const std::string cyanRect =
        "<svg viewBox=\"0 0 24 24\"><rect x=\"4\" y=\"4\" width=\"16\" height=\"16\" "
        "fill=\"#00B4D8\"/></svg>";
    CHECK(quiz::render::svg::rasterize(cyanRect, 32, 0xFFFFFF, image, error));
    const uint32_t center = image.pixels[static_cast<size_t>(16) * 32 + 16];
    CHECK_EQ((center >> 24) & 0xFFu, uint32_t(255));
    CHECK_EQ(center & 0xFFu, uint32_t(0x00));           // R
    CHECK_EQ((center >> 8) & 0xFFu, uint32_t(0xB4));    // G
    CHECK_EQ((center >> 16) & 0xFFu, uint32_t(0xD8));   // B

    // currentColor 也要按 0xRRGGBB 解释（界面里图标颜色就是走这条路传进来的）
    const std::string currentColorRect =
        "<svg viewBox=\"0 0 24 24\"><rect x=\"4\" y=\"4\" width=\"16\" height=\"16\" "
        "fill=\"currentColor\"/></svg>";
    CHECK(quiz::render::svg::rasterize(currentColorRect, 32, 0x00B4D8, image, error));
    const uint32_t currentColorPixel = image.pixels[static_cast<size_t>(16) * 32 + 16];
    CHECK_EQ(currentColorPixel & 0xFFu, uint32_t(0x00));
    CHECK_EQ((currentColorPixel >> 16) & 0xFFu, uint32_t(0xD8));
}

// ---------------------------------------------------------------------------
// 考试模式：卷声明解析（[exam] / [draw] / [q] score）+ 导出往返
// ---------------------------------------------------------------------------
const char* kExamBank =
    "[meta]\n"
    "id = exam-bank\n"
    "title = 考试题库\n"
    "\n"
    "[q]\n"
    "id = q1\n"
    "type = single\n"
    "stem = 1+1=?\n"
    "option = 2\n"
    "option = 3\n"
    "answer = A\n"
    "tags = 基础\n"
    "difficulty = 1\n"
    "score = 2\n"
    "\n"
    "[q]\n"
    "id = q2\n"
    "type = multiple\n"
    "stem = 选偶数\n"
    "option = 1\n"
    "option = 2\n"
    "option = 4\n"
    "answer = B,C\n"
    "tags = 进阶\n"
    "difficulty = 4\n"
    "\n"
    "[q]\n"
    "id = q3\n"
    "type = judge\n"
    "stem = 天空是蓝色的\n"
    "answer = 对\n"
    "difficulty = 1\n"
    "\n"
    "[material]\n"
    "id = m1\n"
    "title = 材料一\n"
    "text = 材料正文\n"
    "\n"
    "[q]\n"
    "id = q4\n"
    "type = single\n"
    "stem = 材料第一问\n"
    "option = 甲\n"
    "option = 乙\n"
    "answer = A\n"
    "difficulty = 3\n"
    "\n"
    "[q]\n"
    "id = q5\n"
    "type = multiple\n"
    "stem = 材料第二问\n"
    "option = 甲\n"
    "option = 乙\n"
    "option = 丙\n"
    "answer = A,B\n"
    "difficulty = 5\n"
    "\n"
    "[break]\n"
    "\n"
    "[exam]\n"
    "id = mock-a\n"
    "title = 模拟卷 A\n"
    "minutes = 60\n"
    "pass = 60\n"
    "seed = 7\n"
    "\n"
    "[draw]\n"
    "count = 2\n"
    "type = multiple\n"
    "difficulty = 4\n"
    "score = 5\n"
    "\n"
    "[draw]\n"
    "count = 1\n"
    "id = q1\n"
    "\n"
    "[draw]\n"
    "count = 1\n"
    "type = judge\n"
    "\n";

void testExamFormat() {
    std::printf("考试：卷声明（[exam] / [draw] / score）\n");
    const quiz::io::LoadResult result = quiz::io::loadBank(kExamBank, "exam-bank");
    CHECK(result.ok());
    const quiz::QuestionBank& bank = result.bank;
    CHECK_EQ(bank.questions.size(), size_t(5));
    CHECK_EQ(bank.questions[0].score, 2);   // [q] score = 2
    CHECK_EQ(bank.questions[1].score, 1);   // 没写就是 1 分
    CHECK_EQ(bank.exams.size(), size_t(1));
    if (bank.exams.empty()) {
        return;
    }
    const quiz::ExamSpec& exam = bank.exams.front();
    CHECK_EQ(exam.id, std::string("mock-a"));
    CHECK_EQ(exam.title, std::string("模拟卷 A"));
    CHECK_EQ(exam.minutes, 60);
    CHECK_EQ(exam.passPercent, 60);
    CHECK_EQ(exam.seed, 7);
    CHECK(!exam.shuffle);
    CHECK_EQ(exam.rules.size(), size_t(3));
    if (exam.rules.size() != 3) {
        return;
    }
    CHECK_EQ(exam.rules[0].count, 2);
    CHECK_EQ(exam.rules[0].types.size(), size_t(1));
    if (!exam.rules[0].types.empty()) {
        CHECK_EQ(exam.rules[0].types[0], quiz::QuestionType::Multiple);
    }
    CHECK_EQ(exam.rules[0].difficultyMin, 4);
    CHECK_EQ(exam.rules[0].difficultyMax, 4);
    CHECK_EQ(exam.rules[0].score, 5);
    CHECK_EQ(exam.rules[1].ids.size(), size_t(1));
    if (!exam.rules[1].ids.empty()) {
        CHECK_EQ(exam.rules[1].ids[0], std::string("q1"));
    }
    CHECK(quiz::io::findExam(bank, "mock-a") != nullptr);
    CHECK(quiz::io::findExam(bank, "没有这套卷") == nullptr);

    // 难度区间 / 材料规则 / 排除标签 / 分值 都从文件里读回来了
    const std::string rangeBank =
        "[meta]\nid = range-bank\ntitle = 区间卷\n\n"
        "[q]\nid = r1\ntype = single\nstem = 题\noption = 甲\noption = 乙\nanswer = A\n\n"
        "[exam]\nid = range-paper\ntitle = 区间卷 A\n\n"
        "[draw]\n"
        "count = 3\n"
        "type = single\n"
        "difficulty = 2-5\n"
        "tags = 第一章,第二章\n"
        "exclude_tags = 送分题\n"
        "material = any\n"
        "score = 3\n";
    const quiz::io::LoadResult range = quiz::io::loadBank(rangeBank, "range-bank");
    CHECK(range.ok());
    if (range.ok() && !range.bank.exams.empty() && !range.bank.exams.front().rules.empty()) {
        const quiz::DrawRule& rule = range.bank.exams.front().rules.front();
        CHECK_EQ(rule.count, 3);
        CHECK_EQ(rule.difficultyMin, 2);
        CHECK_EQ(rule.difficultyMax, 5);
        CHECK_EQ(rule.tags.size(), size_t(2));
        CHECK_EQ(rule.excludeTags.size(), size_t(1));
        CHECK(rule.materialOnly);
        CHECK_EQ(rule.score, 3);
    }

    // 导出再读回来：卷声明与分值都要在（考试能力不能被导出弄丢）
    const quiz::io::LoadResult again = quiz::io::loadBank(quiz::io::saveBank(bank), "round-trip");
    CHECK(again.ok());
    CHECK_EQ(again.bank.questions[0].score, 2);
    CHECK_EQ(again.bank.exams.size(), size_t(1));
    if (!again.bank.exams.empty()) {
        const quiz::ExamSpec& copy = again.bank.exams.front();
        CHECK_EQ(copy.id, std::string("mock-a"));
        CHECK_EQ(copy.minutes, 60);
        CHECK_EQ(copy.passPercent, 60);
        CHECK_EQ(copy.seed, 7);
        CHECK_EQ(copy.rules.size(), size_t(3));
        if (copy.rules.size() == 3) {
            CHECK_EQ(copy.rules[0].count, 2);
            CHECK_EQ(copy.rules[0].score, 5);
            CHECK_EQ(copy.rules[0].difficultyMin, 4);
            CHECK_EQ(copy.rules[1].ids.size(), size_t(1));
            CHECK_EQ(copy.rules[2].types.size(), size_t(1));
        }
    }

    // 写歪的卷声明只报 issue，不影响题库本身可用
    const std::string broken =
        "[meta]\nid = broken\ntitle = 坏卷\n\n"
        "[q]\nid = q1\ntype = judge\nstem = 对错?\nanswer = T\n\n"
        "[draw]\ncount = 1\n"
        "[exam]\ntitle = 半套卷\npass = 200\nminutes = -5\n"
        "[draw]\ncount = 1\ntype = 不存在的题型\n";
    const quiz::io::LoadResult bad = quiz::io::loadBank(broken, "broken");
    CHECK(bad.ok());   // 卷有问题不算致命错误：题库照样能用
    CHECK(!bad.issues.empty());
    CHECK_EQ(bad.bank.exams.size(), size_t(1));
    if (!bad.bank.exams.empty()) {
        CHECK_EQ(bad.bank.exams.front().id, std::string("exam1"));   // 没写 id → 自动补
        CHECK_EQ(bad.bank.exams.front().passPercent, -1);            // 200 不合法 → 不判及格
        CHECK_EQ(bad.bank.exams.front().minutes, 0);
    }
}

// ---------------------------------------------------------------------------
// 考试模式：抽题（种子可复现 / 跨规则不重复 / 材料整段 / 抽不满报 issue / 难度排序）
// ---------------------------------------------------------------------------
void testExamPaper() {
    std::printf("考试：抽题（种子 / 去重 / 材料整段 / 难度排序）\n");
    const quiz::io::LoadResult loaded = quiz::io::loadBank(kExamBank, "exam-bank");
    CHECK(loaded.ok());
    if (!loaded.ok() || loaded.bank.exams.empty()) {
        return;
    }
    const quiz::ExamSpec& exam = loaded.bank.exams.front();
    const quiz::io::ExamPaper paper = quiz::io::buildExamPaper(loaded.bank, exam, 7);

    // 规则 1 要 2 道难度 4 的多选：只有 q2 命中 → 抽不满（报 issue，但照样成卷）
    CHECK_EQ(paper.items.size(), size_t(3));
    CHECK_EQ(paper.issues.size(), size_t(1));
    CHECK_EQ(paper.title, std::string("模拟卷 A"));
    CHECK_EQ(paper.minutes, 60);
    // 卷面顺序：按难度由易到难（q1=1、q3=1、q2=4）
    if (paper.items.size() == 3) {
        CHECK_EQ(paper.items[0].questionIndex, 0);   // q1
        CHECK_EQ(paper.items[1].questionIndex, 2);   // q3
        CHECK_EQ(paper.items[2].questionIndex, 1);   // q2
        CHECK_EQ(paper.items[0].score, 2);           // q1 自带 2 分
        CHECK_EQ(paper.items[1].score, 1);           // 规则没写 score → 用题目自带的 1 分
        CHECK_EQ(paper.items[2].score, 5);           // 规则里的 score 覆盖题目自带的
    }
    CHECK_EQ(paper.totalScore(), 8);

    // 同一个种子 → 同一套题（模拟卷要可复现）
    const quiz::io::ExamPaper again = quiz::io::buildExamPaper(loaded.bank, exam, 7);
    CHECK_EQ(again.items.size(), paper.items.size());
    for (size_t i = 0; i < again.items.size() && i < paper.items.size(); ++i) {
        CHECK_EQ(again.items[i].questionIndex, paper.items[i].questionIndex);
    }

    // 点名抽到一道材料小题 → 整段材料一起进卷（按 materialPart 排）
    const std::string materialBank =
        "[meta]\nid = m-bank\ntitle = 材料卷\n\n"
        "[q]\nid = n1\ntype = judge\nstem = 独题\nanswer = T\ndifficulty = 2\n\n"
        "[material]\nid = m1\ntext = 材料\n\n"
        "[q]\nid = n2\ntype = single\nstem = 问一\noption = 甲\noption = 乙\nanswer = A\ndifficulty = 3\n\n"
        "[q]\nid = n3\ntype = single\nstem = 问二\noption = 甲\noption = 乙\nanswer = B\ndifficulty = 4\n\n"
        "[break]\n\n"
        "[exam]\nid = m-paper\ntitle = 材料卷 A\nseed = 3\n\n"
        "[draw]\ncount = 1\nid = n2\n";
    const quiz::io::LoadResult material = quiz::io::loadBank(materialBank, "m-bank");
    CHECK(material.ok());
    if (material.ok() && !material.bank.exams.empty()) {
        const quiz::io::ExamPaper items =
            quiz::io::buildExamPaper(material.bank, material.bank.exams.front(), 3);
        CHECK_EQ(items.items.size(), size_t(2));   // 点了一道小题 → 整段 2 道都进
        if (items.items.size() == 2) {
            CHECK_EQ(items.items[0].questionIndex, 1);   // n2
            CHECK_EQ(items.items[1].questionIndex, 2);   // n3
        }
    }

    // type = material：count 数的是「几段材料」（跟里面有几道小题无关），
    // 写了 score 就要求整段材料的总分正好等于它（对不上不进候选）
    const std::string mrHead =
        "[meta]\nid = mr\ntitle = 材料规则\n\n"
        "[q]\nid = a1\ntype = judge\nstem = 独题\nanswer = T\ndifficulty = 2\n\n"
        "[material]\nid = m1\ntext = 材料一\n\n"
        "[q]\nid = a2\ntype = single\nstem = 问一\noption = 甲\noption = 乙\nanswer = A\ndifficulty = 3\nscore = 1\n\n"
        "[q]\nid = a3\ntype = single\nstem = 问二\noption = 甲\noption = 乙\nanswer = A\ndifficulty = 3\nscore = 3\n\n"
        "[break]\n\n"
        "[exam]\nid = mr-paper\ntitle = 材料规则卷\nseed = 5\n\n";

    // 整段总分 = 1 + 3 = 4，正好等于 score → 整段进卷（一道小题一道小题地进，分值用题目自带的）
    const std::string mrMatch = mrHead + "[draw]\ncount = 1\ntype = material\nscore = 4\n";
    const quiz::io::LoadResult mrOk = quiz::io::loadBank(mrMatch, "mr");
    CHECK(mrOk.ok());
    if (mrOk.ok() && !mrOk.bank.exams.empty()) {
        const quiz::io::ExamPaper items =
            quiz::io::buildExamPaper(mrOk.bank, mrOk.bank.exams.front(), 5);
        CHECK_EQ(items.items.size(), size_t(2));   // 1 段材料 = 2 道小题（count 说的是「段」）
        CHECK_EQ(items.totalScore(), 4);           // 整段就是把 score 拿满
        CHECK_EQ(items.issues.size(), size_t(0));
        if (items.items.size() == 2) {
            CHECK_EQ(items.items[0].questionIndex, 1);
            CHECK_EQ(items.items[1].questionIndex, 2);
            CHECK_EQ(items.items[0].score, 1);     // 材料段用题目自带的分值
            CHECK_EQ(items.items[1].score, 3);
        }
    }

    // 总分对不上（这里 4 ≠ 5）→ 一段都抽不到，并给一条能照着改的提示
    const std::string mrBad = mrHead + "[draw]\ncount = 1\ntype = material\nscore = 5\n";
    const quiz::io::LoadResult mrMismatch = quiz::io::loadBank(mrBad, "mr");
    CHECK(mrMismatch.ok());
    if (mrMismatch.ok() && !mrMismatch.bank.exams.empty()) {
        const quiz::io::ExamPaper items =
            quiz::io::buildExamPaper(mrMismatch.bank, mrMismatch.bank.exams.front(), 5);
        CHECK_EQ(items.items.size(), size_t(0));
        CHECK_EQ(items.issues.size(), size_t(2));   // 「按总分筛掉」+「只抽到 0/1」
        bool mentioned = false;
        for (const quiz::io::Issue& issue : items.issues) {
            if (issue.message.find("总分") != std::string::npos) {
                mentioned = true;
            }
        }
        CHECK(mentioned);
    }

    // 普通规则只抽独立题：材料小题即使题型、标签或总分命中，也不能被普通规则带走。
    // 材料段只能由 material 规则整段选中，且 material = m2 只收指定材料。
    const std::string mixedHead =
        "[meta]\nid = mixed\ntitle = 混合规则\n\n"
        "[q]\nid = n1\ntype = single\nstem = 独立单选\noption = 甲\noption = 乙\nanswer = A\n"
        "difficulty = 1\nscore = 1\n\n"
        "[material]\nid = m1\ntext = 材料一\n\n"
        "[q]\nid = m1a\ntype = single\nstem = 材料一问\noption = 甲\noption = 乙\nanswer = A\n"
        "difficulty = 2\nscore = 1\n\n"
        "[q]\nid = m1b\ntype = single\nstem = 材料二问\noption = 甲\noption = 乙\nanswer = A\n"
        "difficulty = 2\nscore = 3\n\n"
        "[break]\n\n"
        "[material]\nid = m2\ntext = 材料二\n\n"
        "[q]\nid = m2a\ntype = single\nstem = 另一段材料一问\noption = 甲\noption = 乙\nanswer = A\n"
        "difficulty = 3\nscore = 2\n\n"
        "[q]\nid = m2b\ntype = single\nstem = 另一段材料二问\noption = 甲\noption = 乙\nanswer = A\n"
        "difficulty = 3\nscore = 2\n\n"
        "[break]\n\n";

    const std::string normalRuleBank =
        mixedHead + "[exam]\nid = normal-rule\ntitle = 普通规则\nseed = 4\n\n"
                    "[draw]\ncount = 2\ntype = single\nscore = 4\n";
    const quiz::io::LoadResult normalRule = quiz::io::loadBank(normalRuleBank, "mixed");
    CHECK(normalRule.ok());
    if (normalRule.ok() && !normalRule.bank.exams.empty()) {
        const quiz::io::ExamPaper items =
            quiz::io::buildExamPaper(normalRule.bank, normalRule.bank.exams.front(), 4);
        CHECK_EQ(items.items.size(), size_t(1));   // 只抽到独立题 n1
        CHECK_EQ(items.totalScore(), 4);           // 普通题的规则分值仍然覆盖题目自带分值
        CHECK_EQ(items.issues.size(), size_t(1));  // 材料不能被用来补足 2/2
        if (items.items.size() == 1) {
            CHECK_EQ(items.items[0].questionIndex, 0);
            CHECK_EQ(items.items[0].score, 4);
        }
    }

    const std::string materialRuleBank =
        mixedHead + "[exam]\nid = material-rule\ntitle = 材料规则\nseed = 4\n\n"
                    "[draw]\ncount = 1\ntype = material\nscore = 4\n";
    const quiz::io::LoadResult materialRule = quiz::io::loadBank(materialRuleBank, "mixed");
    CHECK(materialRule.ok());
    if (materialRule.ok() && !materialRule.bank.exams.empty()) {
        const quiz::io::ExamPaper items =
            quiz::io::buildExamPaper(materialRule.bank, materialRule.bank.exams.front(), 4);
        CHECK_EQ(items.items.size(), size_t(2));   // 一段材料的两道小题整段进卷
        CHECK_EQ(items.totalScore(), 4);
        CHECK_EQ(items.issues.size(), size_t(0));
    }

    const std::string namedMaterialBank =
        mixedHead + "[exam]\nid = named-material\ntitle = 指定材料\nseed = 4\n\n"
                    "[draw]\ncount = 1\ntype = material\nmaterial = m2\nscore = 4\n";
    const quiz::io::LoadResult namedMaterial = quiz::io::loadBank(namedMaterialBank, "mixed");
    CHECK(namedMaterial.ok());
    if (namedMaterial.ok() && !namedMaterial.bank.exams.empty()) {
        const quiz::io::ExamPaper items =
            quiz::io::buildExamPaper(namedMaterial.bank, namedMaterial.bank.exams.front(), 4);
        CHECK_EQ(items.items.size(), size_t(2));
        for (const quiz::io::ExamPaperItem& item : items.items) {
            CHECK_EQ(namedMaterial.bank.questions[static_cast<size_t>(item.questionIndex)].materialId,
                     std::string("m2"));
        }
    }

    // 材料总分不匹配，但另一段已经补足 count 时，不应额外报「总分对不上」。
    const std::string warningBank =
        "[meta]\nid = warning\ntitle = 警告过滤\n\n"
        "[material]\nid = m1\ntext = 总分 4 的材料\n\n"
        "[q]\nid = w1a\ntype = single\nstem = 问题一\noption = 甲\noption = 乙\nanswer = A\nscore = 1\n\n"
        "[q]\nid = w1b\ntype = single\nstem = 问题二\noption = 甲\noption = 乙\nanswer = A\nscore = 3\n\n"
        "[break]\n\n"
        "[material]\nid = m2\ntext = 总分 5 的材料\n\n"
        "[q]\nid = w2a\ntype = single\nstem = 问题三\noption = 甲\noption = 乙\nanswer = A\nscore = 2\n\n"
        "[q]\nid = w2b\ntype = single\nstem = 问题四\noption = 甲\noption = 乙\nanswer = A\nscore = 3\n\n"
        "[break]\n\n"
        "[exam]\nid = warning-paper\ntitle = 警告过滤卷\nseed = 1\n\n"
        "[draw]\ncount = 1\ntype = material\nscore = 5\n";
    const quiz::io::LoadResult warning = quiz::io::loadBank(warningBank, "warning");
    CHECK(warning.ok());
    if (warning.ok() && !warning.bank.exams.empty()) {
        const quiz::io::ExamPaper items =
            quiz::io::buildExamPaper(warning.bank, warning.bank.exams.front(), 1);
        CHECK_EQ(items.items.size(), size_t(2));
        CHECK_EQ(items.totalScore(), 5);
        CHECK_EQ(items.issues.size(), size_t(0));
    }
}

// ---------------------------------------------------------------------------
// 考试模式：会话（答题只记草稿 / 交卷才判分 / 空题不算错题）
// ---------------------------------------------------------------------------
void testExamSession() {
    std::printf("考试：会话（只记草稿 / 交卷才判分）\n");
    const quiz::io::LoadResult loaded = quiz::io::loadBank(kExamBank, "exam-bank");
    CHECK(loaded.ok());
    if (!loaded.ok()) {
        return;
    }
    quiz::QuestionBank bank = loaded.bank;
    quiz::BankProgress progress;
    quiz::QuizSession session;
    session.bind(&bank, &progress);

    session.resetOrderExam({0, 2});   // q1（单选）与 q3（判断）
    CHECK(session.isExamMode());
    CHECK_EQ(session.planSize(), 2);
    session.jumpToRoundStart();
    session.selectOption(0);          // q1 选 A（正确）
    CHECK(!session.submitted());      // 考试中不判分
    CHECK(session.isExamAnsweredAt(0));
    CHECK(!progress.questions[0].answered);   // 答题期间不写进度
    session.selectOption(1);          // 考试中可以改答案
    session.selectOption(0);
    session.submit("2026-10-16 10:00:00");    // 考试模式下 submit 是空操作
    CHECK(!session.submitted());
    CHECK(!progress.questions[0].answered);

    session.jumpTo(2);                // q3 空着不答
    const std::vector<quiz::ExamQuestionResult> results = session.finishExam("2026-10-16 10:05:00");
    CHECK(!session.isExamMode());
    CHECK_EQ(results.size(), size_t(2));
    if (results.size() == 2) {
        CHECK(results[0].answered);
        CHECK(results[0].correct);
        CHECK(!results[1].answered);   // 空题
    }
    CHECK(progress.questions[0].answered && progress.questions[0].correct);
    CHECK_EQ(progress.questions[0].rightCount, 1);
    CHECK_EQ(progress.questions[0].wrongCount, 0);
    CHECK(!progress.questions[2].answered);   // 空题不动进度：不该被记成错题
    CHECK_EQ(progress.answeredCount(), 1);
    CHECK(session.isSubmittedAt(0));           // 交卷后可以看判定与解析
    CHECK(session.isCorrectAt(0));

    // 答错的题：交卷那一刻才进错题本（错一次）
    quiz::BankProgress second;
    session.bind(&bank, &second);
    session.resetOrderExam({0});
    session.selectOption(1);                   // 选 B（错）
    const std::vector<quiz::ExamQuestionResult> wrongResults =
        session.finishExam("2026-10-16 11:00:00");
    CHECK_EQ(wrongResults.size(), size_t(1));
    if (!wrongResults.empty()) {
        CHECK(wrongResults[0].answered);
        CHECK(!wrongResults[0].correct);
    }
    CHECK_EQ(second.questions[0].wrongCount, 1);
    CHECK(quiz::isWrongBookItem(second.questions[0], 3));
}
int main(int argc, char** argv) {
    if (argc > 0 && argv[0] != nullptr) {
        g_exeDirectory = directoryOf(argv[0]);
    }
    std::printf("==== AxeQuiz / 题斩：核心逻辑测试 ====\n");
    testParseBank();
    testAnswerVariants();
    testInvalidBank();
    testTypeDiagnostics();
    testRoundTrip();
    testSession();
    testProgressRoundTrip();
    testWrongOnlyExport();
    testTaggedExport();
    testImportHelpers();
    testWeightedPractice();
    testWeightedTagDifficulty();
    testI18n();
    testSettings();
    testStats();
    testPracticeOrder();
    testThemePresets();
    testReciteMode();
    testWrongBookRule();
    testDraftState();
    testThemePresetFile();
    testFillQuestion();
    testFillMultiBlank();
    testIndeterminateQuestion();
    testOrderedQuestion();
    testMaterialQuestion();
    testMaterialBreak();
    testPaperDrafts();
    testNewTypesRoundTrip();
    testProgressWithPartialScore();
    testNoAnswerReplay();
    testExamFormat();
    testExamPaper();
    testExamSession();
    testMathTextCompatibility();
    testSvgRasterize();

    std::printf("==== 共 %d 项检查，失败 %d 项 ====\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

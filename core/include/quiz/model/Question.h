#pragma once

#include <map>
#include <string>
#include <vector>

// ============================================================================
// 题目模型（纯数据，不依赖任何平台 API，可以在电脑上直接跑单元测试）
// ============================================================================
namespace quiz {

/// 题型：单选 / 多选 / 判断 / 填空 / 不定项 / 多项有序 / 材料
/// 判分规则集中写在 quiz/model/Scoring.h，核心逻辑与界面共用同一套
enum class QuestionType {
    Single,         // 单选：选一个，选对才算对
    Multiple,       // 多选：全对才算对（少选、多选都算错）
    Judge,          // 判断（正确 / 错误）
    Fill,           // 填空：答案是文字，见 textAnswers
    Indeterminate,  // 不定项：少选给部分分，选到错的得 0 分
    Ordered,        // 多项有序（七选五 / 段落大意配对 / 排序）：每个空按顺序选一个选项
    Material,       // 材料题：一道挂在材料下的小题，答题方式与单选相同
};

const char* toString(QuestionType type);
bool parseQuestionType(const std::string& text, QuestionType& out);

/// 一道题
struct Question {
    std::string id;                    // 题目编号（题库内唯一；缺省时自动生成 q1、q2…）
    QuestionType type = QuestionType::Single;
    std::string stem;                  // 题干
    std::vector<std::string> options;  // 选项文本（判断题不使用）
    std::vector<int> answer;           // 正确答案的下标；判断题：0=正确 1=错误；
                                       // 有序题：按空的先后顺序存（第 1 个空选了哪个选项）
    // 填空题：每个空的答案（外层 = 空，内层 = 同一个空的多个可接受答案）
    // 题干里的编号（【1】【2】…）由作者自己写，程序只按顺序对应
    std::vector<std::vector<std::string>> fillAnswers;
    std::vector<std::string> blanks;       // 有序题每个空的提示文字（可选），来自 blank = …
    std::string materialId;            // 材料题：所属材料 id（对应 QuestionBank::materials）
    int materialPart = 0;              // 材料下的小题序号（1 开始；0 = 不属于任何材料）
    int materialCount = 0;             // 同一材料下的小题总数
    std::string analysis;              // 解析
    std::vector<std::string> tags;     // 标签，便于以后做分类练习
    int difficulty = 0;                // 难度 0=未设置，1~5
    int score = 1;                     // 分值（考试模式算分用；[draw] 里的 score 会覆盖它）
    std::string source;                // 出处

    /// 预留的多语言内容：key 形如 "stem@en"、"analysis@en"
    /// （界面文案的语言包见 quiz/i18n/I18n.h；这里是「题目内容」的多语言扩展点）
    std::map<std::string, std::string> localized;

    /// 取指定语言的题干/解析；没有对应翻译时回退到默认文本
    const std::string& stemFor(const std::string& lang) const;
    const std::string& analysisFor(const std::string& lang) const;

    /// 判断题的答案是不是「正确」
    bool isJudgeTrue() const { return !answer.empty() && answer.front() == 0; }

    /// 填空题有几个空
    int fillBlankCount() const { return static_cast<int>(fillAnswers.size()); }

    /// 有序题有几个空（答案个数与 blank 行数的较大者）
    int slotCount() const {
        return static_cast<int>(answer.size() > blanks.size() ? answer.size() : blanks.size());
    }

    /// 是不是「点选项」这种答题方式（填空与有序题不是）
    bool isChoice() const {
        return type == QuestionType::Single || type == QuestionType::Multiple ||
               type == QuestionType::Judge || type == QuestionType::Indeterminate ||
               type == QuestionType::Material;
    }
};

/// 选项下标 → 字母（0→A、1→B…），用于显示与题库文件里的答案写法
std::string indexToLetter(int index);

/// 字母 → 选项下标；不是合法字母时返回 -1
int letterToIndex(const std::string& letter);

}  // namespace quiz

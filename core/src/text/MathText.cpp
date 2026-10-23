#include "quiz/text/MathText.h"

#include <cctype>
#include <string>
#include <unordered_map>

namespace quiz::math {

namespace {

bool startsWith(std::string_view value, size_t pos, std::string_view needle) {
    return pos + needle.size() <= value.size() && value.substr(pos, needle.size()) == needle;
}

const std::unordered_map<std::string, std::string>& commandMap() {
    static const std::unordered_map<std::string, std::string> commands = {
        // 希腊字母
        {"alpha", "α"},       {"beta", "β"},        {"gamma", "γ"},
        {"delta", "δ"},       {"epsilon", "ε"},     {"varepsilon", "ε"},
        {"zeta", "ζ"},        {"eta", "η"},         {"theta", "θ"},
        {"vartheta", "ϑ"},    {"iota", "ι"},        {"kappa", "κ"},
        {"lambda", "λ"},      {"mu", "μ"},          {"nu", "ν"},
        {"xi", "ξ"},          {"omicron", "ο"},     {"pi", "π"},
        {"varpi", "ϖ"},       {"rho", "ρ"},         {"varrho", "ϱ"},
        {"sigma", "σ"},       {"varsigma", "ς"},    {"tau", "τ"},
        {"upsilon", "υ"},     {"phi", "φ"},         {"varphi", "ϕ"},
        {"chi", "χ"},         {"psi", "ψ"},         {"omega", "ω"},
        {"Gamma", "Γ"},       {"Delta", "Δ"},       {"Theta", "Θ"},
        {"Lambda", "Λ"},      {"Xi", "Ξ"},          {"Pi", "Π"},
        {"Sigma", "Σ"},       {"Upsilon", "Υ"},     {"Phi", "Φ"},
        {"Psi", "Ψ"},         {"Omega", "Ω"},

        // 关系与运算
        {"le", "≤"},          {"leq", "≤"},         {"ge", "≥"},
        {"geq", "≥"},         {"ne", "≠"},          {"neq", "≠"},
        {"approx", "≈"},      {"equiv", "≡"},       {"sim", "∼"},
        {"simeq", "≃"},       {"propto", "∝"},      {"ll", "≪"},
        {"gg", "≫"},          {"times", "×"},       {"div", "÷"},
        {"pm", "±"},          {"mp", "∓"},          {"cdot", "·"},
        {"ast", "∗"},         {"circ", "∘"},        {"bullet", "•"},
        {"infty", "∞"},       {"sum", "∑"},         {"prod", "∏"},
        {"int", "∫"},         {"partial", "∂"},     {"nabla", "∇"},
        {"forall", "∀"},      {"exists", "∃"},      {"emptyset", "∅"},
        {"in", "∈"},          {"notin", "∉"},       {"ni", "∋"},
        {"subset", "⊂"},      {"subseteq", "⊆"},    {"supset", "⊃"},
        {"supseteq", "⊇"},    {"cup", "∪"},         {"cap", "∩"},
        {"land", "∧"},        {"lor", "∨"},         {"neg", "¬"},
        {"perp", "⊥"},        {"parallel", "∥"},    {"angle", "∠"},
        {"triangle", "△"},    {"degree", "°"},      {"prime", "′"},
        {"to", "→"},          {"rightarrow", "→"},  {"leftarrow", "←"},
        {"leftrightarrow", "↔"}, {"Rightarrow", "⇒"}, {"Leftarrow", "⇐"},
        {"Leftrightarrow", "⇔"}, {"uparrow", "↑"}, {"downarrow", "↓"},
        {"cdots", "⋯"},       {"ldots", "…"},       {"dots", "…"},
        {"hbar", "ℏ"},        {"ell", "ℓ"},         {"Re", "ℜ"},
        {"Im", "ℑ"},          {"aleph", "ℵ"},       {"therefore", "∴"},
        {"because", "∵"},

        // 可选的排版命令：保留内容，去掉控制词本身。
        {"left", ""},         {"right", ""},        {"displaystyle", ""},
        {"textstyle", ""},    {"limits", ""},       {"nolimits", ""},

        // 转义字符与空白。反斜杠命令扫描一次会吃掉这些单字符。
        {"%", "%"},           {"$", "$"},           {"&", "&"},
        {"#", "#"},           {"_", "_"},           {"{", "{"},
        {"}", "}"},           {"\\", "\n"},         {",", " "},
        {";", " "},           {":", " "},           {"!", ""},
    };
    return commands;
}

const std::unordered_map<std::string, std::string>& textCommandMap() {
    static const std::unordered_map<std::string, std::string> commands = {
        {"text", ""}, {"textrm", ""}, {"textnormal", ""}, {"mathrm", ""},
        {"mathbf", ""}, {"mathit", ""}, {"mathsf", ""}, {"mathtt", ""},
        {"operatorname", ""},
    };
    return commands;
}

void skipSpaces(std::string_view input, size_t& pos) {
    while (pos < input.size() && (input[pos] == ' ' || input[pos] == '\t' || input[pos] == '\n')) {
        ++pos;
    }
}

/// 读取一个 `{...}` 参数。花括号允许嵌套；返回时 pos 位于右花括号之后。
bool readBracedGroup(std::string_view input, size_t& pos, std::string& out) {
    skipSpaces(input, pos);
    if (pos >= input.size() || input[pos] != '{') {
        return false;
    }
    const size_t begin = ++pos;
    int depth = 1;
    for (; pos < input.size(); ++pos) {
        if (input[pos] == '\\' && pos + 1 < input.size()) {
            pos += 1;
            continue;
        }
        if (input[pos] == '{') {
            ++depth;
        } else if (input[pos] == '}') {
            --depth;
            if (depth == 0) {
                out.assign(input.data() + begin, pos - begin);
                ++pos;
                return true;
            }
        }
    }
    pos = begin - 1;
    return false;
}

/// 上下标既可写成 `^2`，也可写成 `^{2}`。单个 UTF-8 字符作为参数时按字节长度读取，
/// 避免把中文字符或未知符号切坏。
bool readScriptArgument(std::string_view input, size_t& pos, std::string& out) {
    skipSpaces(input, pos);
    if (readBracedGroup(input, pos, out)) {
        return true;
    }
    if (pos >= input.size()) {
        return false;
    }
    const unsigned char first = static_cast<unsigned char>(input[pos]);
    size_t length = 1;
    if ((first & 0xE0u) == 0xC0u) {
        length = 2;
    } else if ((first & 0xF0u) == 0xE0u) {
        length = 3;
    } else if ((first & 0xF8u) == 0xF0u) {
        length = 4;
    }
    if (pos + length > input.size()) {
        length = 1;
    }
    out.assign(input.data() + pos, length);
    pos += length;
    return true;
}

const char* superscriptOf(char ch) {
    switch (ch) {
        case '0': return "⁰";
        case '1': return "¹";
        case '2': return "²";
        case '3': return "³";
        case '4': return "⁴";
        case '5': return "⁵";
        case '6': return "⁶";
        case '7': return "⁷";
        case '8': return "⁸";
        case '9': return "⁹";
        case '+': return "⁺";
        case '-': return "⁻";
        case '=': return "⁼";
        case '(': return "⁽";
        case ')': return "⁾";
        case 'n': return "ⁿ";
        case 'i': return "ⁱ";
        default: return nullptr;
    }
}

const char* subscriptOf(char ch) {
    switch (ch) {
        case '0': return "₀";
        case '1': return "₁";
        case '2': return "₂";
        case '3': return "₃";
        case '4': return "₄";
        case '5': return "₅";
        case '6': return "₆";
        case '7': return "₇";
        case '8': return "₈";
        case '9': return "₉";
        case '+': return "₊";
        case '-': return "₋";
        case '=': return "₌";
        case '(': return "₍";
        case ')': return "₎";
        case 'a': return "ₐ";
        case 'e': return "ₑ";
        case 'h': return "ₕ";
        case 'i': return "ᵢ";
        case 'j': return "ⱼ";
        case 'k': return "ₖ";
        case 'l': return "ₗ";
        case 'm': return "ₘ";
        case 'n': return "ₙ";
        case 'o': return "ₒ";
        case 'p': return "ₚ";
        case 'r': return "ᵣ";
        case 's': return "ₛ";
        case 't': return "ₜ";
        case 'u': return "ᵤ";
        case 'v': return "ᵥ";
        case 'x': return "ₓ";
        default: return nullptr;
    }
}

/// 上下标参数必须能完整映射才转换；否则保留 `^...` / `_...` 原文，避免出现半截公式。
bool appendScript(std::string& out, const std::string& argument, bool superscript) {
    std::string converted;
    for (unsigned char ch : argument) {
        const char* mapped =
            superscript ? (ch < 128 ? superscriptOf(static_cast<char>(ch)) : nullptr)
                        : (ch < 128 ? subscriptOf(static_cast<char>(ch)) : nullptr);
        if (mapped == nullptr) {
            return false;
        }
        converted += mapped;
    }
    out += converted;
    return true;
}

/// 读取反斜杠命令名。字母命令读到非字母；`\,` 这类单字符命令只取一个字符。
std::string readCommandName(std::string_view input, size_t& pos) {
    // pos 指向反斜杠之后。
    if (pos >= input.size()) {
        return {};
    }
    const unsigned char ch = static_cast<unsigned char>(input[pos]);
    if (!std::isalpha(ch)) {
        return std::string(1, static_cast<char>(input[pos++]));
    }
    const size_t begin = pos;
    while (pos < input.size() && std::isalpha(static_cast<unsigned char>(input[pos]))) {
        ++pos;
    }
    return std::string(input.substr(begin, pos - begin));
}

}  // namespace

std::string latexToUnicode(std::string_view latex) {
    std::string out;
    out.reserve(latex.size());

    for (size_t pos = 0; pos < latex.size();) {
        const char ch = latex[pos];
        if (ch == '\\') {
            const size_t slashPos = pos++;
            const std::string command = readCommandName(latex, pos);
            if (command.empty()) {
                out.push_back('\\');
                continue;
            }

            if (command == "frac") {
                std::string numerator;
                std::string denominator;
                if (readBracedGroup(latex, pos, numerator) &&
                    readBracedGroup(latex, pos, denominator)) {
                    out += latexToUnicode(numerator);
                    out.push_back('/');
                    out += latexToUnicode(denominator);
                    continue;
                }
                // 参数不完整：退回原命令，让后面的字符继续正常扫描。
                out.append(latex.substr(slashPos, pos - slashPos));
                continue;
            }

            if (command == "sqrt") {
                std::string radicand;
                if (readBracedGroup(latex, pos, radicand)) {
                    out += "√(";
                    out += latexToUnicode(radicand);
                    out.push_back(')');
                    continue;
                }
                if (readScriptArgument(latex, pos, radicand)) {
                    out += "√(";
                    out += latexToUnicode(radicand);
                    out.push_back(')');
                    continue;
                }
                out += "√";
                continue;
            }

            const auto textIt = textCommandMap().find(command);
            if (textIt != textCommandMap().end()) {
                std::string content;
                if (readBracedGroup(latex, pos, content)) {
                    out += latexToUnicode(content);
                }
                continue;
            }

            const auto it = commandMap().find(command);
            if (it != commandMap().end()) {
                out += it->second;
            } else {
                // 未知命令必须原样保留。后面若有 `{...}`，花括号也会按普通字符继续输出。
                out.push_back('\\');
                out += command;
            }
            continue;
        }

        if (ch == '^' || ch == '_') {
            const size_t markerPos = pos++;
            std::string argument;
            if (readScriptArgument(latex, pos, argument) &&
                appendScript(out, argument, ch == '^')) {
                continue;
            }
            out.push_back(ch);
            out.append(latex.substr(markerPos + 1, pos - markerPos - 1));
            continue;
        }

        out.push_back(ch);
        ++pos;
    }
    return out;
}

std::vector<MathSegment> splitFormulaSegments(std::string_view text) {
    std::vector<MathSegment> segments;
    std::string plain;

    const auto flushPlain = [&]() {
        if (!plain.empty()) {
            segments.push_back({MathSegmentKind::Text, plain, plain});
            plain.clear();
        }
    };
    const auto pushFormula = [&](MathSegmentKind kind, std::string_view source) {
        segments.push_back({kind, std::string(source), latexToUnicode(source)});
    };

    for (size_t pos = 0; pos < text.size();) {
        if (text[pos] == '\\' && pos + 1 < text.size() && text[pos + 1] == '$') {
            plain.push_back('$');
            pos += 2;
            continue;
        }
        if (text[pos] != '$') {
            plain.push_back(text[pos++]);
            continue;
        }

        const MathSegmentKind kind =
            startsWith(text, pos, "$$") ? MathSegmentKind::DisplayFormula
                                        : MathSegmentKind::InlineFormula;
        const size_t delimiterLength = kind == MathSegmentKind::DisplayFormula ? 2 : 1;
        const size_t contentBegin = pos + delimiterLength;

        size_t close = contentBegin;
        bool escaped = false;
        while (close < text.size()) {
            if (escaped) {
                escaped = false;
                ++close;
                continue;
            }
            if (text[close] == '\\') {
                escaped = true;
                ++close;
                continue;
            }
            if (kind == MathSegmentKind::DisplayFormula) {
                if (startsWith(text, close, "$$")) {
                    break;
                }
            } else if (text[close] == '$') {
                break;
            }
            ++close;
        }

        if (close >= text.size()) {
            // 未闭合：保留美元符号本身，继续扫描后半段普通文本。
            plain.append(text.substr(pos, delimiterLength));
            pos += delimiterLength;
            continue;
        }

        flushPlain();
        pushFormula(kind, text.substr(contentBegin, close - contentBegin));
        pos = close + delimiterLength;
    }
    flushPlain();
    return segments;
}

std::string toDisplayText(std::string_view text) {
    std::string out;
    for (const MathSegment& segment : splitFormulaSegments(text)) {
        if (segment.kind == MathSegmentKind::DisplayFormula) {
            if (!out.empty() && out.back() != '\n') {
                out.push_back('\n');
            }
            out += segment.text;
            out.push_back('\n');
        } else {
            out += segment.text;
        }
    }
    return out;
}

}  // namespace quiz::math

#include "quiz/io/TextDocument.h"

#include <cctype>

namespace quiz::io {

namespace {

bool isBlankOrComment(const std::string& line) {
    const std::string trimmed = trim(line);
    return trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';';
}

}  // namespace

std::string trim(const std::string& text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::string unescape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    size_t i = 0;
    while (i < value.size()) {
        const char c = value[i];
        if (c == '\\' && i + 1 < value.size()) {
            const char next = value[i + 1];
            if (next == 'n') {
                out.push_back('\n');
                i += 2;
                continue;
            }
            if (next == 't') {
                out.push_back('\t');
                i += 2;
                continue;
            }
            if (next == '\\') {
                out.push_back('\\');
                i += 2;
                continue;
            }
        }
        out.push_back(c);
        ++i;
    }
    return out;
}

std::string escape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (char c : value) {
        if (c == '\n') {
            out += "\\n";
        } else if (c == '\t') {
            out += "\\t";
        } else if (c == '\\') {
            out += "\\\\";
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::vector<std::string> split(const std::string& text, char separator) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : text) {
        if (c == separator) {
            const std::string piece = trim(current);
            if (!piece.empty()) {
                parts.push_back(piece);
            }
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    const std::string piece = trim(current);
    if (!piece.empty()) {
        parts.push_back(piece);
    }
    return parts;
}

bool startsWith(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string current;
    for (char c : text) {
        if (c == '\n') {
            if (!current.empty() && current.back() == '\r') {
                current.pop_back();
            }
            lines.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        lines.push_back(current);
    }
    return lines;
}

std::vector<Line> parse(const std::string& text, std::vector<Issue>& issues) {
    std::vector<Line> result;
    std::string currentSection;

    // 记事本之类编辑器存 UTF-8 时会带 BOM：不剥掉的话第一行（通常是 [段落]）
    // 会被当成普通文本，整段的键值就都归不到段落里了。
    std::string body = text;
    if (body.size() >= 3 && static_cast<unsigned char>(body[0]) == 0xEF &&
        static_cast<unsigned char>(body[1]) == 0xBB && static_cast<unsigned char>(body[2]) == 0xBF) {
        body.erase(0, 3);
    }

    const std::vector<std::string> rawLines = splitLines(body);

    for (size_t i = 0; i < rawLines.size(); ++i) {
        const int lineNumber = static_cast<int>(i) + 1;
        if (isBlankOrComment(rawLines[i])) {
            continue;
        }

        const std::string content = trim(rawLines[i]);
        Line line;
        line.lineNumber = lineNumber;

        if (content[0] == '[') {
            const size_t close = content.find(']');
            if (close == std::string::npos) {
                issues.push_back({lineNumber, "段落标记缺少 ]"});
                continue;
            }
            currentSection = trim(content.substr(1, close - 1));
            line.isSection = true;
            line.section = currentSection;
            result.push_back(line);
            continue;
        }

        const size_t equal = content.find('=');
        if (equal == std::string::npos) {
            issues.push_back({lineNumber, "这一行既不是 [段落] 也不是 key = value"});
            continue;
        }

        line.section = currentSection;
        line.key = trim(content.substr(0, equal));
        line.value = unescape(trim(content.substr(equal + 1)));
        result.push_back(line);
    }

    return result;
}

}  // namespace quiz::io

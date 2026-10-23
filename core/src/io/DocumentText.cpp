#include "quiz/io/DocumentText.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <utility>
#include <vector>

#define STBI_NO_STDIO
#include "stb_image.h"

namespace quiz::io {

namespace {

constexpr size_t kMaxArchiveBytes = 32 * 1024 * 1024;
constexpr size_t kMaxDocumentXmlBytes = 16 * 1024 * 1024;
constexpr size_t kMaxZipEntries = 4096;

struct ZipEntry {
    std::string name;
    uint16_t method = 0;
    uint32_t compressedSize = 0;
    uint32_t uncompressedSize = 0;
    uint32_t localHeaderOffset = 0;
};

bool hasPrefix(const std::string& text, std::string_view prefix) {
    return text.size() >= prefix.size() &&
           std::memcmp(text.data(), prefix.data(), prefix.size()) == 0;
}

bool readU16(const std::string& bytes, size_t offset, uint16_t& out) {
    if (offset > bytes.size() || bytes.size() - offset < 2) {
        return false;
    }
    const auto* p = reinterpret_cast<const unsigned char*>(bytes.data() + offset);
    out = static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
    return true;
}

bool readU32(const std::string& bytes, size_t offset, uint32_t& out) {
    if (offset > bytes.size() || bytes.size() - offset < 4) {
        return false;
    }
    const auto* p = reinterpret_cast<const unsigned char*>(bytes.data() + offset);
    out = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
          (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    return true;
}

std::string lowerExtension(const std::string& fileName) {
    const size_t slash = fileName.find_last_of("/\\");
    const size_t dot = fileName.find_last_of('.');
    if (dot == std::string::npos || dot == 0 ||
        (slash != std::string::npos && dot < slash)) {
        return {};
    }
    std::string extension = fileName.substr(dot);
    for (char& ch : extension) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return extension;
}

bool isWordExtension(const std::string& extension) {
    return extension == ".doc" || extension == ".docx" || extension == ".docm" ||
           extension == ".dot" || extension == ".dotx" || extension == ".dotm";
}

bool isOpenXmlWordExtension(const std::string& extension) {
    return extension == ".docx" || extension == ".docm" ||
           extension == ".dotx" || extension == ".dotm";
}

bool isZipArchive(const std::string& bytes) {
    return hasPrefix(bytes, std::string_view("PK\x03\x04", 4));
}

bool findZipEntry(const std::string& bytes, std::string_view wanted, ZipEntry& out,
                  std::string& error) {
    if (bytes.size() > kMaxArchiveBytes) {
        error = "文档压缩包超过 32 MB 限制";
        return false;
    }

    size_t eocd = std::string::npos;
    const size_t searchBegin = bytes.size() > 65557 ? bytes.size() - 65557 : 0;
    for (size_t pos = bytes.size(); pos >= searchBegin + 4; --pos) {
        const size_t candidate = pos - 4;
        if (bytes.compare(candidate, 4, "PK\x05\x06") == 0) {
            eocd = candidate;
            break;
        }
        if (candidate == searchBegin) {
            break;
        }
    }
    if (eocd == std::string::npos) {
        error = "找不到 ZIP 目录";
        return false;
    }

    uint16_t entryCount = 0;
    uint32_t centralOffset = 0;
    if (!readU16(bytes, eocd + 10, entryCount) ||
        !readU32(bytes, eocd + 16, centralOffset)) {
        error = "ZIP 目录不完整";
        return false;
    }
    if (entryCount > kMaxZipEntries) {
        error = "文档条目过多";
        return false;
    }

    size_t cursor = centralOffset;
    for (uint16_t index = 0; index < entryCount; ++index) {
        if (cursor > bytes.size() || bytes.size() - cursor < 4) {
            error = "ZIP 中央目录越界";
            return false;
        }
        if (bytes.compare(cursor, 4, "PK\x01\x02") != 0) {
            error = "ZIP 中央目录损坏";
            return false;
        }
        uint16_t nameLength = 0;
        uint16_t extraLength = 0;
        uint16_t commentLength = 0;
        ZipEntry entry;
        if (!readU16(bytes, cursor + 10, entry.method) ||
            !readU32(bytes, cursor + 20, entry.compressedSize) ||
            !readU32(bytes, cursor + 24, entry.uncompressedSize) ||
            !readU16(bytes, cursor + 28, nameLength) ||
            !readU16(bytes, cursor + 30, extraLength) ||
            !readU16(bytes, cursor + 32, commentLength) ||
            !readU32(bytes, cursor + 42, entry.localHeaderOffset)) {
            error = "ZIP 条目头损坏";
            return false;
        }
        const size_t nameOffset = cursor + 46;
        if (nameOffset > bytes.size() || bytes.size() - nameOffset < nameLength) {
            error = "ZIP 文件名越界";
            return false;
        }
        entry.name.assign(bytes.data() + nameOffset, nameLength);
        if (entry.name == wanted) {
            out = std::move(entry);
            return true;
        }
        cursor = nameOffset + nameLength + extraLength + commentLength;
        if (cursor > bytes.size()) {
            error = "ZIP 中央目录越界";
            return false;
        }
    }
    error = "Word 文档缺少 word/document.xml";
    return false;
}

bool readZipEntry(const std::string& bytes, const ZipEntry& entry, std::string& out,
                  std::string& error) {
    if (entry.uncompressedSize > kMaxDocumentXmlBytes) {
        error = "Word 正文超过 16 MB 限制";
        return false;
    }
    if (entry.localHeaderOffset > bytes.size() ||
        bytes.size() - entry.localHeaderOffset < 4 ||
        bytes.compare(entry.localHeaderOffset, 4, "PK\x03\x04") != 0) {
        error = "ZIP 本地文件头损坏";
        return false;
    }
    uint16_t nameLength = 0;
    uint16_t extraLength = 0;
    if (!readU16(bytes, static_cast<size_t>(entry.localHeaderOffset) + 26, nameLength) ||
        !readU16(bytes, static_cast<size_t>(entry.localHeaderOffset) + 28, extraLength)) {
        error = "ZIP 本地文件头不完整";
        return false;
    }
    const size_t dataOffset =
        static_cast<size_t>(entry.localHeaderOffset) + 30 + nameLength + extraLength;
    if (dataOffset > bytes.size() || bytes.size() - dataOffset < entry.compressedSize) {
        error = "ZIP 文件数据越界";
        return false;
    }
    const char* data = bytes.data() + dataOffset;

    if (entry.method == 0) {
        out.assign(data, entry.compressedSize);
        return true;
    }
    if (entry.method != 8) {
        error = "不支持的 ZIP 压缩方式";
        return false;
    }
    if (entry.uncompressedSize == 0) {
        out.clear();
        return true;
    }
    if (entry.compressedSize > static_cast<uint32_t>(INT_MAX) ||
        entry.uncompressedSize > static_cast<uint32_t>(INT_MAX)) {
        error = "Word 正文过大";
        return false;
    }

    std::vector<char> decoded(entry.uncompressedSize);
    const int size = stbi_zlib_decode_noheader_buffer(
        decoded.data(), static_cast<int>(decoded.size()), data,
        static_cast<int>(entry.compressedSize));
    if (size < 0) {
        error = "Word 正文解压失败";
        return false;
    }
    out.assign(decoded.data(), static_cast<size_t>(size));
    return true;
}

void appendUtf8(std::string& out, uint32_t codepoint) {
    if (codepoint <= 0x7F) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

std::string decodeEntities(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (size_t pos = 0; pos < value.size();) {
        if (value[pos] != '&') {
            out.push_back(value[pos++]);
            continue;
        }
        const size_t semicolon = value.find(';', pos + 1);
        if (semicolon == std::string_view::npos) {
            out.push_back('&');
            ++pos;
            continue;
        }
        const std::string_view entity = value.substr(pos + 1, semicolon - pos - 1);
        if (entity == "amp") {
            out.push_back('&');
        } else if (entity == "lt") {
            out.push_back('<');
        } else if (entity == "gt") {
            out.push_back('>');
        } else if (entity == "quot") {
            out.push_back('"');
        } else if (entity == "apos") {
            out.push_back('\'');
        } else if (!entity.empty() && entity[0] == '#') {
            uint32_t codepoint = 0;
            int base = 10;
            size_t begin = 1;
            if (entity.size() > 2 && (entity[1] == 'x' || entity[1] == 'X')) {
                base = 16;
                begin = 2;
            }
            bool valid = begin < entity.size();
            for (size_t i = begin; valid && i < entity.size(); ++i) {
                const unsigned char ch = static_cast<unsigned char>(entity[i]);
                int digit = -1;
                if (ch >= '0' && ch <= '9') {
                    digit = ch - '0';
                } else if (base == 16 && ch >= 'a' && ch <= 'f') {
                    digit = ch - 'a' + 10;
                } else if (base == 16 && ch >= 'A' && ch <= 'F') {
                    digit = ch - 'A' + 10;
                }
                valid = digit >= 0 && codepoint <= 0x10FFFFu / static_cast<uint32_t>(base);
                if (!valid) {
                    break;
                }
                codepoint = codepoint * static_cast<uint32_t>(base) +
                            static_cast<uint32_t>(digit);
            }
            if (valid && codepoint <= 0x10FFFF) {
                appendUtf8(out, codepoint);
            } else {
                out.append(value.substr(pos, semicolon - pos + 1));
            }
        } else {
            out.append(value.substr(pos, semicolon - pos + 1));
        }
        pos = semicolon + 1;
    }
    return out;
}

void appendLineBreak(std::string& out) {
    if (!out.empty() && out.back() != '\n') {
        out.push_back('\n');
    }
}

std::string tagNameOf(std::string_view tag) {
    size_t begin = 0;
    while (begin < tag.size() &&
           (tag[begin] == '<' || tag[begin] == '/' || std::isspace(
                static_cast<unsigned char>(tag[begin])) != 0)) {
        ++begin;
    }
    size_t end = begin;
    while (end < tag.size() &&
           std::isspace(static_cast<unsigned char>(tag[end])) == 0 &&
           tag[end] != '/' && tag[end] != '>') {
        ++end;
    }
    return std::string(tag.substr(begin, end - begin));
}

std::string openXmlToBankText(const std::string& xml) {
    std::string out;
    bool inText = false;
    size_t pos = 0;
    while (pos < xml.size()) {
        if (xml[pos] == '<') {
            const size_t end = xml.find('>', pos + 1);
            if (end == std::string::npos) {
                break;
            }
            const std::string_view tag(xml.data() + pos, end - pos + 1);
            const bool closing = tag.size() > 1 && tag[1] == '/';
            const bool selfClosing = tag.size() > 2 && tag[tag.size() - 2] == '/';
            const std::string name = tagNameOf(tag);
            if (name == "w:t" || name == "m:t") {
                inText = !closing && !selfClosing;
            } else if (name == "w:tab" || name == "w:ptab") {
                out.push_back('\t');
            } else if (name == "w:br" || name == "w:cr") {
                appendLineBreak(out);
            } else if (closing && name == "w:p") {
                appendLineBreak(out);
            } else if (closing && name == "w:tc") {
                if (!out.empty() && out.back() != '\t' && out.back() != '\n') {
                    out.push_back('\t');
                }
            }
            pos = end + 1;
            continue;
        }
        if (!inText) {
            ++pos;
            continue;
        }
        const size_t end = xml.find('<', pos);
        const size_t textEnd = end == std::string::npos ? xml.size() : end;
        out += decodeEntities(std::string_view(xml.data() + pos, textEnd - pos));
        pos = textEnd;
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == '\t' || out.back() == ' ')) {
        out.pop_back();
    }
    return out;
}

bool looksLikeBankText(const std::string& text) {
    return text.find("[meta]") != std::string::npos ||
           text.find("[q]") != std::string::npos;
}

bool looksLikePlainText(const std::string& text) {
    for (unsigned char ch : text) {
        if (ch < 0x20 && ch != '\n' && ch != '\r' && ch != '\t') {
            return false;
        }
    }
    return true;
}

std::string decodeUtf16(const std::string& bytes, size_t offset, bool littleEndian) {
    std::string out;
    out.reserve((bytes.size() - offset) / 2);
    for (size_t pos = offset; pos + 1 < bytes.size(); pos += 2) {
        const auto* p = reinterpret_cast<const unsigned char*>(bytes.data() + pos);
        uint32_t codepoint = littleEndian
                                 ? static_cast<uint32_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8))
                                 : static_cast<uint32_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
        if (codepoint >= 0xD800 && codepoint <= 0xDBFF && pos + 3 < bytes.size()) {
            const auto* q = reinterpret_cast<const unsigned char*>(bytes.data() + pos + 2);
            const uint32_t low =
                littleEndian ? static_cast<uint32_t>(q[0] | (static_cast<uint16_t>(q[1]) << 8))
                             : static_cast<uint32_t>((static_cast<uint16_t>(q[0]) << 8) | q[1]);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                pos += 2;
            }
        }
        if (codepoint == '\r') {
            if (!out.empty() && out.back() == '\n') {
                continue;
            }
            out.push_back('\n');
        } else if (codepoint == '\n' || codepoint == '\t' || codepoint >= 0x20) {
            appendUtf8(out, codepoint);
        }
    }
    return out;
}

std::string recoverEmbeddedBankText(const std::string& bytes) {
    if (const size_t ascii = bytes.find("[meta]"); ascii != std::string::npos) {
        std::string candidate;
        candidate.reserve(std::min<size_t>(bytes.size() - ascii, 8 * 1024 * 1024));
        for (size_t pos = ascii; pos < bytes.size() &&
                                  candidate.size() < 8 * 1024 * 1024; ++pos) {
            const unsigned char ch = static_cast<unsigned char>(bytes[pos]);
            if (ch == '\n' || ch == '\r' || ch == '\t' || ch >= 0x20) {
                candidate.push_back(static_cast<char>(ch));
            }
        }
        if (looksLikeBankText(candidate)) {
            return candidate;
        }
    }

    static const char kUtf16Marker[] = {'[', 0, 'm', 0, 'e', 0, 't', 0, 'a', 0, ']', 0};
    const std::string utf16Marker(kUtf16Marker, sizeof(kUtf16Marker));
    if (const size_t marker = bytes.find(utf16Marker); marker != std::string::npos) {
        std::string candidate = decodeUtf16(bytes, marker, true);
        if (looksLikeBankText(candidate)) {
            return candidate;
        }
    }
    return {};
}

std::string htmlToBankText(const std::string& html) {
    std::string body = html;
    const size_t headBegin = body.find("<head");
    const size_t headEnd = body.find("</head>");
    if (headBegin != std::string::npos && headEnd != std::string::npos && headEnd > headBegin) {
        body.erase(headBegin, headEnd - headBegin + 7);
    }

    std::string out;
    bool inIgnored = false;
    for (size_t pos = 0; pos < body.size();) {
        if (body[pos] == '<') {
            const size_t end = body.find('>', pos + 1);
            if (end == std::string::npos) {
                break;
            }
            const std::string tag = tagNameOf(std::string_view(body.data() + pos, end - pos + 1));
            const bool closing = body.size() > pos + 1 && body[pos + 1] == '/';
            if (tag == "script" || tag == "style") {
                inIgnored = !closing;
            } else if (!inIgnored && (tag == "br" || tag == "p" || tag == "div" ||
                                      tag == "li" || tag == "tr")) {
                appendLineBreak(out);
            } else if (!inIgnored && closing && tag == "td") {
                out.push_back('\t');
            }
            pos = end + 1;
            continue;
        }
        if (inIgnored) {
            const size_t next = body.find('<', pos);
            pos = next == std::string::npos ? body.size() : next;
            continue;
        }
        const size_t textEnd = body.find('<', pos);
        const size_t end = textEnd == std::string::npos ? body.size() : textEnd;
        out += decodeEntities(std::string_view(body.data() + pos, end - pos));
        pos = end;
    }
    return out;
}

}  // namespace

DocumentTextResult extractBankTextDocument(const std::string& fileName,
                                           const std::string& bytes) {
    if (bytes.empty()) {
        return {false, {}, "文档为空"};
    }

    const std::string extension = lowerExtension(fileName);
    if (isZipArchive(bytes)) {
        ZipEntry entry;
        std::string error;
        if (!findZipEntry(bytes, "word/document.xml", entry, error)) {
            return {false, {}, error};
        }
        std::string xml;
        if (!readZipEntry(bytes, entry, xml, error)) {
            return {false, {}, error};
        }
        std::string text = openXmlToBankText(xml);
        if (!looksLikeBankText(text)) {
            return {false, {}, "Word 文档中没有可识别的题库段落"};
        }
        return {true, std::move(text), {}};
    }

    if (hasPrefix(bytes, std::string_view("\xEF\xBB\xBF", 3))) {
        if (looksLikeBankText(bytes.substr(3))) {
            return {true, bytes, {}};
        }
    } else if (hasPrefix(bytes, std::string_view("\xFF\xFE", 2))) {
        const std::string text = decodeUtf16(bytes, 2, true);
        if (looksLikeBankText(text)) {
            return {true, text, {}};
        }
    } else if (hasPrefix(bytes, std::string_view("\xFE\xFF", 2))) {
        const std::string text = decodeUtf16(bytes, 2, false);
        if (looksLikeBankText(text)) {
            return {true, text, {}};
        }
    }

    if (!isWordExtension(extension)) {
        return {true, bytes, {}};
    }
    if (looksLikeBankText(bytes) && looksLikePlainText(bytes)) {
        return {true, bytes, {}};
    }

    if (isOpenXmlWordExtension(extension)) {
        return {false, {}, "不是有效的 Word Open XML 文档"};
    }

    if (bytes.find("<html") != std::string::npos ||
        bytes.find("<HTML") != std::string::npos) {
        std::string text = htmlToBankText(bytes);
        if (looksLikeBankText(text)) {
            return {true, std::move(text), {}};
        }
    }
    if (bytes.find('<') != std::string::npos &&
        (bytes.find("[meta]") != std::string::npos ||
         bytes.find("[q]") != std::string::npos)) {
        std::string text = htmlToBankText(bytes);
        if (looksLikeBankText(text)) {
            return {true, std::move(text), {}};
        }
    }
    std::string recovered = recoverEmbeddedBankText(bytes);
    if (!recovered.empty()) {
        return {true, std::move(recovered), {}};
    }
    return {false, {}, "旧版二进制 .doc 中没有找到可恢复的题库文本"};
}

}  // namespace quiz::io

#include "SvgIconInternal.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace quiz::render::svg {

namespace {

using Attributes = std::map<std::string, std::string>;

/// 曲线打成折线的段数（图标够用）
constexpr int kCurveSegments = 18;

std::string trim(const std::string& text) {
    size_t begin = 0;
    size_t end = text.size();
    const std::string spaces = " \t\n\r";
    while (begin < end && spaces.find(text[begin]) != std::string::npos) {
        ++begin;
    }
    while (end > begin && spaces.find(text[end - 1]) != std::string::npos) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::vector<std::string> splitNumbers(const std::string& text) {
    std::vector<std::string> out;
    std::string current;
    for (char ch : text) {
        const bool isNumberChar = (ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '+' ||
                                  ch == 'e' || ch == 'E';
        if (isNumberChar) {
            current.push_back(ch);
        } else if (!current.empty()) {
            out.push_back(current);
            current.clear();
        }
    }
    if (!current.empty()) {
        out.push_back(current);
    }
    return out;
}

float toFloat(const std::string& text, float fallback = 0.0f) {
    if (text.empty()) {
        return fallback;
    }
    return static_cast<float>(std::atof(text.c_str()));
}

int hexDigit(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

bool parseColor(const std::string& raw, unsigned int paintRgb, unsigned int& outRgb, bool& outNone) {
    const std::string value = trim(raw);
    outNone = false;
    if (value.empty()) {
        return false;
    }
    if (value == "none" || value == "transparent") {
        outNone = true;
        return true;
    }
    if (value == "currentColor") {
        outRgb = paintRgb & 0xFFFFFFu;
        return true;
    }
    if (value[0] == '#') {
        const std::string hex = value.substr(1);
        if (hex.size() == 3) {
            unsigned int rgb = 0;
            for (int i = 0; i < 3; ++i) {
                const int digit = hexDigit(hex[static_cast<size_t>(i)]);
                if (digit < 0) {
                    return false;
                }
                rgb = (rgb << 8) | static_cast<unsigned int>(digit * 17);
            }
            outRgb = rgb;
            return true;
        }
        if (hex.size() == 6) {
            unsigned int rgb = 0;
            for (char ch : hex) {
                const int digit = hexDigit(ch);
                if (digit < 0) {
                    return false;
                }
                rgb = (rgb << 4) | static_cast<unsigned int>(digit);
            }
            outRgb = rgb;
            return true;
        }
        return false;
    }
    static const std::map<std::string, unsigned int> kNamed = {
        {"black", 0x000000},  {"white", 0xFFFFFF},  {"red", 0xFF0000},    {"green", 0x008000},
        {"lime", 0x00FF00},   {"blue", 0x0000FF},   {"gray", 0x808080},   {"grey", 0x808080},
        {"orange", 0xFFA500}, {"yellow", 0xFFFF00}, {"purple", 0x800080}, {"silver", 0xC0C0C0},
    };
    const auto it = kNamed.find(value);
    if (it != kNamed.end()) {
        outRgb = it->second;
        return true;
    }
    return false;
}

float attributeFloat(const Attributes& attrs, const char* name, float fallback) {
    const auto it = attrs.find(name);
    if (it == attrs.end()) {
        return fallback;
    }
    const std::vector<std::string> numbers = splitNumbers(it->second);
    return numbers.empty() ? fallback : toFloat(numbers[0], fallback);
}

float clamp01(float value) {
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

Matrix parseTransform(const std::string& text) {
    Matrix result;
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t open = text.find('(', pos);
        if (open == std::string::npos) {
            break;
        }
        const size_t close = text.find(')', open);
        if (close == std::string::npos) {
            break;
        }
        const std::string name = trim(text.substr(pos, open - pos));
        const std::vector<std::string> args = splitNumbers(text.substr(open + 1, close - open - 1));
        Matrix step;
        if (name == "translate" && !args.empty()) {
            step.e = toFloat(args[0]);
            step.f = args.size() > 1 ? toFloat(args[1]) : 0.0f;
        } else if (name == "scale" && !args.empty()) {
            step.a = toFloat(args[0], 1.0f);
            step.d = args.size() > 1 ? toFloat(args[1], 1.0f) : step.a;
        } else if (name == "rotate" && !args.empty()) {
            const float radians = toFloat(args[0]) * 3.14159265358979f / 180.0f;
            const float cs = std::cos(radians);
            const float sn = std::sin(radians);
            Matrix rotate;
            rotate.a = cs;
            rotate.b = sn;
            rotate.c = -sn;
            rotate.d = cs;
            if (args.size() >= 3) {
                const float cx = toFloat(args[1]);
                const float cy = toFloat(args[2]);
                Matrix toCenter;
                toCenter.e = cx;
                toCenter.f = cy;
                Matrix back;
                back.e = -cx;
                back.f = -cy;
                step = multiply(back, multiply(rotate, toCenter));
            } else {
                step = rotate;
            }
        } else if (name == "matrix" && args.size() >= 6) {
            step.a = toFloat(args[0], 1.0f);
            step.b = toFloat(args[1]);
            step.c = toFloat(args[2]);
            step.d = toFloat(args[3], 1.0f);
            step.e = toFloat(args[4]);
            step.f = toFloat(args[5]);
        }
        result = multiply(result, step);
        pos = close + 1;
    }
    return result;
}

Attributes parseAttributes(const std::string& text) {
    Attributes attrs;
    size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() &&
               (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n' || text[pos] == '\r')) {
            ++pos;
        }
        const size_t nameBegin = pos;
        while (pos < text.size() && text[pos] != '=' && text[pos] != ' ' && text[pos] != '\t' &&
               text[pos] != '\n' && text[pos] != '\r') {
            ++pos;
        }
        const std::string name = text.substr(nameBegin, pos - nameBegin);
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) {
            ++pos;
        }
        if (pos < text.size() && text[pos] == '=') {
            ++pos;
            while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) {
                ++pos;
            }
            if (pos < text.size() && (text[pos] == '"' || text[pos] == '\'')) {
                const char quote = text[pos++];
                const size_t valueBegin = pos;
                while (pos < text.size() && text[pos] != quote) {
                    ++pos;
                }
                const std::string value = text.substr(valueBegin, pos - valueBegin);
                if (!name.empty()) {
                    attrs[name] = value;
                }
                if (pos < text.size()) {
                    ++pos;
                }
            }
        } else if (!name.empty()) {
            attrs[name] = "";
        }
    }
    return attrs;
}

Style resolveStyle(const Style& parent, const Attributes& attrs, unsigned int paintRgb) {
    Style style = parent;
    float opacity = 1.0f;
    if (attrs.count("opacity")) {
        opacity = clamp01(attributeFloat(attrs, "opacity", 1.0f));
    }

    const auto fillIt = attrs.find("fill");
    if (fillIt != attrs.end()) {
        unsigned int rgb = 0;
        bool none = false;
        if (parseColor(fillIt->second, paintRgb, rgb, none)) {
            style.fill.enabled = !none;
            if (!none) {
                style.fill.rgb = rgb;
            }
        }
    }
    const auto strokeIt = attrs.find("stroke");
    if (strokeIt != attrs.end()) {
        unsigned int rgb = 0;
        bool none = false;
        if (parseColor(strokeIt->second, paintRgb, rgb, none)) {
            style.stroke.enabled = !none;
            if (!none) {
                style.stroke.rgb = rgb;
            }
        }
    }
    if (attrs.count("fill-opacity")) {
        style.fill.alpha *= clamp01(attributeFloat(attrs, "fill-opacity", 1.0f));
    }
    if (attrs.count("stroke-opacity")) {
        style.stroke.alpha *= clamp01(attributeFloat(attrs, "stroke-opacity", 1.0f));
    }
    style.fill.alpha *= opacity;
    style.stroke.alpha *= opacity;
    if (attrs.count("stroke-width")) {
        const std::vector<std::string> numbers = splitNumbers(attrs.at("stroke-width"));
        if (!numbers.empty()) {
            style.strokeWidth = toFloat(numbers[0], style.strokeWidth);
        }
    }
    const auto ruleIt = attrs.find("fill-rule");
    if (ruleIt != attrs.end()) {
        style.evenOdd = trim(ruleIt->second) == "evenodd";
    }
    const auto capIt = attrs.find("stroke-linecap");
    if (capIt != attrs.end()) {
        const std::string cap = trim(capIt->second);
        style.lineCap = cap == "round" ? 1 : (cap == "square" ? 2 : 0);
    }
    const auto joinIt = attrs.find("stroke-linejoin");
    if (joinIt != attrs.end()) {
        const std::string join = trim(joinIt->second);
        style.lineJoin = join == "miter" ? 0 : (join == "bevel" ? 2 : 1);
    }
    return style;
}

/// 椭圆弧 → 折线（按 SVG 规范的端点参数化转换成中心参数化）
void appendArc(const Matrix& matrix, const Point& from, const Point& to, float rx, float ry,
               float rotationDegrees, bool largeArc, bool sweep, std::vector<Point>& out) {
    constexpr float kPi = 3.14159265358979f;
    if (rx <= 0.0f || ry <= 0.0f || (from.x == to.x && from.y == to.y)) {
        out.push_back(apply(matrix, to));
        return;
    }
    const float phi = rotationDegrees * kPi / 180.0f;
    const float cosPhi = std::cos(phi);
    const float sinPhi = std::sin(phi);
    const float dx2 = (from.x - to.x) * 0.5f;
    const float dy2 = (from.y - to.y) * 0.5f;
    const float x1p = cosPhi * dx2 + sinPhi * dy2;
    const float y1p = -sinPhi * dx2 + cosPhi * dy2;

    // 半径不够大时按规范等比放大
    const float lambda = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
    if (lambda > 1.0f) {
        const float scale = std::sqrt(lambda);
        rx *= scale;
        ry *= scale;
    }
    const float rx2 = rx * rx;
    const float ry2 = ry * ry;
    const float denominator = rx2 * y1p * y1p + ry2 * x1p * x1p;
    float coefficient = 0.0f;
    if (denominator > 0.0f) {
        const float numerator = rx2 * ry2 - rx2 * y1p * y1p - ry2 * x1p * x1p;
        coefficient = std::sqrt(std::max(0.0f, numerator / denominator));
        if (largeArc == sweep) {
            coefficient = -coefficient;
        }
    }
    const float cxp = coefficient * rx * y1p / ry;
    const float cyp = -coefficient * ry * x1p / rx;
    const float cx = cosPhi * cxp - sinPhi * cyp + (from.x + to.x) * 0.5f;
    const float cy = sinPhi * cxp + cosPhi * cyp + (from.y + to.y) * 0.5f;

    const auto angleOf = [](float ux, float uy, float vx, float vy) {
        const float dot = ux * vx + uy * vy;
        const float length = std::sqrt((ux * ux + uy * uy) * (vx * vx + vy * vy));
        if (length <= 0.0f) {
            return 0.0f;
        }
        float angle = std::acos(std::max(-1.0f, std::min(1.0f, dot / length)));
        if (ux * vy - uy * vx < 0.0f) {
            angle = -angle;
        }
        return angle;
    };
    const float ux = (x1p - cxp) / rx;
    const float uy = (y1p - cyp) / ry;
    const float vx = (-x1p - cxp) / rx;
    const float vy = (-y1p - cyp) / ry;
    const float startAngle = angleOf(1.0f, 0.0f, ux, uy);
    float deltaAngle = angleOf(ux, uy, vx, vy);
    if (!sweep && deltaAngle > 0.0f) {
        deltaAngle -= 2.0f * kPi;
    } else if (sweep && deltaAngle < 0.0f) {
        deltaAngle += 2.0f * kPi;
    }

    const int segments =
        std::max(2, static_cast<int>(std::fabs(deltaAngle) / (kPi / 16.0f)) + 1);
    for (int i = 1; i <= segments; ++i) {
        const float angle = startAngle + deltaAngle * static_cast<float>(i) / segments;
        const float px = cx + rx * std::cos(angle) * cosPhi - ry * std::sin(angle) * sinPhi;
        const float py = cy + rx * std::cos(angle) * sinPhi + ry * std::sin(angle) * cosPhi;
        out.push_back(apply(matrix, Point{px, py}));
    }
}

void flattenCubic(const Matrix& matrix, const Point& p0, const Point& p1, const Point& p2,
                  const Point& p3, std::vector<Point>& out) {
    for (int i = 1; i <= kCurveSegments; ++i) {
        const float t = static_cast<float>(i) / kCurveSegments;
        const float u = 1.0f - t;
        const float x = u * u * u * p0.x + 3.0f * u * u * t * p1.x + 3.0f * u * t * t * p2.x +
                        t * t * t * p3.x;
        const float y = u * u * u * p0.y + 3.0f * u * u * t * p1.y + 3.0f * u * t * t * p2.y +
                        t * t * t * p3.y;
        out.push_back(apply(matrix, Point{x, y}));
    }
}

std::vector<Contour> parsePath(const std::string& d, const Matrix& matrix) {
    std::vector<Contour> contours;
    Contour current;
    Point cursor{0.0f, 0.0f};
    Point start{0.0f, 0.0f};
    Point lastControl{0.0f, 0.0f};
    char previousCommand = 0;
    size_t pos = 0;

    const auto skipSeparators = [&]() {
        while (pos < d.size() && (d[pos] == ' ' || d[pos] == ',' || d[pos] == '\t' || d[pos] == '\n' ||
                                  d[pos] == '\r')) {
            ++pos;
        }
    };
    const auto readNumber = [&](float& value) {
        skipSeparators();
        const size_t begin = pos;
        if (pos < d.size() && (d[pos] == '-' || d[pos] == '+')) {
            ++pos;
        }
        while (pos < d.size() && ((d[pos] >= '0' && d[pos] <= '9') || d[pos] == '.')) {
            ++pos;
        }
        if (pos < d.size() && (d[pos] == 'e' || d[pos] == 'E')) {
            ++pos;
            if (pos < d.size() && (d[pos] == '-' || d[pos] == '+')) {
                ++pos;
            }
            while (pos < d.size() && d[pos] >= '0' && d[pos] <= '9') {
                ++pos;
            }
        }
        if (pos == begin) {
            return false;
        }
        value = static_cast<float>(std::atof(d.substr(begin, pos - begin).c_str()));
        return true;
    };
    // large-arc-flag / sweep-flag 是单个数字，可能紧挨着别的数字写，单独读
    const auto readFlag = [&](int& value) {
        skipSeparators();
        if (pos >= d.size() || d[pos] < '0' || d[pos] > '9') {
            return false;
        }
        value = d[pos] == '1' ? 1 : 0;
        ++pos;
        return true;
    };
    const auto finishContour = [&]() {
        if (current.points.size() >= 2) {
            contours.push_back(current);
        }
        current = Contour{};
    };

    while (pos < d.size()) {
        skipSeparators();
        if (pos >= d.size()) {
            break;
        }
        char command = previousCommand;
        if ((d[pos] >= 'A' && d[pos] <= 'Z') || (d[pos] >= 'a' && d[pos] <= 'z')) {
            command = d[pos];
            ++pos;
        } else if (previousCommand == 0) {
            break;
        }
        const bool relative = command >= 'a' && command <= 'z';
        const char upper = static_cast<char>(relative ? command - 32 : command);
        previousCommand = command;

        if (upper == 'Z') {
            if (!current.points.empty()) {
                current.closed = true;
                current.points.push_back(apply(matrix, start));
            }
            finishContour();
            cursor = start;
            continue;
        }

        float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f, x3 = 0.0f, y3 = 0.0f;
        const Point base = relative ? cursor : Point{0.0f, 0.0f};
        if (upper == 'M' || upper == 'L' || upper == 'T') {
            if (!readNumber(x1) || !readNumber(y1)) {
                break;
            }
            const Point target{base.x + x1, base.y + y1};
            if (upper == 'M') {
                finishContour();
                start = target;
                current.points.push_back(apply(matrix, target));
            } else if (upper == 'T') {
                const Point control{2.0f * cursor.x - lastControl.x, 2.0f * cursor.y - lastControl.y};
                const float k = 2.0f / 3.0f;
                flattenCubic(matrix, cursor,
                             Point{cursor.x + (control.x - cursor.x) * k,
                                   cursor.y + (control.y - cursor.y) * k},
                             Point{target.x + (control.x - target.x) * k,
                                   target.y + (control.y - target.y) * k},
                             target, current.points);
                lastControl = control;
            } else {
                current.points.push_back(apply(matrix, target));
                lastControl = cursor;
            }
            cursor = target;
        } else if (upper == 'H' || upper == 'V') {
            if (!readNumber(x1)) {
                break;
            }
            const Point target = upper == 'H' ? Point{base.x + x1, cursor.y}
                                              : Point{cursor.x, base.y + x1};
            current.points.push_back(apply(matrix, target));
            cursor = target;
            lastControl = cursor;
        } else if (upper == 'C' || upper == 'S' || upper == 'Q') {
            const Point p0 = cursor;
            Point c1 = p0;
            Point c2 = p0;
            Point end = p0;
            if (upper == 'C') {
                if (!readNumber(x1) || !readNumber(y1) || !readNumber(x2) || !readNumber(y2) ||
                    !readNumber(x3) || !readNumber(y3)) {
                    break;
                }
                c1 = Point{base.x + x1, base.y + y1};
                c2 = Point{base.x + x2, base.y + y2};
                end = Point{base.x + x3, base.y + y3};
                lastControl = c2;
            } else if (upper == 'S') {
                if (!readNumber(x2) || !readNumber(y2) || !readNumber(x3) || !readNumber(y3)) {
                    break;
                }
                c1 = Point{2.0f * p0.x - lastControl.x, 2.0f * p0.y - lastControl.y};
                c2 = Point{base.x + x2, base.y + y2};
                end = Point{base.x + x3, base.y + y3};
                lastControl = c2;
            } else {
                if (!readNumber(x1) || !readNumber(y1) || !readNumber(x3) || !readNumber(y3)) {
                    break;
                }
                const Point control{base.x + x1, base.y + y1};
                end = Point{base.x + x3, base.y + y3};
                const float k = 2.0f / 3.0f;
                c1 = Point{p0.x + (control.x - p0.x) * k, p0.y + (control.y - p0.y) * k};
                c2 = Point{end.x + (control.x - end.x) * k, end.y + (control.y - end.y) * k};
                lastControl = control;
            }
            flattenCubic(matrix, p0, c1, c2, end, current.points);
            cursor = end;
        } else if (upper == 'A') {
            // 椭圆弧：rx ry x轴旋转 large-arc-flag sweep-flag x y（flag 是单个数字）
            float rx = 0.0f;
            float ry = 0.0f;
            float rotation = 0.0f;
            int largeArc = 0;
            int sweep = 0;
            if (!readNumber(rx) || !readNumber(ry) || !readNumber(rotation) ||
                !readFlag(largeArc) || !readFlag(sweep) || !readNumber(x1) || !readNumber(y1)) {
                break;
            }
            const Point target{base.x + x1, base.y + y1};
            appendArc(matrix, cursor, target, std::fabs(rx), std::fabs(ry), rotation,
                      largeArc != 0, sweep != 0, current.points);
            cursor = target;
            lastControl = cursor;
        } else {
            // 其它不支持的命令：把参数读掉，避免死循环
            while (readNumber(x1)) {
                const size_t afterX = pos;
                if (!readNumber(y1)) {
                    break;
                }
                if (afterX == pos) {
                    break;
                }
            }
        }
    }
    finishContour();
    return contours;
}

std::vector<Contour> shapeToContours(const std::string& tag, const Attributes& attrs,
                                     const Matrix& matrix) {
    std::vector<Contour> contours;
    if (tag == "rect") {
        const float x = attributeFloat(attrs, "x", 0.0f);
        const float y = attributeFloat(attrs, "y", 0.0f);
        const float w = attributeFloat(attrs, "width", 0.0f);
        const float h = attributeFloat(attrs, "height", 0.0f);
        if (w > 0.0f && h > 0.0f) {
            Contour contour;
            contour.closed = true;
            contour.points = {apply(matrix, Point{x, y}), apply(matrix, Point{x + w, y}),
                              apply(matrix, Point{x + w, y + h}), apply(matrix, Point{x, y + h}),
                              apply(matrix, Point{x, y})};
            contours.push_back(contour);
        }
    } else if (tag == "circle" || tag == "ellipse") {
        const float cx = attributeFloat(attrs, "cx", 0.0f);
        const float cy = attributeFloat(attrs, "cy", 0.0f);
        const float rx = tag == "circle" ? attributeFloat(attrs, "r", 0.0f)
                                         : attributeFloat(attrs, "rx", 0.0f);
        const float ry = tag == "circle" ? rx : attributeFloat(attrs, "ry", 0.0f);
        if (rx > 0.0f && ry > 0.0f) {
            Contour contour;
            contour.closed = true;
            const int segments = 48;
            for (int i = 0; i <= segments; ++i) {
                const float angle = static_cast<float>(i) / segments * 2.0f * 3.14159265358979f;
                contour.points.push_back(
                    apply(matrix, Point{cx + rx * std::cos(angle), cy + ry * std::sin(angle)}));
            }
            contours.push_back(contour);
        }
    } else if (tag == "line") {
        Contour contour;
        contour.points = {
            apply(matrix, Point{attributeFloat(attrs, "x1", 0.0f), attributeFloat(attrs, "y1", 0.0f)}),
            apply(matrix, Point{attributeFloat(attrs, "x2", 0.0f), attributeFloat(attrs, "y2", 0.0f)})};
        contours.push_back(contour);
    } else if (tag == "polyline" || tag == "polygon") {
        const auto it = attrs.find("points");
        if (it != attrs.end()) {
            const std::vector<std::string> numbers = splitNumbers(it->second);
            Contour contour;
            for (size_t i = 0; i + 1 < numbers.size(); i += 2) {
                contour.points.push_back(
                    apply(matrix, Point{toFloat(numbers[i]), toFloat(numbers[i + 1])}));
            }
            contour.closed = (tag == "polygon");
            if (contour.closed && !contour.points.empty()) {
                contour.points.push_back(contour.points.front());
            }
            if (contour.points.size() >= 2) {
                contours.push_back(contour);
            }
        }
    }
    return contours;
}

}  // namespace

Matrix multiply(const Matrix& m, const Matrix& n) {
    Matrix out;
    out.a = m.a * n.a + m.b * n.c;
    out.b = m.a * n.b + m.b * n.d;
    out.c = m.c * n.a + m.d * n.c;
    out.d = m.c * n.b + m.d * n.d;
    out.e = m.e * n.a + m.f * n.c + n.e;
    out.f = m.e * n.b + m.f * n.d + n.f;
    return out;
}

Point apply(const Matrix& m, const Point& p) {
    return Point{m.a * p.x + m.c * p.y + m.e, m.b * p.x + m.d * p.y + m.f};
}

bool parseDocument(const std::string& svgText, unsigned int paintRgb, Document& out,
                   std::string& outError) {
    out = Document{};
    outError.clear();

    Style rootStyle;
    rootStyle.fill.enabled = true;
    rootStyle.fill.rgb = paintRgb & 0xFFFFFFu;
    std::vector<Style> styleStack{rootStyle};
    std::vector<Matrix> matrixStack{Matrix{}};

    size_t pos = 0;
    while (pos < svgText.size()) {
        const size_t open = svgText.find('<', pos);
        if (open == std::string::npos) {
            break;
        }
        if (svgText.compare(open, 4, "<!--") == 0) {
            const size_t close = svgText.find("-->", open);
            pos = close == std::string::npos ? svgText.size() : close + 3;
            continue;
        }
        if (open + 1 >= svgText.size()) {
            break;
        }
        if (svgText[open + 1] == '?' || svgText[open + 1] == '!') {
            const size_t close = svgText.find('>', open);
            pos = close == std::string::npos ? svgText.size() : close + 1;
            continue;
        }
        const size_t close = svgText.find('>', open);
        if (close == std::string::npos) {
            break;
        }
        std::string inner = svgText.substr(open + 1, close - open - 1);
        bool selfClosing = false;
        if (!inner.empty() && inner.back() == '/') {
            selfClosing = true;
            inner.pop_back();
        }
        const bool closingTag = !inner.empty() && inner[0] == '/';
        if (closingTag) {
            inner.erase(0, 1);
        }
        const size_t nameEnd = inner.find_first_of(" \t\n\r");
        const std::string tag = nameEnd == std::string::npos ? inner : inner.substr(0, nameEnd);
        const Attributes attrs =
            nameEnd == std::string::npos ? Attributes{} : parseAttributes(inner.substr(nameEnd));
        pos = close + 1;

        if (closingTag) {
            if ((tag == "g" || tag == "svg" || tag == "defs") && styleStack.size() > 1) {
                styleStack.pop_back();
                matrixStack.pop_back();
            }
            continue;
        }

        if (tag == "svg") {
            if (attrs.count("viewBox")) {
                const std::vector<std::string> numbers = splitNumbers(attrs.at("viewBox"));
                if (numbers.size() >= 4) {
                    for (int i = 0; i < 4; ++i) {
                        out.viewBox[i] = toFloat(numbers[static_cast<size_t>(i)]);
                    }
                    out.hasViewBox = true;
                }
            }
            if (attrs.count("width")) {
                const std::vector<std::string> numbers = splitNumbers(attrs.at("width"));
                if (!numbers.empty()) {
                    out.width = toFloat(numbers[0]);
                }
            }
            if (attrs.count("height")) {
                const std::vector<std::string> numbers = splitNumbers(attrs.at("height"));
                if (!numbers.empty()) {
                    out.height = toFloat(numbers[0]);
                }
            }
            if (!selfClosing) {
                styleStack.push_back(resolveStyle(styleStack.back(), attrs, paintRgb));
                matrixStack.push_back(matrixStack.back());
            }
            continue;
        }

        if (tag == "g" || tag == "defs") {
            Matrix matrix = matrixStack.back();
            if (attrs.count("transform")) {
                matrix = multiply(matrix, parseTransform(attrs.at("transform")));
            }
            if (!selfClosing) {
                styleStack.push_back(resolveStyle(styleStack.back(), attrs, paintRgb));
                matrixStack.push_back(matrix);
            }
            continue;
        }

        const bool drawable = (tag == "path" || tag == "rect" || tag == "circle" || tag == "ellipse" ||
                               tag == "line" || tag == "polyline" || tag == "polygon");
        if (!drawable) {
            continue;
        }
        Matrix matrix = matrixStack.back();
        if (attrs.count("transform")) {
            matrix = multiply(matrix, parseTransform(attrs.at("transform")));
        }
        const Style style = resolveStyle(styleStack.back(), attrs, paintRgb);

        std::vector<Contour> contours;
        if (tag == "path") {
            const auto it = attrs.find("d");
            if (it != attrs.end()) {
                contours = parsePath(it->second, matrix);
            }
        } else {
            contours = shapeToContours(tag, attrs, matrix);
        }
        if (contours.empty()) {
            continue;
        }

        Shape shape;
        shape.style = style;
        if (style.fill.enabled) {
            shape.fillContours = contours;
        }
        if (style.stroke.enabled) {
            shape.strokeContours = buildStrokeContours(contours, style.strokeWidth, style.lineCap);
        }
        if (!shape.fillContours.empty() || !shape.strokeContours.empty()) {
            out.shapes.push_back(shape);
        }
    }

    if (out.shapes.empty()) {
        outError = "SVG 里没有可绘制的内容（标签/属性可能都不支持）";
        return false;
    }
    return true;
}

}  // namespace quiz::render::svg

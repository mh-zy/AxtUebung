#include "SvgIconInternal.h"

#include "quiz/render/SvgIcon.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace quiz::render::svg {

namespace {

/// 超采样倍数（3×3 就够了，图标不需要更好的抗锯齿）
constexpr int kSuperSample = 3;
constexpr float kPi = 3.14159265358979f;

float clamp01(float value) {
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

/// 把一层覆盖率按颜色合成到画布上（画布是预乘 alpha）
void compositeLayer(std::vector<uint32_t>& target, const std::vector<unsigned char>& coverage,
                    const Paint& paint, bool keepStrongest) {
    const float baseAlpha = clamp01(paint.alpha);
    if (baseAlpha <= 0.0f) {
        return;
    }
    // paint.rgb 是 0xRRGGBB（SVG 里的 #rrggbb 和 currentColor 都是这个约定），
    // 画布是「低字节 = R」。早先按 BGR 取字节，R/B 被换掉，
    // 青色 #00B4D8 会画成金色（白/灰图标看不出来，只有彩色图标才露馅）。
    const uint32_t r = (paint.rgb >> 16) & 0xFF;
    const uint32_t g = (paint.rgb >> 8) & 0xFF;
    const uint32_t b = paint.rgb & 0xFF;
    for (size_t i = 0; i < target.size(); ++i) {
        if (coverage[i] == 0) {
            continue;
        }
        const uint32_t alpha = static_cast<uint32_t>(static_cast<float>(coverage[i]) * baseAlpha + 0.5f);
        if (alpha == 0) {
            continue;
        }
        // 同一层的重叠（描边的四边形与圆点）只画最实的那一次，免得越叠越深
        if (keepStrongest && (target[i] >> 24) >= alpha) {
            continue;
        }
        const uint32_t previous = target[i];
        const uint32_t previousAlpha = previous >> 24;
        if (keepStrongest) {
            // 这一层内部重叠：直接用较实的那个覆盖率覆盖，不做二次混合
            const uint32_t outA = std::max(previousAlpha, alpha);
            target[i] = (r * alpha / 255) | ((g * alpha / 255) << 8) | ((b * alpha / 255) << 16) |
                        (outA << 24);
            continue;
        }
        const uint32_t inv = 255 - alpha;
        const uint32_t outR = r * alpha / 255 + (previous & 0xFF) * inv / 255;
        const uint32_t outG = g * alpha / 255 + ((previous >> 8) & 0xFF) * inv / 255;
        const uint32_t outB = b * alpha / 255 + ((previous >> 16) & 0xFF) * inv / 255;
        uint32_t outA = alpha + previousAlpha * inv / 255;
        if (outA > 255) {
            outA = 255;
        }
        target[i] = outR | (outG << 8) | (outB << 16) | (outA << 24);
    }
}

}  // namespace

std::vector<Contour> buildStrokeContours(const std::vector<Contour>& source, float width, int cap) {
    std::vector<Contour> out;
    const float half = std::max(width, 0.05f) * 0.5f;
    const int circleSegments = 12;

    const auto addDisc = [&](const Point& center) {
        Contour disc;
        disc.closed = true;
        for (int i = 0; i <= circleSegments; ++i) {
            const float angle = static_cast<float>(i) / circleSegments * 2.0f * kPi;
            disc.points.push_back(
                Point{center.x + half * std::cos(angle), center.y + half * std::sin(angle)});
        }
        out.push_back(disc);
    };

    for (const Contour& contour : source) {
        for (size_t i = 0; i + 1 < contour.points.size(); ++i) {
            const Point p0 = contour.points[i];
            const Point p1 = contour.points[i + 1];
            float dx = p1.x - p0.x;
            float dy = p1.y - p0.y;
            const float length = std::sqrt(dx * dx + dy * dy);
            if (length < 0.0001f) {
                continue;
            }
            dx /= length;
            dy /= length;
            const float nx = -dy * half;
            const float ny = dx * half;
            const float extend = (cap == 2) ? half : 0.0f;   // square cap 往外延伸
            const Point a{p0.x - dx * extend + nx, p0.y - dy * extend + ny};
            const Point b{p1.x + dx * extend + nx, p1.y + dy * extend + ny};
            const Point c{p1.x + dx * extend - nx, p1.y + dy * extend - ny};
            const Point d{p0.x - dx * extend - nx, p0.y - dy * extend - ny};
            Contour quad;
            quad.closed = true;
            quad.points = {a, b, c, d, a};
            out.push_back(quad);
            if (i > 0) {
                addDisc(p0);   // 拐角补圆，省掉斜接计算
            }
            if (i + 2 == contour.points.size()) {
                addDisc(p1);   // 末端
            }
            if (cap == 1 && i == 0) {
                addDisc(p0);   // 圆头起点
            }
        }
    }
    return out;
}

void rasterizeContours(const std::vector<Contour>& contours, bool evenOdd,
                       std::vector<unsigned char>& coverage, int width, int height) {
    struct Edge {
        float x0, y0, x1, y1;
        int winding;
    };
    std::vector<Edge> edges;
    for (const Contour& contour : contours) {
        if (contour.points.size() < 2) {
            continue;
        }
        for (size_t i = 0; i + 1 < contour.points.size(); ++i) {
            const Point& p0 = contour.points[i];
            const Point& p1 = contour.points[i + 1];
            if (p0.y == p1.y) {
                continue;
            }
            Edge edge;
            edge.x0 = p0.x;
            edge.y0 = p0.y;
            edge.x1 = p1.x;
            edge.y1 = p1.y;
            edge.winding = p1.y > p0.y ? 1 : -1;
            edges.push_back(edge);
        }
    }
    if (edges.empty()) {
        return;
    }

    std::vector<std::pair<float, int>> crossings;
    for (int y = 0; y < height; ++y) {
        const float sampleY = static_cast<float>(y) + 0.5f;
        crossings.clear();
        for (const Edge& edge : edges) {
            const float minY = std::min(edge.y0, edge.y1);
            const float maxY = std::max(edge.y0, edge.y1);
            if (sampleY < minY || sampleY >= maxY) {
                continue;
            }
            const float t = (sampleY - edge.y0) / (edge.y1 - edge.y0);
            crossings.push_back({edge.x0 + (edge.x1 - edge.x0) * t, edge.winding});
        }
        if (crossings.empty()) {
            continue;
        }
        std::sort(crossings.begin(), crossings.end(),
                  [](const std::pair<float, int>& a, const std::pair<float, int>& b) {
                      return a.first < b.first;
                  });

        int winding = 0;
        for (size_t i = 0; i + 1 < crossings.size(); ++i) {
            winding += crossings[i].second;
            const bool inside = evenOdd ? ((i % 2) == 0) : (winding != 0);
            if (!inside) {
                continue;
            }
            int x0 = static_cast<int>(std::floor(crossings[i].first));
            int x1 = static_cast<int>(std::ceil(crossings[i + 1].first));
            x0 = std::max(x0, 0);
            x1 = std::min(x1, width);
            if (x1 <= x0) {
                continue;
            }
            unsigned char* row = coverage.data() + static_cast<size_t>(y) * width;
            for (int x = x0; x < x1; ++x) {
                row[x] = 255;
            }
        }
    }
}

bool rasterizeImpl(const std::string& svgText, int sizePx, float inkFraction, unsigned int paintRgb,
                   Image& out, std::string& outError) {
    out = Image{};
    outError.clear();

    Document document;
    if (!parseDocument(svgText, paintRgb, document, outError)) {
        return false;
    }

    // ---- 目标尺寸 & viewBox → 像素 ----
    int size = sizePx;
    if (size <= 0) {
        size = static_cast<int>(std::max(document.width, document.height) + 0.5f);
    }
    if (size <= 0) {
        size = 64;
    }
    float boxX = 0.0f;
    float boxY = 0.0f;
    float boxW = 0.0f;
    float boxH = 0.0f;
    if (document.hasViewBox && document.viewBox[2] > 0.0f && document.viewBox[3] > 0.0f) {
        boxX = document.viewBox[0];
        boxY = document.viewBox[1];
        boxW = document.viewBox[2];
        boxH = document.viewBox[3];
    } else {
        boxW = document.width > 0.0f ? document.width : static_cast<float>(size);
        boxH = document.height > 0.0f ? document.height : static_cast<float>(size);
    }
    // ---- viewBox → 超采样画布 ----
    // 几何要直接投到「超采样画布」的坐标系（ssSize × ssSize）上。
    // 早先是投到 size 坐标系、再画进 ssSize 的画布，于是每个图标都只有目标尺寸的
    // 1/kSuperSample，还死死贴在左上角；ImGui 又按整张纹理居中，看着就是「又小又偏」。
    const int ssSize = size * kSuperSample;
    const float scale =
        std::min(static_cast<float>(ssSize) / boxW, static_cast<float>(ssSize) / boxH);
    Matrix toPixels;
    toPixels.a = scale;
    toPixels.d = scale;
    toPixels.e = (static_cast<float>(ssSize) - boxW * scale) * 0.5f - boxX * scale;
    toPixels.f = (static_cast<float>(ssSize) - boxH * scale) * 0.5f - boxY * scale;
    for (Shape& shape : document.shapes) {
        std::vector<Contour>* lists[2] = {&shape.fillContours, &shape.strokeContours};
        for (std::vector<Contour>* list : lists) {
            for (Contour& contour : *list) {
                for (Point& point : contour.points) {
                    point = apply(toPixels, point);
                }
            }
        }
    }

    // ---- 按「墨迹」归一化（可选）----
    // 不同 SVG 在自家画布里留白差很多（chevron 只占一条线，标签图标几乎占满），
    // 不归一化就会出现「同一个按钮尺寸下，图标有大有小」。
    // inkFraction > 0 时：量出真正画出来的外接矩形，把它缩放并居中到 ssSize * inkFraction。
    if (inkFraction > 0.0f) {
        float minX = 0.0f;
        float minY = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;
        bool first = true;
        for (const Shape& shape : document.shapes) {
            const std::vector<Contour>* lists[2] = {&shape.fillContours, &shape.strokeContours};
            for (const std::vector<Contour>* list : lists) {
                for (const Contour& contour : *list) {
                    for (const Point& point : contour.points) {
                        if (first) {
                            minX = maxX = point.x;
                            minY = maxY = point.y;
                            first = false;
                        } else {
                            minX = std::min(minX, point.x);
                            minY = std::min(minY, point.y);
                            maxX = std::max(maxX, point.x);
                            maxY = std::max(maxY, point.y);
                        }
                    }
                }
            }
        }
        const float inkW = maxX - minX;
        const float inkH = maxY - minY;
        const float inkSide = std::max(inkW, inkH);
        if (!first && inkSide > 0.5f) {
            const float target = static_cast<float>(ssSize) * inkFraction;
            const float fitScale = target / inkSide;
            Matrix fit;
            fit.a = fitScale;
            fit.d = fitScale;
            fit.e = (static_cast<float>(ssSize) - inkW * fitScale) * 0.5f - minX * fitScale;
            fit.f = (static_cast<float>(ssSize) - inkH * fitScale) * 0.5f - minY * fitScale;
            for (Shape& shape : document.shapes) {
                std::vector<Contour>* lists[2] = {&shape.fillContours, &shape.strokeContours};
                for (std::vector<Contour>* list : lists) {
                    for (Contour& contour : *list) {
                        for (Point& point : contour.points) {
                            point = apply(fit, point);
                        }
                    }
                }
            }
        }
    }

    // ---- 超采样光栅化 ----
    std::vector<uint32_t> canvas(static_cast<size_t>(ssSize) * ssSize, 0u);
    std::vector<unsigned char> coverage(static_cast<size_t>(ssSize) * ssSize, 0);
    for (const Shape& shape : document.shapes) {
        if (!shape.fillContours.empty()) {
            std::fill(coverage.begin(), coverage.end(), static_cast<unsigned char>(0));
            rasterizeContours(shape.fillContours, shape.style.evenOdd, coverage, ssSize, ssSize);
            compositeLayer(canvas, coverage, shape.style.fill, false);
        }
        if (!shape.strokeContours.empty()) {
            std::fill(coverage.begin(), coverage.end(), static_cast<unsigned char>(0));
            for (const Contour& piece : shape.strokeContours) {
                std::vector<Contour> single{piece};
                rasterizeContours(single, false, coverage, ssSize, ssSize);
            }
            compositeLayer(canvas, coverage, shape.style.stroke, true);
        }
    }

    // ---- 降采样 ----
    out.width = size;
    out.height = size;
    out.pixels.assign(static_cast<size_t>(size) * size, 0u);
    const float inv = 1.0f / static_cast<float>(kSuperSample * kSuperSample);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            uint32_t sumR = 0;
            uint32_t sumG = 0;
            uint32_t sumB = 0;
            uint32_t sumA = 0;
            for (int sy = 0; sy < kSuperSample; ++sy) {
                const int row = y * kSuperSample + sy;
                for (int sx = 0; sx < kSuperSample; ++sx) {
                    const uint32_t pixel =
                        canvas[static_cast<size_t>(row) * ssSize + x * kSuperSample + sx];
                    sumR += pixel & 0xFF;
                    sumG += (pixel >> 8) & 0xFF;
                    sumB += (pixel >> 16) & 0xFF;
                    sumA += (pixel >> 24) & 0xFF;
                }
            }
            const uint32_t r = static_cast<uint32_t>(static_cast<float>(sumR) * inv + 0.5f);
            const uint32_t g = static_cast<uint32_t>(static_cast<float>(sumG) * inv + 0.5f);
            const uint32_t b = static_cast<uint32_t>(static_cast<float>(sumB) * inv + 0.5f);
            const uint32_t a = static_cast<uint32_t>(static_cast<float>(sumA) * inv + 0.5f);
            out.pixels[static_cast<size_t>(y) * size + x] = r | (g << 8) | (b << 16) | (a << 24);
        }
    }
    return true;
}

bool rasterize(const std::string& svgText, int sizePx, unsigned int paintRgb, Image& out,
               std::string& outError) {
    return rasterizeImpl(svgText, sizePx, 0.0f, paintRgb, out, outError);
}

bool rasterizeFitted(const std::string& svgText, int sizePx, float inkFraction,
                     unsigned int paintRgb, Image& out, std::string& outError) {
    return rasterizeImpl(svgText, sizePx, inkFraction, paintRgb, out, outError);
}

}  // namespace quiz::render::svg

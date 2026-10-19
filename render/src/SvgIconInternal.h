#pragma once

#include <string>
#include <vector>

// SVG 光栅化器的内部共享类型（解析在 SvgIcon.cpp，光栅化在 SvgRaster.cpp）
namespace quiz::render::svg {

struct Point {
    float x = 0.0f;
    float y = 0.0f;
};

/// 2×3 仿射矩阵：x' = a*x + c*y + e, y' = b*x + d*y + f
struct Matrix {
    float a = 1.0f, b = 0.0f, c = 0.0f, d = 1.0f, e = 0.0f, f = 0.0f;
};

Matrix multiply(const Matrix& m, const Matrix& n);
Point apply(const Matrix& m, const Point& p);

/// 一条轮廓（曲线已打成折线，坐标已按变换展开）
struct Contour {
    std::vector<Point> points;
    bool closed = false;
};

struct Paint {
    bool enabled = false;
    unsigned int rgb = 0x000000;   // 0xRRGGBB
    float alpha = 1.0f;
};

struct Style {
    Paint fill;
    Paint stroke;
    float strokeWidth = 1.0f;
    bool evenOdd = false;
    int lineCap = 0;    // 0=butt 1=round 2=square
    int lineJoin = 0;   // 0=miter 1=round 2=bevel（这里按圆角处理，图标够用）
};

/// 一个待绘制图形
struct Shape {
    std::vector<Contour> fillContours;
    std::vector<Contour> strokeContours;
    Style style;
};

/// 解析结果
struct Document {
    std::vector<Shape> shapes;
    float viewBox[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    bool hasViewBox = false;
    float width = 0.0f;
    float height = 0.0f;
};

/// 解析 SVG 文本（失败时返回 false 并写 outError）
bool parseDocument(const std::string& svgText, unsigned int paintRgb, Document& out,
                   std::string& outError);

/// 描边：把每条折线扩成四边形 + 拐角/端点补圆点
std::vector<Contour> buildStrokeContours(const std::vector<Contour>& source, float width, int cap);

/// 扫描线填充（even-odd / nonzero），写入覆盖率缓冲（超采样分辨率）
void rasterizeContours(const std::vector<Contour>& contours, bool evenOdd,
                       std::vector<unsigned char>& coverage, int width, int height);

}  // namespace quiz::render::svg

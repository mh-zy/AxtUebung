#pragma once

#include <cstdint>
#include <string>
#include <vector>

// ============================================================================
// 极简 SVG 光栅化器（给图标用，不是完整 SVG 实现）
// ----------------------------------------------------------------------------
// 支持：
//   * <svg viewBox / width / height>：按 viewBox 等比缩放并居中到目标尺寸
//   * <g>（含 transform，属性可继承）、<path>、<rect>、<circle>、<ellipse>、
//     <line>、<polyline>、<polygon>
//   * path 的 d：M m L l H h V v C c S s Q q T t A a（椭圆弧会转成折线）Z z
//   * fill / fill-rule(nonzero|evenodd) / stroke / stroke-width /
//     stroke-linecap(butt|round|square) / stroke-linejoin /
//     opacity / fill-opacity / stroke-opacity
//   * transform：translate / scale / rotate / matrix（可叠加）
//   * 颜色：#rgb、#rrggbb、none、currentColor（用调用方给的颜色）、少量常用色名
// 不支持：渐变、图案、滤镜、蒙版、<text>、<use>、<style>/CSS。
//         椭圆弧 A 会转成折线；不认识的标记/属性被忽略，不报错。
//
// 输出：sizePx × sizePx 的 RGBA 像素，格式 0xAABBGGRR（与帧缓冲一致），
//       **已预乘 alpha** —— 软件光栅化器的混合公式是 src*a + dst*(1-a)，
//       按预乘写入才能得到正确的边缘过渡。
// ============================================================================
namespace quiz::render::svg {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels;   // 0xAABBGGRR，预乘 alpha
};

/// 把 SVG 文本光栅化成 sizePx × sizePx 的图标
/// @param sizePx   目标边长；<=0 时用 SVG 自身尺寸
/// @param paintRgb currentColor / 未写颜色时的颜色（0xRRGGBB）
bool rasterize(const std::string& svgText, int sizePx, unsigned int paintRgb, Image& out,
               std::string& outError);

/// 同上，但把图标「真正画出来的外接矩形」缩放并居中，使最长边占 sizePx * inkFraction。
/// 不同 SVG 在自家画布里留白差别很大，同一套按钮里必须归一化，
/// 否则会出现「同一个按钮尺寸下有的图标很大、有的很小」。
/// @param inkFraction 0.1 ～ 1.0；<=0 时和 rasterize() 完全一样
bool rasterizeFitted(const std::string& svgText, int sizePx, float inkFraction,
                     unsigned int paintRgb, Image& out, std::string& outError);

}  // namespace quiz::render::svg


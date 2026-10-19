#include "quiz/render/ImGuiRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include "quiz/platform/Platform.h"
#include "quiz/render/SvgIcon.h"
#include "quiz/util/Log.h"

namespace quiz::render {

namespace {

// ---------------------------------------------------------------------------
// 纹理：把 ImGui 的 ImTextureData 复制到我们自己的内存里
// ---------------------------------------------------------------------------
struct Texture {
    int width = 0;
    int height = 0;
    bool isGray = false;             // 单通道（Alpha8）字体图集
    std::vector<uint32_t> pixels;   // RGBA8（内存顺序 R,G,B,A）
};

std::vector<Texture> g_textures;

/// 图标纹理缓存：key = 资源名#尺寸#颜色
std::map<std::string, ImTextureID> g_iconCache;
ImFont* g_font = nullptr;
bool g_usingSystemFont = false;
std::string g_fontPath;
float g_fontScale = 1.0f;

/// 待处理的「按下 / 抬起」边沿。
/// 为什么排队：ImGui 的事件队列在同一帧里先收到「按下」再收到「抬起」，到 NewFrame 时
/// 只剩「没按下」→ MouseClicked 不成立，这一下点击就丢了。安卓的 input tap 只有几毫秒，
/// DOWN 和 UP 经常被同一次 poll 一起收走（实测点一次没反应，多选题点第二个选项也没反应）。
/// 所以边沿排队、每帧只送一个进去；移动只保留最新位置，拖动不会因此变卡。
///
/// 滚动也依赖这条链路：UiScroll 要在按下帧记录“是否落在控件上”，后续帧还要看到
/// MouseDragging / MouseDelta。若 DOWN 与 UP 合并，短点击会消失；若 MOVE 被压掉，
/// 拖动距离不够 6px，列表同样不会滚。这里的边沿队列与最新位置缓存缺一不可。
std::deque<PointerEvent> g_pendingEdges;
bool g_hasPendingMove = false;
float g_pendingMoveX = 0.0f;
float g_pendingMoveY = 0.0f;
bool g_pendingMoveIsTouch = true;
uint64_t g_pendingMoveSequence = 0;
uint64_t g_nextPointerSequence = 1;


Texture* textureFromId(ImTextureID id) {
    const intptr_t index = static_cast<intptr_t>(id);
    if (index <= 0 || index > static_cast<intptr_t>(g_textures.size())) {
        return nullptr;
    }
    return &g_textures[static_cast<size_t>(index - 1)];
}

ImTextureID idFromTextureIndex(size_t index) {
    return static_cast<ImTextureID>(static_cast<intptr_t>(index + 1));
}

/// 处理 ImGui 1.92 的动态纹理请求（创建/局部更新/销毁）
void updateTexture(ImTextureData* textureData) {
    switch (textureData->Status) {
    case ImTextureStatus_WantCreate: {
        Texture texture;
        texture.width = textureData->Width;
        texture.height = textureData->Height;
        texture.isGray = (textureData->BytesPerPixel != 4);
        const size_t count = static_cast<size_t>(texture.width) * texture.height;
        texture.pixels.resize(count);   // 不再先 assign 一遍白色

        const uint8_t* source = static_cast<const uint8_t*>(textureData->GetPixels());
        const int bpp = textureData->BytesPerPixel;

        if (bpp == 1) {
            // Alpha8：逐行展开成 0x00FFFFFF | (a << 24)
            for (int y = 0; y < texture.height; ++y) {
                const uint8_t* srcRow = source + static_cast<size_t>(y) * texture.width;
                uint32_t* dstRow = texture.pixels.data() + static_cast<size_t>(y) * texture.width;
                for (int x = 0; x < texture.width; ++x) {
                    dstRow[x] = 0x00FFFFFFu | (static_cast<uint32_t>(srcRow[x]) << 24);
                }
            }
        }
        else if (bpp == 4) {
            // RGBA8：字节序一致，直接整块拷贝
            std::memcpy(texture.pixels.data(), source, count * 4);
        }
        else {
            for (int y = 0; y < texture.height; ++y) {
                for (int x = 0; x < texture.width; ++x) {
                    const uint8_t* pixel =
                        source + (static_cast<size_t>(y) * texture.width + x) * bpp;
                    texture.pixels[static_cast<size_t>(y) * texture.width + x] =
                        0x00FFFFFFu | (static_cast<uint32_t>(pixel[0]) << 24);
                }
            }
        }
        g_textures.push_back(std::move(texture));
        const size_t index = g_textures.size() - 1;
        textureData->SetTexID(idFromTextureIndex(index));
        textureData->SetStatus(ImTextureStatus_OK);
        logInfo("字体纹理已创建：%d x %d（%d 字节/像素）", textureData->Width, textureData->Height, bpp);
        break;
    }
    case ImTextureStatus_WantUpdates: {
        Texture* texture = textureFromId(textureData->TexID);
        if (texture == nullptr) {
            textureData->SetStatus(ImTextureStatus_Destroyed);
            break;
        }
        const uint8_t* source = static_cast<const uint8_t*>(textureData->GetPixels());
        const int bpp = textureData->BytesPerPixel;
        for (const ImTextureRect& rect : textureData->Updates) {
            const int rx = rect.x, ry = rect.y, rw = rect.w, rh = rect.h;
            const int maxY = std::min(ry + rh, texture->height);
            const int maxX = std::min(rx + rw, texture->width);
            for (int y = ry; y < maxY; ++y) {
                const uint8_t* srcRow =
                    source + (static_cast<size_t>(y) * textureData->Width + rx) * bpp;
                uint32_t* dstRow = texture->pixels.data() + static_cast<size_t>(y) * texture->width + rx;
                const int w = maxX - rx;
                if (bpp == 1) {
                    for (int x = 0; x < w; ++x) {
                        dstRow[x] = 0x00FFFFFFu | (static_cast<uint32_t>(srcRow[x]) << 24);
                    }
                }
                else if (bpp == 4) {
                    std::memcpy(dstRow, srcRow, static_cast<size_t>(w) * 4);
                }
                else {
                    for (int x = 0; x < w; ++x) {
                        dstRow[x] = 0x00FFFFFFu | (static_cast<uint32_t>(srcRow[x * bpp]) << 24);
                    }
                }
            }
        }
        textureData->SetStatus(ImTextureStatus_OK);
        break;
    }
    }
}

// ---------------------------------------------------------------------------
// 光栅化：把 ImGui 的三角形画进 RGBA8 帧缓冲
// ---------------------------------------------------------------------------
inline float edgeFunction(float ax, float ay, float bx, float by, float px, float py) {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

inline int clampi(int value, int low, int high) {
    return value < low ? low : (value > high ? high : value);
}

/// 快速除以 255：等价于 floor(x / 255)，但只有加法与移位。
/// 光栅化内层每个像素原本要做 7 次整数除法（modulate 4 次 + 混合 3 次），
/// 在 Debug/Release 下这都是一大笔开销，换成这个能省掉大头。
inline uint32_t div255(uint32_t value) {
    return (value + 1 + (value >> 8)) >> 8;
}

inline uint32_t mul255(uint32_t a, uint32_t b) {
    return div255(a * b);
}

/// 把一个像素混合进帧缓冲：srcA=0 直接跳过，srcA=255 直接覆盖（省掉一次读和三次乘加）
inline void blendPixel(uint32_t& dst, uint32_t srcR, uint32_t srcG, uint32_t srcB, uint32_t srcA) {
    if (srcA == 0) {
        return;
    }
    if (srcA == 255) {
        dst = srcR | (srcG << 8) | (srcB << 16) | 0xFF000000u;
        return;
    }
    const uint32_t invA = 255u - srcA;
    dst = div255(srcR * srcA + (dst & 0xFF) * invA) |
          (div255(srcG * srcA + ((dst >> 8) & 0xFF) * invA) << 8) |
          (div255(srcB * srcA + ((dst >> 16) & 0xFF) * invA) << 16) | 0xFF000000u;
}

/// 三个顶点颜色按重心权重插值（w0 + w1 + w2 = 1）
inline uint32_t interpolateColor(uint32_t c0, uint32_t c1, uint32_t c2, float w0, float w1, float w2) {
    const auto mix = [&](int shift) -> uint32_t {
        const float a = static_cast<float>((c0 >> shift) & 0xFF);
        const float b = static_cast<float>((c1 >> shift) & 0xFF);
        const float c = static_cast<float>((c2 >> shift) & 0xFF);
        const float value = a * w0 + b * w1 + c * w2;
        return static_cast<uint32_t>(value <= 0.0f ? 0.0f : (value >= 255.0f ? 255.0f : value));
    };
    return mix(0) | (mix(8) << 8) | (mix(16) << 16) | (mix(24) << 24);
}

uint32_t sampleTexture(const Texture& texture, float u, float v) {
    if (texture.width <= 0 || texture.height <= 0 || texture.pixels.empty()) {
        return 0xFFFFFFFFu;
    }
    if (texture.width == 1 && texture.height == 1) {
        return texture.pixels[0];   // 纯色纹理不需要插值
    }

    // 8 位定点权重：fx = (u * width - 0.5) * 256，取整后高 8 位是整数坐标、低 8 位是小数权重。
    // 全整数运算，省掉每像素的浮点 clamp 与除法。
    const int fx = static_cast<int>(u * static_cast<float>(texture.width) * 256.0f - 128.0f);
    const int fy = static_cast<int>(v * static_cast<float>(texture.height) * 256.0f - 128.0f);
    const int x0 = fx >> 8;
    const int y0 = fy >> 8;
    const int tx = fx & 0xFF;
    const int ty = fy & 0xFF;

    const int xa = clampi(x0, 0, texture.width - 1);
    const int xb = clampi(x0 + 1, 0, texture.width - 1);
    const int ya = clampi(y0, 0, texture.height - 1);
    const int yb = clampi(y0 + 1, 0, texture.height - 1);
    const uint32_t* pixels = texture.pixels.data();
    const uint32_t rowA = static_cast<uint32_t>(ya) * static_cast<uint32_t>(texture.width);
    const uint32_t rowB = static_cast<uint32_t>(yb) * static_cast<uint32_t>(texture.width);

    const uint32_t w00 = static_cast<uint32_t>((256 - tx) * (256 - ty));
    const uint32_t w10 = static_cast<uint32_t>(tx * (256 - ty));
    const uint32_t w01 = static_cast<uint32_t>((256 - tx) * ty);
    const uint32_t w11 = static_cast<uint32_t>(tx * ty);

    // 灰度字体图集：只插值 alpha，RGB 恒为白
    if (texture.isGray) {
        const uint32_t a00 = pixels[rowA + xa] >> 24;
        const uint32_t a10 = pixels[rowA + xb] >> 24;
        const uint32_t a01 = pixels[rowB + xa] >> 24;
        const uint32_t a11 = pixels[rowB + xb] >> 24;
        const uint32_t a = (a00 * w00 + a10 * w10 + a01 * w01 + a11 * w11) >> 16;
        return 0x00FFFFFFu | (a << 24);
    }

    const auto blend = [&](int shift) -> uint32_t {
        const uint32_t a = (pixels[rowA + xa] >> shift) & 0xFF;
        const uint32_t b = (pixels[rowA + xb] >> shift) & 0xFF;
        const uint32_t c = (pixels[rowB + xa] >> shift) & 0xFF;
        const uint32_t d = (pixels[rowB + xb] >> shift) & 0xFF;
        return (a * w00 + b * w10 + c * w01 + d * w11) >> 16;
    };
    return blend(0) | (blend(8) << 8) | (blend(16) << 16) | (blend(24) << 24);
}

void rasterizeTriangle(const ImDrawVert& v0, const ImDrawVert& v1, const ImDrawVert& v2,
    const Texture* texture, const ImVec4& clipRect, uint32_t* framebuffer,
    int width, int height) {
    const float area = edgeFunction(v0.pos.x, v0.pos.y, v1.pos.x, v1.pos.y, v2.pos.x, v2.pos.y);
    if (std::fabs(area) < 0.0001f || (width == 1 && height == 1)) {
        return;
    }
    const bool positive = area > 0.0f;

    int minX = static_cast<int>(std::floor(std::min({ v0.pos.x, v1.pos.x, v2.pos.x })));
    int maxX = static_cast<int>(std::ceil(std::max({ v0.pos.x, v1.pos.x, v2.pos.x })));
    int minY = static_cast<int>(std::floor(std::min({ v0.pos.y, v1.pos.y, v2.pos.y })));
    int maxY = static_cast<int>(std::ceil(std::max({ v0.pos.y, v1.pos.y, v2.pos.y })));
    minX = std::max(minX, std::max(static_cast<int>(clipRect.x), 0));
    minY = std::max(minY, std::max(static_cast<int>(clipRect.y), 0));
    maxX = std::min(maxX, std::min(static_cast<int>(clipRect.z), width));
    maxY = std::min(maxY, std::min(static_cast<int>(clipRect.w), height));
    if (minX >= maxX || minY >= maxY) {
        return;
    }

    const float invArea = 1.0f / area;

    // ---- 整个三角形共享的不变量 ----
    const bool sameColor = (v0.col == v1.col && v1.col == v2.col);
    const uint32_t uniformColor = v0.col;
    const bool trivialTexture =
        texture == nullptr || texture->width <= 0 || texture->height <= 0 ||
        texture->pixels.empty() || (texture->width == 1 && texture->height == 1);

    // ---- 增量边函数 ----
    // 每像素只做加法：e(x+1) = e(x) + ∂e/∂x，e(y+1) = e(y) + ∂e/∂y，
    // 省掉原来的“每像素 2 次乘法 + 1 次除法”。
    const float e0dx = v2.pos.y - v1.pos.y;
    const float e0dy = v1.pos.x - v2.pos.x;
    const float e1dx = v0.pos.y - v2.pos.y;
    const float e1dy = v2.pos.x - v0.pos.x;
    const float e2dx = v1.pos.y - v0.pos.y;
    const float e2dy = v0.pos.x - v1.pos.x;

    const float startX = static_cast<float>(minX) + 0.5f;
    const float startY = static_cast<float>(minY) + 0.5f;
    float e0Row = edgeFunction(v1.pos.x, v1.pos.y, v2.pos.x, v2.pos.y, startX, startY);
    float e1Row = edgeFunction(v2.pos.x, v2.pos.y, v0.pos.x, v0.pos.y, startX, startY);
    float e2Row = edgeFunction(v0.pos.x, v0.pos.y, v1.pos.x, v1.pos.y, startX, startY);

    // ---- 整块同色判定（这是最值钱的一条快路径）----
    // ImGui 画实心矩形时并不用“1x1 白纹理”，而是用字体图集里那个白色像素的 UV。
    // 所以判据是：三顶点颜色相同 + 三顶点 UV 相同 + 该纹素是不透明白色
    // → 整个三角形就是一个颜色，逐像素只需要“在不在三角形里 + 混合”，
    //    完全跳过重心权重、UV 插值、纹理采样。
    bool flatFill = sameColor && trivialTexture;
    if (!flatFill && sameColor && texture != nullptr && v0.uv.x == v1.uv.x && v1.uv.x == v2.uv.x &&
        v0.uv.y == v1.uv.y && v1.uv.y == v2.uv.y) {
        const uint32_t texel = sampleTexture(*texture, v0.uv.x, v0.uv.y);
        if (texel == 0xFFFFFFFFu) {
            flatFill = true;   // 白色纹素：最终颜色就是顶点色
        }
    }
    const uint32_t flatColor = uniformColor;

    for (int y = minY; y < maxY; ++y) {
        float e0 = e0Row;
        float e1 = e1Row;
        float e2 = e2Row;
        uint32_t* dstRow = framebuffer + static_cast<size_t>(y) * width;

        for (int x = minX; x < maxX; ++x) {
            const float r0 = e0;
            const float r1 = e1;
            const float r2 = e2;
            e0 += e0dx;
            e1 += e1dx;
            e2 += e2dx;

            // 三角形内部判定：面积为正时三条边函数都 ≥ 0，为负时都 ≤ 0
            const bool inside = positive ? (r0 >= 0.0f && r1 >= 0.0f && r2 >= 0.0f)
                                         : (r0 <= 0.0f && r1 <= 0.0f && r2 <= 0.0f);
            if (!inside) {
                continue;
            }

            uint32_t* dst = dstRow + x;

            if (flatFill) {
                const uint32_t srcA = (flatColor >> 24) & 0xFF;
                if (srcA != 0) {
                    blendPixel(*dst, flatColor & 0xFF, (flatColor >> 8) & 0xFF,
                               (flatColor >> 16) & 0xFF, srcA);
                }
                continue;
            }

            const float w0 = r0 * invArea;
            const float w1 = r1 * invArea;
            const float w2 = 1.0f - w0 - w1;

            uint32_t srcR;
            uint32_t srcG;
            uint32_t srcB;
            uint32_t srcA;

            if (trivialTexture) {
                const uint32_t color = interpolateColor(v0.col, v1.col, v2.col, w0, w1, w2);
                srcR = color & 0xFF;
                srcG = (color >> 8) & 0xFF;
                srcB = (color >> 16) & 0xFF;
                srcA = (color >> 24) & 0xFF;
            } else {
                // 先采样纹理：文字图集里大部分像素是完全透明的，alpha 为 0 就直接跳过，
                // 不用再做颜色插值与 modulate（这一步在文字多的界面上省很多）
                const float u = w0 * v0.uv.x + w1 * v1.uv.x + w2 * v2.uv.x;
                const float v = w0 * v0.uv.y + w1 * v1.uv.y + w2 * v2.uv.y;
                const uint32_t tex = sampleTexture(*texture, u, v);
                const uint32_t texA = (tex >> 24) & 0xFF;
                if (texA == 0) {
                    continue;
                }
                const uint32_t color = sameColor
                                           ? uniformColor
                                           : interpolateColor(v0.col, v1.col, v2.col, w0, w1, w2);
                srcR = mul255(color & 0xFF, tex & 0xFF);
                srcG = mul255((color >> 8) & 0xFF, (tex >> 8) & 0xFF);
                srcB = mul255((color >> 16) & 0xFF, (tex >> 16) & 0xFF);
                srcA = mul255((color >> 24) & 0xFF, texA);
            }

            blendPixel(*dst, srcR, srcG, srcB, srcA);
        }

        e0Row += e0dy;
        e1Row += e1dy;
        e2Row += e2dy;
    }
}

void renderDrawData(const ImDrawData* drawData, uint32_t* framebuffer, int width, int height) {
    if (drawData == nullptr || framebuffer == nullptr) {
        return;
    }
    // 缓冲区尺寸与 ImGui 的显示尺寸可能差 1 像素，这里用较小值兜底
    const int safeWidth = std::min(width, static_cast<int>(drawData->DisplaySize.x));
    const int safeHeight = std::min(height, static_cast<int>(drawData->DisplaySize.y));

    for (const ImDrawList* drawList : drawData->CmdLists) {
        const ImDrawVert* vertices = drawList->VtxBuffer.Data;
        const ImDrawIdx* indices = drawList->IdxBuffer.Data;
        for (const ImDrawCmd& cmd : drawList->CmdBuffer) {
            if (cmd.UserCallback != nullptr) {
                continue;  // 我们不使用用户回调（含 ResetRenderState）
            }
            const Texture* texture = textureFromId(cmd.GetTexID());
            const ImVec4 clip = cmd.ClipRect;
            const size_t indexCount = cmd.ElemCount;
            for (size_t i = 0; i + 2 < indexCount; i += 3) {
                const ImDrawVert& v0 = vertices[indices[cmd.IdxOffset + i]];
                const ImDrawVert& v1 = vertices[indices[cmd.IdxOffset + i + 1]];
                const ImDrawVert& v2 = vertices[indices[cmd.IdxOffset + i + 2]];
                rasterizeTriangle(v0, v1, v2, texture, clip, framebuffer, safeWidth, safeHeight);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 字体：从系统字体文件里按需生成中文字形
// ---------------------------------------------------------------------------
ImFontConfig defaultFontConfig() {
    ImFontConfig config;
    config.OversampleH = 1;      // 软件渲染下采样开销小一点
    config.OversampleV = 1;
    config.PixelSnapH = false;
    return config;
}

void applyStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.FrameRounding = 6.0f;
    style.GrabRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.WindowPadding = ImVec2(16.0f, 16.0f);
    style.FramePadding = ImVec2(14.0f, 10.0f);
    style.ItemSpacing = ImVec2(10.0f, 10.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.ScrollbarSize = 18.0f;
    style.TouchExtraPadding = ImVec2(6.0f, 6.0f);   // 触摸屏更容易点中

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.09f, 0.11f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.09f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.12f, 0.14f, 0.98f);
    colors[ImGuiCol_Border] = ImVec4(0.20f, 0.28f, 0.26f, 0.60f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.13f, 0.18f, 0.18f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.30f, 0.28f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.38f, 0.33f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.16f, 0.36f, 0.29f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.22f, 0.47f, 0.37f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.28f, 0.56f, 0.44f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.16f, 0.36f, 0.29f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.22f, 0.47f, 0.37f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.28f, 0.56f, 0.44f, 1.00f);
    colors[ImGuiCol_Text] = ImVec4(0.92f, 0.96f, 0.94f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.60f, 0.58f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.55f, 0.84f, 0.67f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.55f, 0.84f, 0.67f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.25f, 0.35f, 0.32f, 0.80f);
}

}  // namespace

bool initialize(int framebufferWidth, int framebufferHeight, float fontScale) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    // 从零开始：纹理表和图标缓存必须一起清（同一个进程里可能被第二次调用，见 AndroidMain.cpp）
    g_textures.clear();
    g_iconCache.clear();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;      // 不写 imgui.ini，保持目录干净
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(static_cast<float>(framebufferWidth), static_cast<float>(framebufferHeight));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // 关键：告诉 ImGui「本后端自己处理 ImTextureData 的动态创建/更新」。
    // 1.92 起字体图集是运行时动态生成的，没有这个标志 ImGui 会认为后端不支持纹理，
    // 进而在渲染时触发断言（表现为弹窗卡死）。
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    g_fontScale = fontScale;
    applyStyle();

    // 先用内置字体兜底（保证启动就能显示），随后 rebuildFont 再换成系统中文字体
    io.Fonts->AddFontDefault();
    return true;
}

void shutdown() {
    g_textures.clear();
    // 图标纹理 id 的缓存必须跟纹理一起清：纹理没了，旧 id 要么指向别的纹理、要么根本不存在，
    // 界面上就表现为图标变成白块 / 花屏（同一个进程里 Activity 重建时踩过）。
    g_iconCache.clear();
    g_font = nullptr;
    if (ImGui::GetCurrentContext() != nullptr) {
        ImGui::DestroyContext();
    }
    g_pendingEdges.clear();
    g_hasPendingMove = false;
    g_pendingMoveSequence = 0;
    g_nextPointerSequence = 1;
}

void beginFrame(int framebufferWidth, int framebufferHeight, float deltaSeconds) {
    ImGuiIO& io = ImGui::GetIO();
    // 每帧最多送一个按键边沿（原因见文件里 g_pendingEdges 的注释）；没有边沿才送最新移动。
    //
    // 但是“边沿优先”不能把松手前的 MOVE 吞掉：安卓一次 poll 里可能同时收到
    // DOWN、若干 MOVE、UP。若第一帧发 DOWN 时丢弃 MOVE，下一帧又直接发 UP，
    // ImGui 就只看到“原地按下后松开”——按钮会误触，拖动距离也永远是 0。
    // 序号记录真实输入顺序：只要待处理 MOVE 早于队首边沿，就先送 MOVE 并保留边沿，
    // 下一帧再送 UP。移动仍只保留最新位置，不会积压；多花一帧换正确拖动值得。
    const bool moveBeforeEdge =
        g_hasPendingMove && !g_pendingEdges.empty() &&
        g_pendingMoveSequence < g_pendingEdges.front().sequence;
    if (moveBeforeEdge) {
        g_hasPendingMove = false;
        io.AddMouseSourceEvent(g_pendingMoveIsTouch ? ImGuiMouseSource_TouchScreen
                                                    : ImGuiMouseSource_Mouse);
        io.AddMousePosEvent(g_pendingMoveX, g_pendingMoveY);
    } else if (!g_pendingEdges.empty()) {
        const PointerEvent event = g_pendingEdges.front();
        g_pendingEdges.pop_front();

        io.AddMouseSourceEvent(event.isTouch ? ImGuiMouseSource_TouchScreen : ImGuiMouseSource_Mouse);
        io.AddMousePosEvent(event.x, event.y);
        io.AddMouseButtonEvent(0, event.down);
        // 不在这里清 g_hasPendingMove：它可能比队首边沿晚（例如 DOWN 后同批到达的
        // MOVE），会在下一帧按序号先于 UP 送出。
    } else if (g_hasPendingMove) {
        g_hasPendingMove = false;
        io.AddMouseSourceEvent(g_pendingMoveIsTouch ? ImGuiMouseSource_TouchScreen
                                                    : ImGuiMouseSource_Mouse);
        io.AddMousePosEvent(g_pendingMoveX, g_pendingMoveY);
    }
    io.DisplaySize = ImVec2(static_cast<float>(framebufferWidth), static_cast<float>(framebufferHeight));
    io.DeltaTime = deltaSeconds > 0.0f ? deltaSeconds : (1.0f / 60.0f);
    // 1.92 起全局字体缩放从 io.FontGlobalScale 改成 style.FontScaleMain
    ImGui::GetStyle().FontScaleMain = g_fontScale;
    ImGui::NewFrame();
}

bool endFrameAndRender(uint32_t* framebufferPixels, int width, int height, bool rasterize) {
    ImGui::Render();

    // 按需更新字体纹理（ImGui 1.92 的动态纹理机制）
    ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData != nullptr && drawData->Textures != nullptr) {
        for (ImTextureData* textureData : *drawData->Textures) {
            if (textureData->Status != ImTextureStatus_OK) {
                updateTexture(textureData);
            }
        }
    }
    if (!rasterize || drawData == nullptr || drawData->CmdListsCount == 0) {
        return false;
    }
    renderDrawData(drawData, framebufferPixels, width, height);
    return true;
}

void addPointerEvent(const PointerEvent& event) {
    PointerEvent ordered = event;
    ordered.sequence = g_nextPointerSequence++;

    g_pendingEdges.push_back(ordered);
    if (g_pendingEdges.size() > 16) {
        g_pendingEdges.pop_front();   // 异常积压：丢最老的，免得点击越拖越晚
    }
}

void addPointerMove(float x, float y, bool isTouch) {
    g_hasPendingMove = true;
    g_pendingMoveX = x;
    g_pendingMoveY = y;
    g_pendingMoveIsTouch = isTouch;
    g_pendingMoveSequence = g_nextPointerSequence++;
}

void addScroll(float deltaY) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseWheelEvent(0.0f, deltaY);
}

void addKeyEvent(ImGuiKey key, bool down) {
    ImGui::GetIO().AddKeyEvent(key, down);
}

void addTextInput(unsigned int codepoint) {
    ImGui::GetIO().AddInputCharacter(codepoint);
}

void addTextUtf8(const std::string& text) {
    if (!text.empty()) {
        ImGui::GetIO().AddInputCharactersUTF8(text.c_str());
    }
}

namespace {

/// 看一个 sfnt 的表目录里有没有 glyf 表（偏移从文件头算）
bool faceHasGlyf(std::FILE* file, long faceOffset) {
    if (std::fseek(file, faceOffset, SEEK_SET) != 0) {
        return false;
    }
    unsigned char head[12] = {0};
    if (std::fread(head, 1, sizeof(head), file) < sizeof(head)) {
        return false;
    }
    const unsigned int numTables = (static_cast<unsigned int>(head[4]) << 8) | head[5];
    if (numTables == 0 || numTables > 512) {
        return false;
    }
    for (unsigned int i = 0; i < numTables; ++i) {
        unsigned char tag[4] = {0};
        if (std::fread(tag, 1, 4, file) < 4) {
            break;
        }
        if (tag[0] == 'g' && tag[1] == 'l' && tag[2] == 'y' && tag[3] == 'f') {
            return true;
        }
        if (std::fseek(file, 12, SEEK_CUR) != 0) {   // 跳过 checksum + offset + length
            break;
        }
    }
    return false;
}

/// ImGui 的字体后端是 stb_truetype，只认 TrueType 轮廓（glyf 表）。
/// CFF / CFF2（也就是 .otf，以及现代 Android 的 NotoSansCJK-*.ttc）它读不了 ——
/// 真喂进去 ImGui 会直接断言 abort（Android 上就是这么崩的），
/// 所以先自己看一眼，不合适就跳过，退回内置字体总比崩了强。
bool looksLikeStbFont(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }
    bool usable = false;
    unsigned char header[12] = {0};
    if (std::fread(header, 1, sizeof(header), file) == sizeof(header)) {
        const bool isCollection =
            header[0] == 't' && header[1] == 't' && header[2] == 'c' && header[3] == 'f';
        if (isCollection) {
            // ttcf：version(4) + numFonts(4) + 每个 face 的偏移(4)；ImGui 默认读第 0 个 face
            unsigned char offset[4] = {0};
            if (std::fread(offset, 1, 4, file) == 4) {
                const long face0 = (static_cast<long>(offset[0]) << 24) |
                                   (static_cast<long>(offset[1]) << 16) |
                                   (static_cast<long>(offset[2]) << 8) |
                                   static_cast<long>(offset[3]);
                usable = faceHasGlyf(file, face0);
            }
        } else {
            usable = faceHasGlyf(file, 0);
        }
    }
    std::fclose(file);
    return usable;
}

}  // namespace

void rebuildFont(float fontScale) {
    ImGuiIO& io = ImGui::GetIO();
    g_fontScale = fontScale;

    io.Fonts->Clear();
    g_font = nullptr;
    g_usingSystemFont = false;
    g_fontPath.clear();

    const float fontSizePixels = 20.0f;   // 逻辑字号；实际缩放靠 FontGlobalScale
    for (const auto& path : platform::fontCandidates()) {
        if (!platform::fileExists(path)) {
            continue;
        }
        if (!looksLikeStbFont(path)) {
            // 例如 Android 的 NotoSansCJK-Regular.ttc（CFF2 轮廓）：
            // 交给 ImGui 会断言崩掉，这里直接跳过换下一个候选
            logWarn("跳过 stb 读不了的字体（CFF/OTF 轮廓）：%s", path.c_str());
            continue;
        }
        ImFontConfig config = defaultFontConfig();
        // 字形范围传 nullptr：1.92 会在真正绘制时按需生成用到的字形。
        // 中文字库有几千个字，按需生成能让启动更快、占用更小。
        ImFont* font = io.Fonts->AddFontFromFileTTF(path.c_str(), fontSizePixels, &config, nullptr);
        if (font != nullptr) {
            g_font = font;
            g_usingSystemFont = true;
            g_fontPath = path;
            logInfo("已加载系统字体：%s", path.c_str());
            break;
        }
        logWarn("字体加载失败：%s", path.c_str());
    }

    if (g_font == nullptr) {
        g_font = io.Fonts->AddFontDefault();
        logWarn("没有可用的带 glyf 轮廓的中文字体，退回内置英文字体（中文会显示为方块）");
        logWarn("Android 系统只带 CFF 轮廓的 Noto CJK：要显示中文得内置一个 TrueType 中文字体");
    }

}

bool usingSystemFont() {
    return g_usingSystemFont;
}

std::string fontPath() {
    return g_fontPath;
}

// ---------------------------------------------------------------------------
// 图标纹理：SVG → 像素 → 注册进纹理表
// ---------------------------------------------------------------------------
namespace {

ImTextureID registerIconPixels(const svg::Image& image) {
    if (image.width <= 0 || image.height <= 0 || image.pixels.empty()) {
        return static_cast<ImTextureID>(0);
    }
    Texture texture;
    texture.width = image.width;
    texture.height = image.height;
    texture.isGray = false;
    texture.pixels = image.pixels;
    g_textures.push_back(std::move(texture));
    return idFromTextureIndex(g_textures.size() - 1);
}

}  // namespace

ImTextureID createIconTextureFromSvg(const std::string& svgText, int sizePx, unsigned int colorRgb,
                                     std::string* outError, float inkFraction) {
    svg::Image image;
    std::string error;
    const bool fitted = inkFraction > 0.0f
                            ? svg::rasterizeFitted(svgText, sizePx, inkFraction, colorRgb, image, error)
                            : svg::rasterize(svgText, sizePx, colorRgb, image, error);
    if (!fitted) {
        if (outError != nullptr) {
            *outError = error;
        }
        logWarn("图标光栅化失败：%s", error.c_str());
        return static_cast<ImTextureID>(0);
    }
    const ImTextureID id = registerIconPixels(image);
    if (id == static_cast<ImTextureID>(0) && outError != nullptr) {
        *outError = "图标纹理创建失败";
    }
    return id;
}

ImTextureID loadIconTexture(const std::string& assetName, int sizePx, unsigned int colorRgb,
                            std::string* outError, float inkFraction) {
    const std::string key = assetName + "#" + std::to_string(sizePx) + "#" +
                            std::to_string(colorRgb & 0xFFFFFFu) + "#" +
                            std::to_string(inkFraction);
    const auto cached = g_iconCache.find(key);
    if (cached != g_iconCache.end()) {
        return cached->second;
    }
    std::string text;
    if (!platform::readAssetText(assetName, text)) {
        if (outError != nullptr) {
            *outError = "读不到图标资源：" + assetName;
        }
        logWarn("读不到图标资源：%s", assetName.c_str());
        return static_cast<ImTextureID>(0);
    }
    const ImTextureID id = createIconTextureFromSvg(text, sizePx, colorRgb, outError, inkFraction);
    if (id != static_cast<ImTextureID>(0)) {
        g_iconCache[key] = id;
    }
    return id;
}

void clearIconCache() {
    g_iconCache.clear();
}

}  // namespace quiz::render

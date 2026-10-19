#pragma once

#include <algorithm>
#include <cstdio>
#include <string>

// ============================================================================
// 帧率统计（平台无关）
// ----------------------------------------------------------------------------
// 为什么放在 core：Windows 要打到控制台、Android 要打到 logcat，
// 但“怎么算、怎么排版这行文本”应该只有一份实现，免得两边数字对不上。
//
// 关键设计：**每一帧都统计，但单独记录“真正重绘了多少帧”**。
//   本程序在没有交互时会跳过光栅化（省电），那些帧几乎不干活；
//   如果把它们和干活帧混在一起平均，数字会变得很好看但毫无意义。
//   所以报告里同时给出：
//       帧率 60.0 FPS | 重绘 12/60 帧 | 重绘帧 17.20 ms（最快/最慢）| 各阶段耗时
//
// 用法（平台层主循环里）：
//     quiz::FrameStats stats(60);                       // 每 60 帧汇报一次
//     stats.addFrame(这一帧总耗时秒, {begin, draw, render, present}, 这一帧是否真的重绘);
//     quiz::FrameStats::Snapshot snapshot;
//     if (stats.consumeReport(snapshot)) {
//         quiz::logInfo("%s", quiz::FrameStats::formatReport(snapshot).c_str());
//     }
// ============================================================================
namespace quiz {

/// 一帧里各阶段的耗时（秒）。测不到的阶段留 0 即可。
struct FrameBreakdown {
    double beginSeconds = 0.0;    // ImGui::NewFrame
    double drawSeconds = 0.0;     // 界面逻辑（ui.draw）
    double renderSeconds = 0.0;   // 光栅化（把 ImGui 画进像素缓冲）
    double presentSeconds = 0.0;  // 贴屏（GDI StretchDIBits / ANativeWindow）
};

class FrameStats {
public:
    /// 一次汇报的内容（一个统计窗口，或整段运行）
    struct Snapshot {
        int frames = 0;            // 窗口内的总帧数
        int renderedFrames = 0;    // 其中真正重绘过的帧数
        double averageFps = 0.0;   // 按总帧数算的平均帧率
        double averageFrameMs = 0.0;  // 重绘帧的平均耗时（没有重绘帧时退化为全部帧）
        double minFrameMs = 0.0;
        double maxFrameMs = 0.0;
        FrameBreakdown averageMs;  // 各阶段平均耗时（毫秒）

        bool idle() const { return renderedFrames == 0; }
    };

    explicit FrameStats(int reportIntervalFrames = 60)
        : interval_(reportIntervalFrames > 0 ? reportIntervalFrames : 1) {}

    void setReportInterval(int frames) { interval_ = frames > 0 ? frames : 1; }
    int reportInterval() const { return interval_; }

    /// 记录一帧
    /// @param totalSeconds 这一帧的总耗时（含等待），用来算帧率
    /// @param breakdown    各阶段耗时
    /// @param rendered     这一帧是否真的做了光栅化/贴屏
    void addFrame(double totalSeconds, const FrameBreakdown& breakdown = FrameBreakdown(),
                  bool rendered = true) {
        if (totalSeconds <= 0.0) {
            return;
        }
        const double ms = totalSeconds * 1000.0;

        windowFrames_ += 1;
        windowSeconds_ += totalSeconds;
        lifetimeFrames_ += 1;
        lifetimeSeconds_ += totalSeconds;

        if (!rendered) {
            return;   // 空闲帧只计入总帧数与时间，不参与耗时统计
        }

        // ---- 当前统计窗口内的“重绘帧” ----
        if (windowRendered_ == 0) {
            windowMinMs_ = ms;
            windowMaxMs_ = ms;
        }
        windowMinMs_ = (std::min)(windowMinMs_, ms);   // 括号形式避免 Windows 的 min/max 宏
        windowMaxMs_ = (std::max)(windowMaxMs_, ms);
        windowRenderedMs_ += ms;
        windowStages_.begin += breakdown.beginSeconds * 1000.0;
        windowStages_.draw += breakdown.drawSeconds * 1000.0;
        windowStages_.render += breakdown.renderSeconds * 1000.0;
        windowStages_.present += breakdown.presentSeconds * 1000.0;
        windowRendered_ += 1;

        // ---- 整段运行累计 ----
        if (lifetimeRendered_ == 0) {
            lifetimeMinMs_ = ms;
            lifetimeMaxMs_ = ms;
        }
        lifetimeMinMs_ = (std::min)(lifetimeMinMs_, ms);
        lifetimeMaxMs_ = (std::max)(lifetimeMaxMs_, ms);
        lifetimeRenderedMs_ += ms;
        lifetimeStages_.begin += breakdown.beginSeconds * 1000.0;
        lifetimeStages_.draw += breakdown.drawSeconds * 1000.0;
        lifetimeStages_.render += breakdown.renderSeconds * 1000.0;
        lifetimeStages_.present += breakdown.presentSeconds * 1000.0;
        lifetimeRendered_ += 1;
    }

    /// 攒够一个统计窗口就返回 true，并把统计结果写到 out
    bool consumeReport(Snapshot& out) {
        if (windowFrames_ < interval_) {
            return false;
        }
        out = makeSnapshot(windowFrames_, windowRendered_, windowSeconds_, windowRenderedMs_,
                           windowMinMs_, windowMaxMs_, windowStages_, windowFrames_);
        resetWindow();
        return true;
    }

    /// 无视间隔，立刻取出整段运行的统计（--bench 结束时用）
    Snapshot lifetimeSnapshot() const {
        return makeSnapshot(lifetimeFrames_, lifetimeRendered_, lifetimeSeconds_, lifetimeRenderedMs_,
                            lifetimeMinMs_, lifetimeMaxMs_, lifetimeStages_, lifetimeFrames_);
    }

    /// 生成一行人类可读的报告文本（两个平台格式一致）
    static std::string formatReport(const Snapshot& snapshot, const char* title = "帧率") {
        char buffer[320];
        if (snapshot.idle()) {
            std::snprintf(buffer, sizeof(buffer),
                          "%s %.1f FPS | 本窗口 %d 帧全部空闲（界面无变化，跳过光栅化）",
                          title, snapshot.averageFps, snapshot.frames);
            return std::string(buffer);
        }
        std::snprintf(buffer, sizeof(buffer),
                      "%s %.1f FPS | 重绘 %d/%d 帧 | 重绘帧平均 %.2f ms（最快 %.2f / 最慢 %.2f）| "
                      "begin %.2f / draw %.2f / 光栅化 %.2f / 贴屏 %.2f ms",
                      title, snapshot.averageFps, snapshot.renderedFrames, snapshot.frames,
                      snapshot.averageFrameMs, snapshot.minFrameMs, snapshot.maxFrameMs,
                      snapshot.averageMs.beginSeconds, snapshot.averageMs.drawSeconds,
                      snapshot.averageMs.renderSeconds, snapshot.averageMs.presentSeconds);
        return std::string(buffer);
    }

private:
    struct StagesMs {
        double begin = 0.0;
        double draw = 0.0;
        double render = 0.0;
        double present = 0.0;
    };

    static Snapshot makeSnapshot(int frames, int renderedFrames, double sumSeconds, double renderedMs,
                                 double minMs, double maxMs, const StagesMs& stages, int divisorFrames) {
        Snapshot snapshot;
        snapshot.frames = frames;
        snapshot.renderedFrames = renderedFrames;
        if (frames <= 0) {
            return snapshot;
        }
        snapshot.averageFps = sumSeconds > 0.0 ? static_cast<double>(frames) / sumSeconds : 0.0;
        // 平均耗时按“重绘帧”算；一个重绘帧都没有时，用整段平均（快得几乎测不出来）
        const int divisor = renderedFrames > 0 ? renderedFrames : divisorFrames;
        if (divisor <= 0) {
            return snapshot;
        }
        snapshot.averageFrameMs = renderedFrames > 0 ? (renderedMs / divisor)
                                                     : (sumSeconds * 1000.0 / divisor);
        snapshot.minFrameMs = minMs;
        snapshot.maxFrameMs = maxMs;
        snapshot.averageMs.beginSeconds = stages.begin / divisor;
        snapshot.averageMs.drawSeconds = stages.draw / divisor;
        snapshot.averageMs.renderSeconds = stages.render / divisor;
        snapshot.averageMs.presentSeconds = stages.present / divisor;
        return snapshot;
    }

    void resetWindow() {
        windowFrames_ = 0;
        windowRendered_ = 0;
        windowSeconds_ = 0.0;
        windowRenderedMs_ = 0.0;
        windowMinMs_ = 0.0;
        windowMaxMs_ = 0.0;
        windowStages_ = StagesMs{};
    }

    int interval_ = 60;

    // 当前统计窗口
    int windowFrames_ = 0;
    int windowRendered_ = 0;
    double windowSeconds_ = 0.0;
    double windowRenderedMs_ = 0.0;
    double windowMinMs_ = 0.0;
    double windowMaxMs_ = 0.0;
    StagesMs windowStages_;

    // 整段运行累计
    int lifetimeFrames_ = 0;
    int lifetimeRendered_ = 0;
    double lifetimeSeconds_ = 0.0;
    double lifetimeRenderedMs_ = 0.0;
    double lifetimeMinMs_ = 0.0;
    double lifetimeMaxMs_ = 0.0;
    StagesMs lifetimeStages_;
};

}  // namespace quiz

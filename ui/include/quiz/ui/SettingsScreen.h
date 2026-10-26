#pragma once

#include <functional>
#include <string>

#include "quiz/app/AppContext.h"

// ============================================================================
// 设置页（下钻式列表）
// ----------------------------------------------------------------------------
// 根页只列分类（答题 / 界面 / 主题配色 / 提示词 / 插件），点进去才是具体设置。
// 主题配色页内部**不再往下跳页**：五个分组都在同一页上展开 / 收起（见 drawTheme）。
// 以后加分类：在 drawRoot() 的列表里加一项，再补一个 drawXxx() 即可。
// 这里只负责「怎么画」，业务状态仍然存在 AppContext / Settings 里。
// ============================================================================
namespace quiz::ui {

class SettingsScreen {
public:
    explicit SettingsScreen(AppContext& context);

    /// 画一帧当前页；返回值是本帧产生的动作请求
    struct Result {
        bool leaveToHome = false;     // 点了「返回首页」
        bool fontDirty = false;       // 换了语言，需要重建字体
        bool cancelAutoNext = false;  // 关掉了「答对自动跳下一题」
        bool requestBackgroundImport = false;  // 打开系统文件选择器导入首页背景
        bool requestPromptDownload = false;    // 打开系统保存对话框下载当前提示词
        int promptDownloadIndex = -1;
    };
    Result draw();

    /// 从首页进入设置时调用：回到根页
    void reset() { page_ = Page::Root; }
    /// Android 返回手势：按设置层级逐页返回；根页返回 false。
    bool backToRoot() {
        if (page_ == Page::Root) {
            return false;
        }
        page_ = parentPage(page_);
        return true;
    }

    /// 图标按钮的画法由 UiApp 注入（设置页头部要和首页共用同一套样式）
    using IconButton =
        std::function<bool(const char* id, const char* svg, const std::string& label)>;
    void setIconButton(IconButton drawIcon) { iconButton_ = std::move(drawIcon); }

private:
    enum class Page {
        Root,
        Answering,
        Interface,
        Theme,   // 主题配色：五个分组在这一页上展开 / 收起，不再往下跳页
        Prompt,
        Manual,
        ManualOverview,
        ManualImport,
        ManualExport,
        ManualIcons,
        Plugins,
    };

    static Page parentPage(Page page);
    static const char* titleKeyFor(Page page);
    /// 子页头部：返回（回设置根页）+ 标题；返回 true 表示点了返回
    bool drawSubHeader(Page page);

    void drawRoot();
    void drawAnswering(Result& result);
    void drawInterface(Result& result);
    void drawTheme();
    /// 配色分组页（group：0 底色 / 1 文字 / 2 控件 / 3 状态）
    void drawThemeGroup(int group);
    /// 预设与应用：换预设、恢复默认、复制/粘贴主题文本
    void drawThemeApply();
    void drawPrompt(Result& result);
    void drawManual();
    void drawManualContent(Page page, const char* manualId);

    /// 切换界面比例：存设置，并在支持的平台上把窗口也调成该比例
    void applyAspect(const std::string& code, int width, int height);

    AppContext& context_;
    Page page_ = Page::Root;
    int promptIndex_ = 0;   // 「提示词」页当前选中的那一项
    /// 主题配色页哪些分组是展开的（位 0 底色 / 1 文字 / 2 控件 / 3 状态 / 4 预设与应用）
    int themeOpen_ = 1;
    /// 上一帧主题内容是否超出可视高度；用来只在实际需要时显示滚动条
    bool themeScrollNeeded_ = false;
    IconButton iconButton_;   // 由 UiApp 注入；没注入时退回文字按钮
};

}  // namespace quiz::ui

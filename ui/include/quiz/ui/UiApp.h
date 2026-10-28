#pragma once

#include <string>

#include "quiz/app/AppContext.h"
#include "quiz/ui/SettingsScreen.h"

// ============================================================================
// 界面层：四个页面（首页 / 刷题 / 结果 / 设置）
// ----------------------------------------------------------------------------
// 只依赖 AppContext 与 ImGui：
//   * 不碰文件、不碰平台接口 → 换平台不用改这里
//   * 所有文案都走 i18n，方便加语言包
//   * 页面逻辑（点按钮做什么）集中在这里，方便以后加设置页、错题页
// ============================================================================
namespace quiz::ui {

enum class Screen {
    Home,    // 首页：选题库、导出、切换语言
    Quiz,    // 刷题页：题干、选项按钮、提交、解析
    Result,  // 结果页：统计与错题列表
    Settings,// 设置页
    Stats,   // 数据统计页：累计刷题量、时长、连续天数、题库完成情况
    Finished,// 本轮刷完：激励性结算页
    ExamResult,// 考试成绩：交卷后的成绩单
    ExamReady,// 考试准备页：选完卷先看规格与规则
};

class UiApp {
public:
    explicit UiApp(AppContext& context);

    /// 每帧调用（ImGui 的 NewFrame 已由渲染层完成）
    void draw(float deltaSeconds);

    Screen screen() const { return screen_; }
    /// 直接切换页面（无头截图工具用）
    void setScreen(Screen screen) { screen_ = screen; }
    /// Android 返回手势 / Windows 返回键：
    /// true = 已在界面内处理；false = 平台可以退出应用。
    bool handleBack();

    /// 界面文字变化（切语言、加载题库）后需要重建字体图集
    void markFontDirty() { fontDirty_ = true; }

    /// 是否需要在「下一帧开始之前」重建字体。
    /// 注意：ImGui 1.92 的字体图集不能在 NewFrame() 之后修改（会破坏纹理协议并触发断言），
    /// 所以平台层要在这两个函数之间安排：先 applyFontRebuild()，再 beginFrame()。
    bool needsFontRebuild() const { return fontDirty_; }
    void applyFontRebuild();

private:
    void drawHome();
    void drawQuiz();
    /// 材料题卷面：材料 + 同一段材料下的所有小题，画在一页里（可上下滚动）
    void drawPaper(const QuestionBank& bank, const std::vector<int>& group);
    /// 卷面里的一道小题
    void drawPaperQuestion(const QuestionBank& bank, int index, int part, int count);
    void drawResult();
    void drawSettings();
    /// 数据统计页
    void drawStats();
    /// 本轮刷完的结算页
    void drawFinished();
    /// 每交完一题调用：这一轮做完了就切到结算页
    void checkRoundFinished();
    void drawToast(float deltaSeconds);
    void drawManualIntroPopup();

    void startBank(int index);
    /// 发起导出：弹系统「保存」对话框（只负责发起，结果在 pollPendingDialog 里收尾）
    void beginExport(AppContext::ExportKind kind);
    /// 发起提示词下载：弹系统「保存」对话框
    void beginPromptDownload(int index);
    /// 从系统文件对话框导入题库（首页「载入」按钮，同样只负责发起）
    void beginImport();
    /// 从系统文件对话框导入首页背景图片
    void beginBackgroundImport();
    /// 系统对话框（导入 / 导出）有结果了吗？有结果就接着做完。
    /// 为什么不在发起处等结果：Android 的选择器是另一个 Activity，系统停窗口时
    /// native_app_glue 的 onPause 会阻塞主线程直到宿主线程处理完命令（实测 5 秒即 ANR）。
    void pollPendingDialog();
    /// 导出内容写好之后的收尾：交给用户选中的位置，并按结果给出提示
    void reportExportResult(const std::string& localPath);
    void openIssuesPopup();

    /// 背题模式：把当前页的答案亮出来（材料题一次亮一组），并把没做对过的记成错题
    void applyReciteIfNeeded();
    /// 打开「题目总览」面板（圆点网格）
    void openOverviewPopup();
    /// 画「题目总览」面板：编号 = 轮内位置，点一下跳到那一题
    void drawOverviewPanel();
    /// 提交当前题：判分、落盘，并按设置安排「答对自动跳下一题」
    void submitCurrent();
    /// 每帧推进自动跳转倒计时（在画题目前调用）
    void tickAutoNext();
    /// 取消待执行的自动跳转（手动翻页、离开刷题页、关掉开关时调用）
    void cancelAutoNext();
    /// 「加标签」弹窗：答题页与材料题卷面共用（内容画在主窗口之外）
    void drawTagPopup();
    /// 「按标签导出」弹窗：标签多选 + 要不要把用户标签固化进导出的题目
    void drawTagExportPopup();

    // ---- 考试模式 ----
    /// 首页点了「开始考试」：抽一遍每套卷做预览，然后弹选卷框
    void openExamPicker();
    /// 选卷弹窗：列题库里的卷（题数 / 满分 / 时长 / 抽题提示）
    void drawExamPicker();
    /// 「确定交卷吗」确认框
    void drawExamSubmitAsk();
    /// 考试中返回时的「退出考试」确认框
    void drawExamExitAsk();
    /// 交卷：判分、写进度、切到成绩页（倒计时到点也走这里）
    void finishExamNow();
    /// 考试成绩页：分数 / 及格与否 / 逐题对错
    void drawExamResult();
    /// 考试准备页：卷名 / 题数 / 满分 / 时长 / 及格线 / 规则，点「开始答题」才开考
    void drawExamReady();

    /// 计算内容区：选了固定比例就取最大居中内接矩形，否则铺满窗口
    void computeContentRect(float& outX, float& outY, float& outWidth, float& outHeight) const;
    /// 把设置里的配色套到 ImGui 主题上（每帧调用，改完立刻生效）
    void applyTheme();

    AppContext& context_;
    SettingsScreen settingsScreen_;
    Screen screen_ = Screen::Home;
    Screen quizParent_ = Screen::Home;      // 返回刷题页时回哪里
    Screen resultParent_ = Screen::Quiz;    // 返回结果页时回哪里
    int selectedBank_ = 0;
    bool fontDirty_ = true;
    std::string toast_;
    float toastTimer_ = 0.0f;
    float frameDelta_ = 0.0f;      // 本帧时长（秒）：自动跳转倒计时用
    bool pendingBackDismissText_ = false;
    bool pendingBackClosePopup_ = false;
    bool manualIntroInitialized_ = false;
    bool manualIntroOpen_ = false;
    bool manualIntroDontAgain_ = false;
    bool exitArmed_ = false;
    float exitArmTimer_ = 0.0f;
    float autoNextTimer_ = 0.0f;   // >0 表示正在倒计时自动跳下一题
    int autoNextIndex_ = -1;       // 倒计时是为哪一题安排的
    bool aspectApplied_ = false;   // 启动后是否已经把窗口调成设置里的比例
    unsigned int colorAccentRgb_ = 0xB22222;    // 0xRRGGBB，来自设置
    unsigned int colorCorrectRgb_ = 0x5C1010;
    unsigned int colorWrongRgb_ = 0xD32F2F;
    unsigned int colorPartialRgb_ = 0xA93226;   // 部分分 / 进度条
    unsigned int colorDraftRgb_ = 0x3F7FBF;     // 未提交（正在作答）
    unsigned int colorStarRgb_ = 0xD35400;      // 收藏星标
    unsigned int colorTextAccentRgb_ = 0x8B0000; // 强调文字（标题 / 题号）
    unsigned int colorButtonTextRgb_ = 0x1A1C1D; // 主按钮上的文字
    unsigned int colorIconRgb_ = 0x3A434A;      // 图标颜色
    bool statsResetArmed_ = false;   // 统计页「重置统计」是否已经点过一次（再点才清零）
    int clearArmedBank_ = -1;        // 题库卡片上「清除数据」已经点过一次的是哪一张
    int deleteArmedBank_ = -1;       // 卡片上「删除题库」已经点过一次的是哪一张
    bool showIssues_ = false;
    std::string issuesText_;
    bool overviewPopupRequested_ = false;
    int fillFocusIndex_ = -1;      // 已经给哪一题自动聚焦过输入框
    bool openTagPopup_ = false;    // 本帧要打开「加标签」弹窗
    int tagTargetIndex_ = -1;      // 弹窗正在给哪一题加标签（-1 = 没有）
    char tagBuffer_[64] = {0};     // 输入框里的标签名
    // 「按标签导出」弹窗的状态（打开时刷新一遍标签列表）
    bool openTagExport_ = false;
    std::vector<AppContext::TagInfo> tagExportList_;
    std::vector<char> tagExportChecked_;
    bool tagExportBake_ = false;   // 把我自己加的标签固化进导出的题目
    // 考试模式：
    bool openExamPicker_ = false;               // 本帧要弹出「选卷」框
    bool examSubmitAsk_ = false;                // 本帧要弹出「确定交卷吗」
    bool examExitAsk_ = false;                  // 本帧要弹出「退出考试」
    std::vector<io::ExamPaper> examPreviews_;   // 选卷框里的预览（题数 / 满分 / 抽题提示）
    int pendingExam_ = -1;                      // 准备页上等确认的那套卷（题库里的下标）

    // 正在等结果的系统对话框（同一时刻只允许一个）
    enum class PendingDialog {
        None,
        Import,
        BackgroundImport,
        ExportBank,
        ExportTags,
        PromptDownload,
    };
    PendingDialog pendingDialog_ = PendingDialog::None;
    AppContext::ExportKind pendingExportKind_ = AppContext::ExportKind::WholeBank;
    std::vector<std::string> pendingTags_;   // 按标签导出时选中的标签
    bool pendingBake_ = false;               // 按标签导出时要不要固化用户标签
    int pendingPromptIndex_ = -1;            // 正在下载的提示词下标
};

}  // namespace quiz::ui

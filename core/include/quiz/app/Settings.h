#pragma once

#include <string>

// ============================================================================
// 应用设置：跟着数据目录走（data/settings.txt），纯文本、可手工编辑
// ----------------------------------------------------------------------------
// 只放「影响答题体验」的开关；界面临时状态（当前页、倒计时）不放这里。
// ============================================================================
namespace quiz {

struct Settings {
    /// 答对后自动跳到下一题（最后一题不跳）
    bool autoNextOnCorrect = false;
    /// 单选 / 判断题：点一下选项就直接提交判分（多选题不受影响）
    bool autoSubmitSingle = false;
    /// 材料题卷面怎么交：false = 逐题提交（每题自己交），true = 整页提交（填完一次交）
    bool paperSubmitWholePage = false;
    /// 刷题顺序："sequence" = 按题库编号依次刷（默认），"shuffle" = 每轮打乱，
    /// "wrong" = 只刷错题。**只作用于从「数据统计」页点进题库那一处**。
    std::string practiceOrder = "sequence";
    /// 首页「刷题模式」选的那个，决定首页入口（开始练习 / 题库卡片）刷什么：`n    /// "sequence" 顺序 / "shuffle" 乱序 / "weighted" 权重 / "recite" 背题 / "exam" 考试
    /// "sequence" = 顺序，"shuffle" = 乱序。和上面的 practiceOrder 互不干扰。
    std::string homeOrder = "sequence";
    /// 「每轮题数」：顺序 / 乱序 / 权重三种刷题都按它定这一轮刷几道
    /// （权重刷题允许同一道题重复出现；考试模式还没做，暂时不受它影响）
    int roundSize = 20;
    /// 「错题本」的被动移出比例：答对次数达到答错次数的这个倍数时，这道题自动移出错题本
    /// （默认 3 倍；手改配置文件写歪了会夹回 kMinWrongBookRatio ~ kMaxWrongBookRatio）
    int wrongBookRatio = 3;
    /// 权重刷题：每提交这么多题，就在队尾按**当前**掌握度再补抽一道（默认 5；
    /// 设成 1 会让这一轮永远补不完，所以下限是 2）
    int weightedAppendEvery = 5;
    /// 自动跳转前停留的秒数，留点时间看「回答正确」和解析
    float autoNextDelaySeconds = 1.2f;
    /// 界面比例："auto" = 跟随窗口，也可以写 "16:9"（格式是 宽:高）
    std::string uiAspect = "auto";
    /// 界面配色：十六进制 RRGGBB（可带 #）；默认值就是原来内置主题的颜色
    std::string uiThemeName = "电子绿";         // 当前主题名（导入预设时记住）
    std::string uiColorBackground = "0F171C";   // 窗口底色
    std::string uiColorPanel = "171F24";        // 面板 / 输入框底色
    std::string uiColorText = "EBF5F0";         // 正文文字
    std::string uiColorButton = "295C4A";       // 按钮底色
    std::string uiColorAccent = "96DCB4";       // 强调色（标题 / 进度 / 提示）
    std::string uiColorCorrect = "78DCA0";      // 答对
    std::string uiColorWrong = "F08C82";        // 答错
    // ---- 配色扩容：底色 / 文字 / 控件 / 状态，共 17 项 ----
    std::string uiColorInput = "232E34";        // 输入框底色
    std::string uiColorBorder = "35434A";       // 边框线
    std::string uiColorTextMuted = "8FA3A0";    // 次要文字（说明 / 禁用）
    std::string uiColorTextAccent = "96DCB4";   // 强调文字（标题 / 题号）
    std::string uiColorButtonText = "16181C";   // 主按钮上的文字
    std::string uiColorButton2 = "24322F";      // 次按钮底色
    std::string uiColorIcon = "EBF5F0";         // 图标颜色
    std::string uiColorPartial = "E8C46A";      // 部分分
    std::string uiColorDraft = "5B9BD5";        // 未提交（正在作答：选了 / 填了但还没交）
    std::string uiColorStar = "F2C14E";         // 收藏星标

    /// 允许的停顿区间：手改配置文件写歪了也会夹回这个范围
    static constexpr float kMinDelaySeconds = 0.3f;
    static constexpr float kMaxDelaySeconds = 5.0f;
    /// 「每轮题数」的允许范围（手改配置文件写歪了也会夹回来）
    static constexpr int kMinRoundSize = 5;
    static constexpr int kMaxRoundSize = 200;
    /// 「错题本」被动移出比例的允许范围
    static constexpr int kMinWrongBookRatio = 1;
    static constexpr int kMaxWrongBookRatio = 20;
    /// 权重刷题补题间隔的允许范围
    static constexpr int kMinWeightedAppend = 2;
    static constexpr int kMaxWeightedAppend = 100;

    /// 实际使用的停顿时间（已夹到 [kMinDelaySeconds, kMaxDelaySeconds]）
    float autoNextDelay() const;

    /// 界面比例：返回 false 表示「跟随窗口」或者写得不合法
    bool uiAspectRatio(int& outWidth, int& outHeight) const;

    /// 解析 "RRGGBB" / "#RRGGBB" 形式的颜色；成功时 outRgb 是 0xRRGGBB
    static bool parseHexColor(const std::string& text, unsigned int& outRgb);

    /// 序列化成 settings.txt 的内容
    std::string serialize() const;
    /// 从 settings.txt 的内容解析；文件里没写的键保持默认值
    static Settings parse(const std::string& text);
};

}  // namespace quiz

#pragma once

#include <map>
#include <string>
#include <vector>

// ============================================================================
// 语言包接口（预留给多语言）
// ----------------------------------------------------------------------------
// 设计目标：界面里不出现任何硬编码文案，全部通过 key 取文本。
// 查找顺序：语言包 → 内置默认（中文） → key 本身（方便一眼看出漏翻的键）
//
// 语言包文件（assets/lang/*.lang 或数据目录里的同名文件）格式：
//     [lang]
//     code = en
//     name = English
//     [text]
//     ui.app_title = Quiz App
//     ...
// ============================================================================
namespace quiz::i18n {

class I18n {
public:
    I18n();

    /// 回到内置默认语言（简体中文），用于语言包加载失败时的兜底
    void useBuiltin();

    /// 加载一份语言包文本；成功返回 true（会自动把内部语言代码设为包里的 code）
    bool loadPackText(const std::string& text, const std::string& sourceName, std::string* outError = nullptr);

    /// 当前语言代码，例如 "zh-CN" / "en"
    const std::string& language() const { return language_; }
    /// 当前语言显示名，例如 "简体中文" / "English"
    const std::string& languageName() const { return languageName_; }
    /// 是否正在使用内置默认文案（没有成功加载任何语言包）
    bool usingBuiltin() const { return usingBuiltin_; }

    /// 取文案；找不到时返回 key 本身，便于发现漏翻
    const char* text(const std::string& key) const;

    /// 带占位符的文案：文本里写 {name}，这里传 {"name", "张三"}
    std::string format(const std::string& key,
                       const std::vector<std::pair<std::string, std::string>>& args) const;

    /// 内置支持的语言候选（真正的可用性由 AppContext 从 assets/lang 扫描决定）
    static std::vector<std::string> builtinLanguageCandidates();

    /// 全部内置文案的键名（用于字体字形收集、检查漏翻）
    static std::vector<std::string> allKeys();

private:
    std::map<std::string, std::string> strings_;   // 当前语言包的键值
    std::string language_;
    std::string languageName_;
    bool usingBuiltin_ = true;
};

}  // namespace quiz::i18n

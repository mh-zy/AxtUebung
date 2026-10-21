#include "quiz/i18n/I18n.h"

#include "quiz/io/TextDocument.h"

namespace quiz::i18n {

namespace {

/// 内置默认文案（简体中文）。即使一个语言包都没有，程序也能正常显示。
const std::pair<const char*, const char*> kBuiltinTexts[] = {
    {"ui.app_title", "AxtUebung"},
    {"ui.home.settings", "设置"},
    {"ui.home.practice_sequential", "顺序刷题"},
    {"ui.home.mode_short_sequential", "顺序"},
    {"ui.home.mode_short_shuffle", "乱序"},
    {"ui.home.mode_short_weighted", "权重"},
    {"ui.home.mode_short_exam", "考试"},
    {"ui.home.practice_shuffle", "乱序刷题"},
    {"ui.home.practice_weighted", "权重刷题"},
    {"ui.home.practice_recite", "背题"},
    {"ui.home.mode_short_recite", "背题"},
    {"ui.home.practice_exam", "考试模式"},
    {"ui.home.round_size", "每轮题数"},
    {"ui.home.round_size_unit", "题"},
    {"ui.home.practice_wrong", "错题训练"},
    {"ui.home.export_tags", "导出标签题"},
    {"ui.home.not_implemented", "{name}还没做"},
    {"ui.home.upload", "载入"},
    {"ui.home.bank_list", "题库列表"},
    {"ui.home.progress_summary", "已答 {answered}/{total}，正确 {correct}"},
    {"ui.home.bank_line", "{file} ｜ 共 {total} 题 ｜ {lang}"},
    {"ui.home.not_started", "未开始"},
    {"ui.home.start", "开始练习"},
    {"ui.home.continue", "继续练习"},
    {"ui.home.restart", "清除数据"},
    {"ui.home.delete_bank", "删除题库"},
    {"ui.home.delete_armed", "再点一次就删除「{name}」的题库文件，答题数据一起删"},
    {"ui.home.delete_done", "已删除题库：{name}"},
    {"ui.home.delete_builtin", "「{name}」是内置题库，删不掉；想清空记录可以用「清除数据」"},
    {"ui.home.delete_failed", "删除失败：{name}"},
    {"ui.home.clear_armed", "再点一次就清除「{name}」的答题数据，累计统计不受影响"},
    {"ui.home.clear_done", "已清除「{name}」的答题数据"},
    {"ui.home.wrong_book", "错题本"},
    {"ui.home.wrong_book_empty", "「{name}」还没有错题"},
    {"ui.home.export_bank", "导出题库"},
    {"ui.home.export_short_bank", "题库"},
    {"ui.home.export_short_wrong", "错题"},
    {"ui.home.export_short_marked", "收藏"},
    {"ui.home.export_short_tags", "标签"},
    {"ui.home.section_export", "导出"},
    {"ui.home.section_practice", "刷题模式"},
    {"ui.home.stats", "数据统计"},
    {"ui.stats.title", "数据统计"},
    {"ui.stats.back", "返回"},
    {"ui.stats.empty", "还没有答题记录：开始刷题之后这里就有数据了"},
    {"ui.stats.total_line", "累计刷题 {count} 题"},
    {"ui.stats.accuracy_line", "其中全对 {correct} 题 · 正确率 {rate}"},
    {"ui.stats.time_line", "做题时长 {time}"},
    {"ui.stats.per_question", "平均每题 {seconds} 秒"},
    {"ui.stats.duration_hm", "{hours} 小时 {minutes} 分"},
    {"ui.stats.duration_ms", "{minutes} 分 {seconds} 秒"},
    {"ui.stats.duration_s", "{seconds} 秒"},
    {"ui.stats.banks_line", "已完成 {done} / {total} 个题库"},
    {"ui.stats.banks_ever", "累计完成过 {count} 个题库"},
    {"ui.stats.banks_left", "还有 {count} 个题库有错题"},
    {"ui.stats.banks_all_done", "所有题库都没有错题了"},
    {"ui.stats.marks_line", "收藏 {marked} 题 · 错题 {wrong} 题"},
    {"ui.stats.streak_line", "连续答题 {current} 天，最长 {best} 天"},
    {"ui.stats.dates_line", "首次使用 {first} · 最近答题 {last}"},
    {"ui.stats.recent", "最近 7 天提交的题数"},
    {"ui.stats.detail", "题库明细"},
    {"ui.stats.bank_progress", "已答 {answered} / {total}"},
    {"ui.stats.status_done", "已完成"},
    {"ui.stats.status_doing", "进行中"},
    {"ui.stats.status_new", "未开始"},
    {"ui.home.export_wrong", "导出错题本"},
    {"ui.home.export_marked", "导出收藏题"},
    {"ui.home.language", "语言"},
    {"ui.home.no_banks", "没有找到题库：把 .quiz 放进 assets/banks 或 data/banks"},
    {"ui.home.issues", "题库提示"},
    {"ui.home.quit", "退出"},
    {"ui.quiz.type_single", "单选题"},
    {"ui.quiz.type_multiple", "多选题"},
    {"ui.quiz.type_judge", "判断题"},
    {"ui.quiz.judge_true", "正确"},
    {"ui.quiz.judge_false", "错误"},
    {"ui.quiz.progress", "第 {current} / {total} 题"},
    {"ui.quiz.overview", "题目总览"},
    {"ui.quiz.round_correct", "答对 {correct} 题"},
    {"ui.quiz.remove_from_wrong", "移出错题本"},
    {"ui.quiz.remove_from_wrong_done", "已移出错题本"},
    {"ui.quiz.submit", "提交答案"},
    {"ui.quiz.next", "下一题"},
    {"ui.quiz.prev", "上一题"},
    {"ui.quiz.back_home", "返回首页"},
    {"ui.quiz.mark", "收藏"},
    {"ui.quiz.unmark", "取消收藏"},
    {"ui.quiz.tags", "标签"},
    {"ui.quiz.tag_hint", "输入标签名后回车"},
    {"ui.quiz.tag_mine", "我加的标签"},
    {"ui.quiz.tag_remove", "删除"},
    {"ui.quiz.tag_empty", "还没有标签"},
    {"ui.quiz.analysis", "解析"},
    {"ui.quiz.no_selection", "请先选择答案"},
    {"ui.quiz.result_correct", "回答正确"},
    {"ui.quiz.result_wrong", "回答错误，正确答案：{answer}"},
    {"ui.quiz.empty_bank", "题库为空"},
    {"ui.result.title", "本次结果"},
    {"ui.result.summary", "共 {total} 题，已答 {answered} 题，正确 {correct} 题，正确率 {rate}"},
    {"ui.result.wrong_list", "错题列表"},
    {"ui.result.no_wrong", "目前没有错题，真棒！"},
    {"ui.result.redo_wrong", "重做错题"},
    {"ui.result.export_wrong", "导出错题本"},
    {"ui.result.back_home", "返回首页"},
    {"ui.export.done", "已导出到：{path}"},
    {"ui.export.tags_title", "按标签导出"},
    {"ui.export.tags_pick", "选要导出的标签，可多选"},
    {"ui.export.tags_none", "这个题库还没有标签"},
    {"ui.export.tags_no_bank", "先在首页选一个题库"},
    {"ui.export.tags_bake", "把我自己加的标签一起写进导出的题目"},
    {"ui.export.tags_bake_hint", "勾上：导出的每题带上你加的标签；不勾：只保留题库原本的标签"},
    {"ui.export.tags_go", "导出"},
    {"ui.export.tags_pick_empty", "还没选标签"},
    {"ui.export.save_title", "导出题库到…"},
    {"ui.export.failed_write", "写文件失败：目标目录可能没有写权限"},
    {"ui.export.no_bank", "没有打开的题库"},
    {"ui.home.import_title", "选择题库文件"},
    {"ui.home.import_ok", "已导入 {count} 个题库：{names}"},
    {"ui.home.import_invalid", "没有导入：选中的文件不是有效的题库"},
    {"ui.home.import_none", "没有导入任何题库"},
    {"ui.home.import_failed", "读不到你选的文件"},
    {"ui.export.failed", "导出失败：{reason}"},
    {"ui.export.failed_commit", "没能写进你选的位置"},
    {"ui.export.failed_pick", "弹不出系统「保存」对话框"},
    {"ui.export.empty", "没有可导出的题目"},
    {"ui.lang.switched", "语言已切换：{name}"},
    {"ui.common.ok", "确定"},
    {"ui.common.cancel", "取消"},
    {"ui.common.copy", "复制"},
    {"ui.common.copied", "已复制"},
    {"ui.common.add", "添加"},
    {"ui.common.close", "关闭"},
    {"ui.settings.title", "设置"},
    {"ui.settings.quiz_section", "答题"},
    {"ui.settings.auto_next", "答对后自动跳下一题"},
    {"ui.settings.auto_next_delay", "答对后停留"},
    {"ui.settings.auto_next_delay_unit", "秒"},
    {"ui.settings.auto_next_hint", "本轮最后一题不跳"},
    {"ui.settings.auto_submit", "单选题选择即提交"},
    {"ui.settings.auto_submit_hint", "单选与判断题选择即判分"},
    {"ui.settings.paper_submit", "材料题卷面提交方式"},
    {"ui.settings.paper_submit_each", "逐题提交"},
    {"ui.settings.paper_submit_whole", "整页提交"},
    {"ui.settings.paper_submit_hint", "只影响「一段材料一页」的卷面；整页提交时每题不再有单独的提交按钮"},
    {"ui.settings.practice_order", "统计页的刷题顺序"},
    {"ui.settings.practice_order_sequence", "顺序刷题"},
    {"ui.settings.practice_order_shuffle", "乱序刷题"},
    {"ui.settings.wrong_book_ratio", "错题移出比例"},
    {"ui.settings.wrong_book_ratio_unit", "倍"},
    {"ui.settings.wrong_book_ratio_hint", "答对次数达到答错次数的这个倍数，就自动移出错题本"},
    {"ui.settings.weighted_append", "权重补题间隔"},
    {"ui.settings.weighted_append_unit", "题"},
    {"ui.settings.weighted_append_hint", "每提交这么多题，就在队尾按当前掌握度再补抽一道"},
    {"ui.settings.practice_order_hint", "只影响从「数据统计」页点进题库时的顺序"},
    {"ui.settings.practice_order_wrong", "错题训练"},

    {"ui.quiz.plan_summary", "本轮 {done} / {total} 题"},
    {"ui.finish.title", "本轮刷完"},
    {"ui.finish.headline", "这一轮结束了！"},
    {"ui.finish.praise_high", "几乎全对，太强了"},
    {"ui.finish.praise_mid", "不错！剩下的错题再过一遍就拿下了"},
    {"ui.finish.praise_low", "别急，错题多刷两轮就顺了"},
    {"ui.finish.round_line", "本轮 {done} 题 · 对 {correct} 题 · 正确率 {rate}"},
    {"ui.finish.time_line", "本轮用时 {time} · 最长连对 {streak} 题"},
    {"ui.finish.total_line", "累计刷题 {count} 题 · 连续 {days} 天"},
    {"ui.finish.wrong_line", "这一轮还剩 {count} 道错题"},
    {"ui.finish.no_wrong", "这个题库已经没有错题了，漂亮！"},
    {"ui.finish.again", "再刷一轮"},
    {"ui.finish.result", "本次结果"},
    {"ui.finish.home", "返回首页"},
    {"ui.stats.reset", "重置统计"},
    {"ui.stats.reset_confirm", "再点一次就清零：累计刷题、时长、连续天数都会重来"},
    {"ui.stats.reset_done", "累计统计已重置"},
    {"ui.settings.file_hint", "设置文件：{path}"},
    {"ui.settings.data_dir", "数据目录：{path}"},
    {"ui.settings.font_path", "字体：{path}"},
    {"ui.settings.font_builtin", "内置英文字体"},
    {"ui.settings.theme_bg", "底色"},
    {"ui.settings.theme_text", "文字"},
    {"ui.settings.theme_controls", "控件"},
    {"ui.settings.theme_status", "状态"},
    {"ui.settings.theme_preset_page", "预设与应用"},
    {"ui.settings.theme_expand_hint", "点分组标题可以展开 / 收起，一页看完所有颜色，不用来回跳页"},
    {"ui.settings.color_input", "输入框底"},
    {"ui.settings.color_border", "边框线"},
    {"ui.settings.color_text_muted", "次要文字"},
    {"ui.settings.color_text_accent", "强调文字"},
    {"ui.settings.color_button_text", "主按钮文字"},
    {"ui.settings.color_button2", "次按钮底"},
    {"ui.settings.color_icon", "图标色"},
    {"ui.settings.color_scrollbar", "滚动条"},
    {"ui.settings.color_partial", "部分分"},
    {"ui.settings.color_draft", "未提交"},
    {"ui.settings.color_star", "收藏星标"},
    {"ui.settings.color_common", "常用配色（快速调整）"},
    {"ui.settings.color_hint", "点色块取色，也可以直接填 RRGGBB"},
    {"ui.settings.theme_reset", "恢复默认配色"},
    {"ui.settings.theme_reset_done", "配色已恢复默认"},
    {"ui.settings.theme_copy", "复制主题文本"},
    {"ui.settings.theme_import_hint", "把主题文本粘到下面，再点「应用」"},
    {"ui.settings.theme_apply", "应用"},
    {"ui.settings.theme_applied", "主题文本已应用：{count} 项"},
    {"ui.settings.version", "版本：{version}"},
    {"ui.settings.section_ui", "界面"},
    {"ui.settings.aspect", "界面比例"},
    {"ui.settings.aspect_auto", "跟随窗口"},
    {"ui.settings.aspect_hint", "界面按这个比例显示；Windows 上窗口也会一起改"},
    {"ui.settings.home_background", "首页背景"},
    {"ui.settings.background_none", "无"},
    {"ui.settings.background_doppelaxt", "双斧"},
    {"ui.settings.background_hellebarde", "长戟"},
    {"ui.settings.background_custom", "自定义"},
    {"ui.settings.background_import", "导入图片"},
    {"ui.settings.background_remove", "移除背景"},
    {"ui.settings.background_mode", "显示模式"},
    {"ui.settings.background_auto", "自动"},
    {"ui.settings.background_tint", "主题染色"},
    {"ui.settings.background_color", "原彩"},
    {"ui.settings.background_opacity", "不透明度"},
    {"ui.settings.background_fit", "铺放方式"},
    {"ui.settings.background_cover", "铺满裁切"},
    {"ui.settings.background_contain", "完整显示"},
    {"ui.settings.background_coverage", "覆盖范围"},
    {"ui.settings.background_top", "顶部"},
    {"ui.settings.background_bank", "题库"},
    {"ui.settings.background_bottom", "底部"},
    {"ui.settings.background_bank_required", "题库区始终覆盖"},
    {"ui.settings.background_imported", "已导入首页背景"},
    {"ui.settings.background_failed", "首页背景导入失败"},
    {"ui.settings.background_too_large", "背景图片过大（上限 32 MB）"},
    {"ui.theme.switched", "主题更改为"},
    {"ui.settings.preset", "主题预设"},
    {"ui.settings.color_section", "配色"},
    {"ui.settings.color_bg", "窗口底色"},
    {"ui.settings.color_panel", "面板底色"},
    {"ui.settings.color_text", "文字"},
    {"ui.settings.color_button", "按钮"},
    {"ui.settings.color_accent", "强调色"},
    {"ui.settings.color_correct", "答对"},
    {"ui.settings.color_wrong", "答错"},
    {"ui.settings.page_answering", "答题"},
    {"ui.settings.page_interface", "界面"},
    {"ui.settings.page_theme", "主题配色"},
    {"ui.settings.page_prompt", "提示词"},
    {"ui.settings.page_plugins", "插件"},
    {"ui.settings.back", "返回"},
    {"ui.settings.not_implemented", "本页尚未实现"},
    {"ui.settings.prompt", "提示词"},
    {"ui.settings.prompt_select", "选择提示词"},
    {"ui.settings.prompt_copy_hint", "也可以按 Ctrl+C"},
    {"ui.settings.prompt_copy_failed", "复制失败：当前平台不支持剪贴板"},
    {"ui.settings.prompt_preview", "预览"},
    {"ui.settings.prompt_empty", "没有找到提示词：把 .txt 放进 assets/prompt 或 data/prompt"},
    {"ui.settings.preset", "主题预设"},
    {"ui.settings.preset_import", "导入"},
    {"ui.settings.preset_applied", "已导入主题：{name}，{count} 个颜色"},
    {"ui.settings.preset_hint", "选中即刻生效；想微调就展开上面四个分组逐个改"},
    {"ui.settings.preset_empty", "没有找到主题预设：把 themes.txt 放进 assets/presets 或 data/presets"},
    {"ui.quiz.auto_next_hint", "即将自动跳到下一题…"},
    {"ui.quiz.type_fill", "填空题"},
    {"ui.quiz.type_indeterminate", "不定项选择题"},
    {"ui.quiz.type_ordered", "多项有序选择题"},
    {"ui.quiz.type_material", "材料题"},
    {"ui.quiz.fill_placeholder", "在这里输入答案"},
    {"ui.quiz.fill_hint", "忽略空格与大小写"},
    {"ui.quiz.fill_submitted", "本题已作答"},
    {"ui.quiz.ordered_pool", "备选：点每个空右边的字母"},
    {"ui.quiz.ordered_slot", "空 {index}"},
    {"ui.quiz.material_title", "材料"},
    {"ui.quiz.material_part", "第 {part} / {count} 小题"},
    {"ui.quiz.paper_summary", "本段材料共 {count} 小题"},
    {"ui.quiz.submit_paper", "提交本页"},
    {"ui.quiz.paper_filled", "已填 {filled} / {count} 小题"},
    {"ui.quiz.paper_submitted", "已提交 {count} 小题"},
    {"ui.quiz.paper_part", "第 {index} 小题"},
    {"ui.quiz.answered", "已答"},
    {"ui.quiz.unanswered", "未答"},
    {"ui.quiz.prev_paper", "上一段材料"},
    {"ui.quiz.next_paper", "下一段材料"},
    {"ui.quiz.result_partial", "部分正确，得分 {score}"},
    {"ui.quiz.answer_is", "正确答案：{answer}"},
    {"ui.exam.pick_title", "选择试卷"},
    {"ui.exam.start", "开始考试"},
    {"ui.exam.continue", "继续考试"},
    {"ui.exam.paper_line", "{count} 题 · 满分 {score} 分 · 限时 {minutes} 分钟"},
    {"ui.exam.paper_line_no_time", "{count} 题 · 满分 {score} 分 · 不限时"},
    {"ui.exam.no_bank", "先选一个题库再进考试模式"},
    {"ui.exam.no_paper", "这个题库没有出卷规则（题库文件里要写 [exam] 与 [draw] 段）"},
    {"ui.exam.remaining", "剩余 {time}"},
    {"ui.exam.answered_line", "已作答 {answered} / {total} 题"},
    {"ui.exam.submit", "交卷"},
    {"ui.exam.submit_ask", "还有 {count} 题没作答"},
    {"ui.exam.submit_ask_done", "全部作答完毕"},
    {"ui.exam.keep_going", "继续答题"},
    {"ui.exam.result_title", "考试成绩"},
    {"ui.exam.score_line", "{earned} / {total} 分"},
    {"ui.exam.pass", "及格"},
    {"ui.exam.fail", "不及格"},
    {"ui.exam.no_pass_line", "本卷不判及格"},
    {"ui.exam.summary", "答对 {correct} / {count} 题 · 用时 {time}"},
    {"ui.exam.answered_summary", "交卷时作答 {answered} 题，没作答的按 0 分算"},
    {"ui.exam.line_score", "{got} / {score} 分"},
    {"ui.exam.line_correct", "对"},
    {"ui.exam.line_wrong", "错"},
    {"ui.exam.line_unanswered", "未作答"},
    {"ui.exam.review", "成绩单"},
    {"ui.exam.review_hint", "点题回看"},
    {"ui.exam.prep_title", "准备考试"},
    {"ui.exam.prep_start", "开始答题"},
    {"ui.exam.prep_back", "返回"},
    {"ui.exam.prep_pass", "及格线 {pass}%"},
    {"ui.exam.prep_rules", "答题时可以随时改答案；交卷才判分，错题会进错题本"},
    {"ui.exam.prep_auto", "到点自动交卷"},
    {"ui.exam.home", "返回主页"},
    {"ui.exam.exit", "退出考试"},
    {"ui.exam.exit_ask", "退出这场考试不记成绩，确定吗？"},
};

}  // namespace

I18n::I18n() {
    useBuiltin();
}

void I18n::useBuiltin() {
    strings_.clear();
    language_ = "zh-CN";
    languageName_ = "简体中文";
    usingBuiltin_ = true;
}

bool I18n::loadPackText(const std::string& text, const std::string& sourceName, std::string* outError) {
    std::vector<io::Issue> issues;
    const std::vector<io::Line> lines = io::parse(text, issues);

    std::map<std::string, std::string> loaded;
    std::string code;
    std::string name;
    std::string section;

    for (const io::Line& line : lines) {
        if (line.isSection) {
            section = line.section;
            continue;
        }
        if (section == "lang") {
            if (line.key == "code") {
                code = io::trim(line.value);
            } else if (line.key == "name") {
                name = io::trim(line.value);
            }
        } else if (section == "text") {
            loaded[line.key] = line.value;
        }
    }

    if (loaded.empty()) {
        if (outError != nullptr) {
            *outError = sourceName + " 里没有任何 [text] 文案";
        }
        return false;
    }

    strings_ = loaded;
    language_ = code.empty() ? sourceName : code;
    languageName_ = name.empty() ? language_ : name;
    usingBuiltin_ = false;
    return true;
}

const char* I18n::text(const std::string& key) const {
    const auto it = strings_.find(key);
    if (it != strings_.end()) {
        return it->second.c_str();
    }
    for (const auto& entry : kBuiltinTexts) {
        if (key == entry.first) {
            return entry.second;
        }
    }
    return key.c_str();  // 兜底：直接把 key 显示出来，方便发现漏翻
}

std::string I18n::format(const std::string& key,
                         const std::vector<std::pair<std::string, std::string>>& args) const {
    std::string result = text(key);
    for (const auto& arg : args) {
        const std::string placeholder = "{" + arg.first + "}";
        size_t pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos) {
            result.replace(pos, placeholder.size(), arg.second);
            pos += arg.second.size();
        }
    }
    return result;
}

std::vector<std::string> I18n::builtinLanguageCandidates() {
    return {"zh-CN", "en"};
}

std::vector<std::string> I18n::allKeys() {
    std::vector<std::string> keys;
    keys.reserve(sizeof(kBuiltinTexts) / sizeof(kBuiltinTexts[0]));
    for (const auto& entry : kBuiltinTexts) {
        keys.emplace_back(entry.first);
    }
    return keys;
}

}  // namespace quiz::i18n

# 题库格式修正提示词

本目录的提示词随项目公开，采用项目 MIT License。

| 文件 | 语言 |
| --- | --- |
| `prompt-cn.txt` | 简体中文 |
| `prompt-en.txt` | English |

用途：把用户提供的原始题库整理成题斩 / AxeQuiz 可直接导入的 BankFormat v1 纯文本。

维护要求：

- 与 `docs/题库格式.md` 和 `core/src/io/BankFormat.cpp` 同步更新；
- 新增题型、考试卷字段或抽题规则时，中英文两份必须同时修改；
- 默认要求输出纯文本，不使用 Markdown 代码围栏；
- 不引导模型修改题目语义、猜测答案或虚构考试卷。

完整题库格式见 [`docs/题库格式.md`](../../docs/题库格式.md)。

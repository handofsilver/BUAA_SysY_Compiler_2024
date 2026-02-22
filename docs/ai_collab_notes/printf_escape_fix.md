# printf 与字符串转义字符修正记录

## 问题

1. `printf("hello\n");` 运行时没有输出换行，而是输出了反斜杠和字母 `n`。
2. `char s[] = "a\nb"` 若不做转义，数组里存 4 个字符（`a`、`\`、`n`、`b`），与规范语义不符，会导致如统计字符串长度等逻辑错误。

## 原因

词法分析器（`Lexer::GetStringConst` / `GetCharConst`）原先对字符串/字符常量**不做转义**：源码中的 `\n` 被存成两个字符 `\` 和 `n`。同一套 StringConst 既用于 printf 格式串，也用于 ConstInitVal/InitVal 的数组初值，因此两处都会出错。

## 修正方案（词法统一转义）

按 SysY 规范（字符串/字符中仅可能出现一种转义 `\n`，用以标注换行），在**词法阶段**对字符串常量和字符常量统一做 `\n` 转义：

- **Lexer::GetStringConst**：扫描到 `\` 且下一字符为 `n` 时，向 token value 追加一个换行符 `'\n'` 并前进 2 个源字符；否则按单字符追加。
- **Lexer::GetCharConst**：同样将 `\n` 解析为一个换行符存入 token value。

这样：

- **printf**：`format_string` 已是转义后的串，IR 生成直接交给 `EmitGlobalStringLiteral`，运行时正确换行。
- **数组初值**：`char s[] = "a\nb"` 的初值字符串也是转义后的，长度与语义一致，不会出现“长度多 1”等逻辑错误。

IR 层不再需要对 printf 做额外转义（此前在 `VisitPrintfStmt` 中的 `UnescapePrintfLiteral` 已移除）。

## 修改文件

- **include/Lexer.h**：在类注释中说明字符串/字符常量在词法中解析 `\n` 为换行。
- **src/Lexer.cpp**：重写 `GetStringConst`、`GetCharConst` 的循环，遇 `\n` 时写入 `'\n'` 并 `cur_pos_ += 2`。
- **src/IRGenVisitorStmt.cpp**：删除 `UnescapePrintfLiteral` 及两处调用，直接使用 `literal` 调用 `EmitGlobalStringLiteral`。
- **docs/design_documents/lexer.md**：见下文“说明文档”更新。

## 参考

- `docs/course_info/2024_SysY_detailed.md`：`<StringConst>` / `<CharConst>` 定义；字符串中仅可能出现一种转义字符 `\n`，用以标注此处换行。

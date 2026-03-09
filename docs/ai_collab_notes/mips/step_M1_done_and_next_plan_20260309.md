# M1 验收完成 + 下一步计划

**Date**: 2026-03-09
**本步**：纯算术 + return（单函数、单块）— 已验收并规范化。

---

## 一、本步验收结论

| 项目 | 状态 |
|------|------|
| `return 1+2` 在 MARS 中运行正确（返回值 3） | ✅ |
| BuildStackFrame：value_offset_ + frame_size（$ra + BinaryInst 结果） | ✅ |
| LoadValueToReg：ConstantInt → li，其它 → lw offset($sp) | ✅ |
| EmitBinaryInst：ADD/SUB/MUL/DIV/REM 正确映射（DIV/REM 用 div+mflo/mfhi） | ✅ 已规范化 |
| EmitReturnInst + 单次 Epilogue（在遇到 ReturnInst 时输出，避免重复） | ✅ 已修复 |
| 输出格式：__start、4 格缩进、助记符对齐 | ✅ |
| 临时“打印 main 返回值”：可关闭开关 `k_emit_debug_print_main_ret` | ✅ 已加 |

**规范化修改摘要**：
- **重复 epilogue**：main 的 epilogue 只在 EmitBody 遇到 ReturnInst 时输出一次，MipsEmitter 不再对 main 调用 `EmitEpilogue()`。
- **DIV/REM**：按 MIPS 规范改为 `div $t0, $t1` + `mflo $t2` / `mfhi $t2`（不再用伪指令 div/rem 三操作数形式，保证 MARS 兼容）。
- **头文件**：FunctionEmitter.cpp 增加 `#include "ir/Constant.h"`、`#include <cassert>`。
- **临时调试**：`k_emit_debug_print_main_ret` 为 true 时，在 `jal main` 后插入“打印 $v0 + 换行”的 syscall，便于在 MARS 控制台看到 main 返回值；设为 false 即关闭。

---

## 二、临时调试代码说明（可随时关闭）

**位置**：`src/mips/MipsEmitter.cpp` 中匿名命名空间内：

```cpp
const bool k_emit_debug_print_main_ret = true;  // 改为 false 即关闭
```

**效果**：在 `jal main` 之后、`li $v0, 10` 之前，多发射：
- 注释一行
- `move $a0, $v0`
- `li $v0, 1` + `syscall`（打印整数）
- `li $v0, 11` + `li $a0, 10` + `syscall`（换行）

关闭后删除或不再使用：将 `k_emit_debug_print_main_ret` 设为 `false` 即可；若完全移除该逻辑，可删掉 `if (k_emit_debug_print_main_ret) { ... }` 整块。

---

## 三、下一步计划（三选一，建议顺序）

### 选项 A：M2 — .data 段 + 全局变量（推荐先做）

**目标**：支持全局 `int a;` / `int a = 5;`，main 里 `return a;` 或 `a = 1; return a;` 能正确生成并运行。

**要点**：
- MipsEmitter 增加 `EmitDataSegment()`：遍历 `module_.GetGlobalVars()`，输出 `.data`、标签、`.word`（初值或 0）。
- FunctionEmitter：`LoadValueToReg` 中识别 `GlobalVar` → `la reg, label` + `lw reg, 0(reg)`（或直接 lw label 若汇编器支持）；StoreInst 若目标为全局 → `sw` 到全局标签。
- 先只做标量 int，数组与字符串可再下一步。

**子步建议**：M2a 只发射 .data 段（含一个全局变量的 .word）；M2b LoadValueToReg 支持 GlobalVar；M2c 实现 EmitStoreInst（目标为全局时）。

---

### 选项 B：M4 — 多基本块 + 分支（if/for）

**目标**：支持 `if (cond) ... else ...`、简单 for；即 BranchInst（条件/无条件）、IcmpInst、块标签。

**要点**：
- 为每个 BasicBlock 输出 MIPS 标签（如 `main_entry`、`main_if_then`）；EmitBody 按块遍历，先打标签再遍历块内指令。
- EmitBranchInst：无条件 `j label`；条件分支 `lw cond, offset($sp)`、`bnez/j label`。
- EmitIcmpInst：slt/seq/sne 等，结果写栈槽；EmitZextInst 本步可视为拷贝（i1 已按 i32 存）。
- Phi 可暂不处理（只做“单前驱”或简单情形），或放到 M5。

**子步建议**：M4a 多块 + 块标签 + 无条件 br；M4b IcmpInst + 条件 br；M4c 简单 phi（若需要）。

---

### 选项 C：M3 — 库函数调用（getint/putint/putch/putstr）

**目标**：main 里能调用 `getint()`、`putint(i)`、`putch(c)`、`putstr`，对应 MARS syscall 5/1/11/4。

**要点**：
- EmitCallInst：若 callee 为库函数名，生成对应 syscall 序列（参数从栈或 $a0 取）；否则留作“用户函数调用”后续做。
- 字符串字面量：.data 段中 `.asciiz "..."`，putstr 的 $a0 传该标签。

**子步建议**：M3a putint(getint()) 单用例；M3b 加上 putch(10)、putstr。

---

## 四、推荐顺序与当前状态

- **当前**：M1 已完成（单块、BinaryInst + ReturnInst、栈帧 + value_offset_）。
- **推荐下一步**：**M2（.data + 全局变量）**，再 M3 或 M4。这样“数据从哪来”先打通，再打“控制流”或“IO”。
- 若你更想先看到 if/for 跑通，也可先做 **M4**，再回头做 M2/M3。

完成下一步后，同样可以贴关键改动（EmitDataSegment、LoadValueToReg 对 GlobalVar、或 Branch/Icmp 等），再一起验收并定再下一步。

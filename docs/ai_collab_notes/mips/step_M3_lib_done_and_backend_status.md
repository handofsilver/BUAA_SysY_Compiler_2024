# M3 库函数完成 + 后端整体进度

**Date**: 2026-03-09
**本步**：库函数 getint / getchar / putint / putch / putstr（MARS syscall）+ .data 字符串字面量 + 用户函数 jal 调用。

---

## 一、本步完成内容

### 1. .data 段：i8 数组（字符串字面量）

- **位置**：`MipsEmitter::EmitDataSegment()`
- **逻辑**：对全局数组，若元素类型为 **i8**（`IntegerType` 且 `GetBits()==8`），按“字符串”处理：
  - 有初值（`ConstantArray`）：从各元素（`ConstantInt`）拼出字符串，对 `\`、`"`、`\n` 转义后输出 **`.asciiz "..."`**
  - 无初值：输出 **`.space n`**（n 为元素个数，字节）
- **用途**：`printf("%d\n", ...)` 降级为 putstr + putint，putstr 的实参为 `EmitGlobalStringLiteral` 得到的全局（如 `.str.0`），.data 中需有对应 `.asciiz`，标签为 `global_<name>`（与 FunctionEmitter 中 `LoadValueToReg(GlobalVar)` 的 `la` 一致）。

### 2. BuildStackFrame：CallInst 结果槽

- **位置**：`FunctionEmitter::BuildStackFrame()`
- **逻辑**：遍历指令时，若为 **CallInst** 且**返回类型非 void**（`!dynamic_cast<VoidType*>(call->GetType())`），为其分配 4 字节结果槽，写入 `value_offset_`。
- **用途**：getint/getchar 的返回值需存到栈槽，供后续指令使用。

### 3. EmitCallInst / EmitLibraryFunctionCall

- **库函数**（由 `EmitLibraryFunctionCall` 处理）：
  - **getint**：`li $v0, 5`；`syscall`；`sw $v0, result_slot($sp)`
  - **getchar**：`li $v0, 12`；`syscall`；`sw $v0, result_slot($sp)`
  - **putint**：`LoadValueToReg(arg0, "$a0")`；`li $v0, 1`；`syscall`
  - **putch**：`LoadValueToReg(arg0, "$a0")`；`li $v0, 11`；`syscall`
  - **putstr**：`LoadValueToReg(arg0, "$a0")`（字符串地址）；`li $v0, 4`；`syscall`
- **用户函数**（在 `EmitCallInst` 中）：
  - 前 4 个实参依次 `LoadValueToReg(arg_i, "$a0")` … `"$a3"`
  - 若实参个数 > 4：在 jal 前 `addiu $sp, $sp, -(extra*4)`，将第 5、6… 个实参 `sw` 到 `0($sp)`, `4($sp)`…，jal 后恢复 `addiu $sp, $sp, extra*4`
  - `jal <函数名>`
  - 若返回类型非 void，`sw $v0, value_offset_(call)($sp)`
- **被调用方**：`BuildStackFrame` 为前 4 个形参分配栈槽并在 prologue 中 `sw $a0`…`$a3` 写入；第 5 个及以后形参的 `value_offset_` 设为 `frame_size + (i-4)*4`（caller 传入的栈区），供 `LoadValueToReg` 使用。
- **约定**：与课程/MARS 一致；超过 4 个参数已支持（caller 栈传参，callee 从 frame_size($sp) 起读）。

### 4. 涉及文件

| 文件 | 变更摘要 |
|------|----------|
| `src/mips/MipsEmitter.cpp` | `.data` 中按 `IsArray()` + 元素类型区分；i8 数组用 `.asciiz "..."` / `.space n` |
| `src/mips/FunctionEmitter.cpp` | BuildStackFrame 为“非 void CallInst”分配槽；EmitCallInst 分支库函数/用户函数；EmitLibraryFunctionCall 实现 5 个库函数；用户函数 jal + 存 $v0 |

---

## 二、验收样例（你本地 Run）

- `int main(){ int c = getint(); printf("%d\n", c); return c; }`：输入一整数，输出同一整数并返回该值。
- `int main(){ putch(10); return 0; }`：输出换行。
- `int main(){ putstr("hello"); return 0; }`：输出 hello（.data 中应有对应 .asciiz）。
- 用户函数：`int f(int a,int b){ return a+b; } int main(){ return f(1,2); }` → 返回值 3。

---

## 三、后端整体进度与剩余项

### 3.1 已完成的里程碑（按顺序）

| 阶段 | 内容 | 状态 |
|------|------|------|
| **M0** | 骨架：入口、__start、.text、main 空体、MARS 可跑 | ✅ |
| **M1** | 单块、算术（BinaryInst）、ReturnInst、栈帧、value_offset_、loadValueToReg | ✅ |
| **M2** | .data 标量/数组全局；Load/Store 对 GlobalVar；GEP base=GlobalVar | ✅ |
| **M6** | 局部数组：AllocaInst 栈上分配、GEP base=Alloca、Load/Store 通过 GEP | ✅ |
| **M4** | 多块、块标签、BranchInst（条件/无条件）、IcmpInst、ZextInst、TruncInst；return 即 epilogue | ✅ |
| **M5** | Phi 节点下降：前驱末尾按依赖顺序写 phi 槽 | ✅ |
| **M3** | 库函数 getint/getchar/putint/putch/putstr；.data 字符串；用户函数 jal + $a0–$a3 + $v0 | ✅ |

### 3.2 当前能力概览

- **程序结构**：CompUnit、全局变量/常量、多函数、main 入口、__start + exit。
- **数据类型**：int、char（i8）、一维/二维数组（全局与局部）、字符串字面量。
- **控制流**：if/else、while/for、多基本块、条件/无条件分支、phi。
- **表达式**：算术、比较、逻辑（通过 icmp+zext）、数组下标、函数调用（库 + 用户）。
- **I/O**：getint、getchar、putint、putch、putstr（printf 降级为 putstr+putint 等）。

在“全栈分配、前 4 个参数用 $a0–$a3、无参数溢出、无优化”的前提下，课程要求的 **MIPS 目标代码生成** 已可覆盖绝大部分 SysY 单文件程序。

### 3.3 剩余/可选项（非必须）

| 项目 | 说明 | 优先级 |
|------|------|--------|
| **参数 > 4 个** | 已实现：caller 栈传第 5+ 实参，callee 形参槽与 prologue 保存 $a0–$a3，第 5+ 形参从 frame_size($sp) 读 | ✅ |
| **char 读写** | 全局/局部 char 用 .byte / lb/sb 等（若课程要求与 int 区分） | 可选 |
| **优化接口** | 预留“关闭调试打印”“开启寄存器分配/窥孔”等开关，与课程“竞速/优化”要求对接 | 建议预留 |
| **鲁棒性** | 非法/边界输入、错误处理不依赖前端时，后端可做简单防御性检查 | 可选 |

---

## 四、小结

- **M3**：库函数 5 个 + .data 字符串 + 用户函数 jal 已实现；BuildStackFrame 为有返回值的 CallInst 分配槽。
- **后端主流程**：M0→M1→M2→M6→M4→M5→M3 已全部打通，可生成满足课程要求的 mips.txt，在 MARS 4.5 上运行常规 SysY 单文件程序。
- **后续**：视课程与评测需求，补“多参数栈传参”或“优化开关/简单优化”即可收尾。

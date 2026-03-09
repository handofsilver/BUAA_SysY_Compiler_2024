# M6 验收完成 + 下一阶段计划

**Date**: 2026-03-09  
**本步**：局部数组（AllocaInst 栈上分配 + GEP base=Alloca + Load/Store）— 已验收通过。

---

## 一、M6 验收结论

| 项目 | 状态 |
|------|------|
| BuildStackFrame：$ra 后先处理 entry 块前导 AllocaInst（size 4 或 N×4），再为 Binary/Load/GEP 分配结果槽 | ✅ |
| LoadValueToReg(AllocaInst) → addiu reg, $sp, offset | ✅ |
| GEP base=Alloca 时 base 为 $sp+offset，index×4 写回 GEP 槽 | ✅（沿用现有 GEP） |
| Load/Store 的 ptr 为 GEP 结果时，从栈槽取地址再 lw/sw 0(reg) | ✅ |
| 样例 `int a[3]={1,2,3}; a[1]=99; return a[1];` 在 MARS 输出 **99** | ✅ |

**规范化**：`FunctionEmitter.cpp` 已补 `#include <cassert>`（EmitBinaryInst 中 default 分支使用 assert）。

**约定（已满足）**：当前实现假定“所有 AllocaInst 均在 entry 块最前面”，遇非 alloca 即 break；与 Mem2Reg 前 IR 及 SysY 习惯一致。若将来 IR 出现非 entry 的 alloca，再扩展遍历即可。

---

## 二、下一阶段：M4 控制流（多基本块 + 分支）

**目标**：支持 if/else、for 等控制流，即多基本块、条件/无条件分支、比较指令；为后续 M5（phi）和更复杂用例打基础。

### 2.1 实现思路（伪代码级）

**（1）块标签与发射顺序**

- 每个 BasicBlock 对应一个 MIPS 标签，全局唯一。约定：`函数名_块名`，块名中的 `.` 替换为 `_`（MARS 标签限制）。
- EmitBody 按 **块顺序** 遍历：先输出当前块标签，再按指令顺序发射该块内指令；遇到 BranchInst 时发射跳转（并在需要时为先驱块到后继块的 phi 预留插入点，M5 再做）。

**（2）BranchInst**

- **无条件** `br label %dest`：  
  `j <dest_label>`
- **条件** `br i1 %cond, label %if_true, label %if_false`：  
  用 LoadValueToReg 将 cond 加载到临时寄存器，`bnez reg, if_true_label`，`j if_false_label`。

**（3）IcmpInst**

- 产生 i1（在栈上仍占 4 字节，0/1）：  
  LoadValueToReg(lhs), LoadValueToReg(rhs)，按谓词发 MIPS 比较（slt/sltu/seq/sne 等或伪指令），结果写入 IcmpInst 的结果槽。  
- 需在 BuildStackFrame 中为 IcmpInst 分配 4 字节结果槽；EmitBody 中派发 IcmpInst，调用 EmitIcmpInst。

**（4）ZextInst**

- i1→i32 在全栈分配下多为 no-op（复制栈槽）；若实现简单，可直接将源槽拷贝到目标槽（或 load + sw 到目标 offset）。

**（5）多块下的 Epilogue**

- 单块时可在 EmitBody 后统一调一次 EmitEpilogue()。**多块时**不能这样：若某块以 `ret` 结束，必须在该块内“加载 \$v0”后**立即**输出 epilogue（lw \$ra、addiu \$sp、jr \$ra），否则会“落空”到下一块。约定：在 EmitBody 中**遇到 ReturnInst 时**先 EmitReturnInst（加载 \$v0），再**立即**调用 EmitEpilogue() 输出三条指令；MipsEmitter 在 EmitBody() 之后**不再**调用 fe.EmitEpilogue()。这样每条 return 路径都会正确返回。

### 2.2 子步骤与验收样例（预期你本地 Run 验证）

| 子步 | 内容 | 样例与预期 |
|------|------|------------|
| **M4a** | 多块 + 块标签；EmitBody 按块遍历，每块先打标签再发射指令；无条件 `br label %dest` → `j label` | `int main(){ if(1) return 2; return 3; }` 或等价两块、无条件跳到 return 2 → 返回值 **2** |
| **M4b** | IcmpInst：BuildStackFrame 为 Icmp 分配槽；EmitIcmpInst(slt/eq/ne/…)，结果写栈槽 | `int main(){ int a=1,b=2; if(a<b) return 10; return 20; }` → 返回值 **10** |
| **M4c** | 条件 BranchInst：cond 从栈槽加载，bnez/j 或 beqz/j | 同上或 `if(a>=b) return 20; return 10;` → 根据条件返回 10 或 20 |
| **M4d** | ZextInst（若 IR 中有 i1→i32）：复制或 load+sw 到目标槽 | 含比较的 if 通常会产生 zext，用 M4b/M4c 的样例一起验证即可 |

**建议顺序**：M4a（多块+无条件 br）→ M4b（Icmp）→ M4c（条件 br）→ M4d（Zext，可与 M4b/M4c 同测）。

### 2.3 验收样例汇总（你本地跑）

- `int main(){ if(1) return 2; return 3; }` → **2**
- `int main(){ int a=1,b=2; if(a<b) return 10; return 20; }` → **10**
- `int main(){ int a=1,b=2; if(a>=b) return 10; return 20; }` → **20**
- `int main(){ int i=0; while(i<3) i=i+1; return i; }` → **3**（若已做简单 while/for，可作扩展验收；否则可留到 M4 完成后）

---

## 三、再下一阶段（M5 / M3）简述

- **M5 Phi**：在多前驱汇合点，在**各前驱块末尾**（跳转前）为该前驱对应的 incoming 插入“move”到 phi 结果槽；必要时两阶段 parallel copy（先 load 再 store）避免覆盖。  
  验收例：`int main(){ int a=getint(), b; if(a>0) b=1; else b=2; return b; }`（b 为 phi）→ 根据 a 返回 1 或 2（需 M3 getint 或改用手写常量测 phi 路径）。

- **M3 库函数**：getint/putint/putch/putstr 对应 MARS syscall；CallInst 中识别库函数名并发射对应 syscall 序列，参数从栈/$a0 取。  
  验收例：`int main(){ putint(getint()); return 0; }` 等。

建议路线：**M4（控制流）→ M5（phi）→ M3（库函数）**，这样 if/for 和 phi 先通，再补 I/O，测试用例更灵活。

---

## 四、涉及文件与接口（M4 时可能改动）

- **FunctionEmitter**：BuildStackFrame 中为 IcmpInst 分配 4 字节；EmitBody 中按块循环、块内派发 BranchInst / IcmpInst / ZextInst；新增/实现 EmitBranchInst、EmitIcmpInst、EmitZextInst；块标签名需与 Module/Function 内块名一致（如 `main_entry`、`main_if_then`）。
- **MipsEmitter**：若 .text 中非 main 函数也走 FunctionEmitter，需保证非 main 的 epilogue 仍只在 return 路径上输出一次（与当前 main 一致即可）。

以上为 M6 验收结论与下一阶段（M4 控制流）的实现思路与目标；子步骤与样例按上表在本地验证即可。

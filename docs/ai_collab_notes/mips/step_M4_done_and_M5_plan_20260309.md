# M4 控制流完成 + 下一阶段计划

**Date**: 2026-03-09
**本步**：多基本块、BranchInst（条件/无条件）、IcmpInst、ZextInst、TruncInst；每条 return 路径立即输出 epilogue。

---

## 一、本次完成的修改（M4 收尾）

| 修改 | 说明 |
|------|------|
| **BuildStackFrame** | 为 IcmpInst、ZextInst、TruncInst 分配 4 字节结果槽 |
| **EmitBody** | 遇到 ReturnInst 时先 EmitReturnInst，再**立即**调用 EmitEpilogue()，保证多块下每条 return 路径都正确返回 |
| **MipsEmitter** | 不再在 EmitBody() 后统一调用 fe.EmitEpilogue()；所有有体函数均走 FunctionEmitter（不再仅 main） |
| **EmitCallInst** | 增加空实现占位，避免链接错误；M3 再实现 |
| **TruncInst** | 发射 andi $t0, $t0, 0xFF 再 sw（i32→i8 取低 8 位） |
| **#include &lt;cassert&gt;** | FunctionEmitter.cpp 已包含 |

你已有：块标签（BlockLabel）、EmitBranchInst（bnez/j、j）、EmitIcmpInst（slt/sgt/sle/sge/seq/sne）、EmitZextInst（拷贝栈槽），以上保持不变。

---

## 二、验收样例（你本地 Run 验证）

- `int main(){ if(1) return 2; return 3; }` → **2**
- `int main(){ int a=1,b=2; if(a<b) return 10; return 20; }` → **10**
- `int main(){ int a=1,b=2; if(a>=b) return 10; return 20; }` → **20**
- 含 while/for 的简单循环（如 `int i=0; while(i<3) i=i+1; return i;`）→ **3**（若有 phi 需 M5 再做）

---

## 三、下一阶段：M5 Phi 节点下降

**目标**：正确处理 Mem2Reg 产生的 φ 节点，使“多前驱汇合处的 SSA 值”在每条前驱边上写入正确 incoming，从而 if/else、循环后的变量取值正确。

### 3.1 实现思路（伪代码）

**（1）语义**
`%r = phi i32 [ %a, %pred1 ], [ %b, %pred2 ]` 表示：从 pred1 进入则 r=a，从 pred2 进入则 r=b。MIPS 无 φ 指令，需在**前驱块末尾**（跳转前）把对应 incoming 写入 r 的栈槽。

**（2）收集**
对每个 BasicBlock B，收集“B 内定义的所有 PhiInst”（即 phi 结果属于 B）。对每个 PhiInst，记其 (incoming_value, incoming_block) 列表。

**（3）插入 move**
在发射**前驱块**的指令时，在**该块末尾**（BranchInst 之前）：对该前驱块 P，找出“incoming_block == P”的所有 phi 的 (value, P) 对，对每一对：把 value 加载到临时寄存器，再 store 到 phi 结果槽。
注意**顺序**：若多个 phi 的 incoming 有依赖（如 phi2 的 incoming 是 phi1），需避免覆盖未读的槽。**两阶段**：先把所有 incoming  load 到临时寄存器或临时栈槽，再统一 store 到各 phi 结果槽（或使用 swap 等避免覆盖）。

**（3'）直观例子：为什么顺序重要 + 两阶段在做什么**

假设汇合块 S 里有两个 phi：
- `%b = phi i32 [ 10, %if.then ], [ 20, %if.else ]`  （从 if.then 来则 b=10，从 if.else 来则 b=20）
- `%c = phi i32 [ %b, %if.then ], [ 99, %if.else ]`  （从 if.then 来则 c=b，从 if.else 来则 c=99）

即：从 **if.then** 进入 S 时，应得到 b=10、c=10（因为 c 的 incoming 是 b，而 b 在 if.then 边上为 10）。

- **错误做法**：在 if.then 末尾“按任意顺序”写 phi 槽。若先写 c 再写 b：  
  - 写 c：要写的是“b 的值”，此时 b 的槽还没写，读出来是旧/未定义 → 错。  
  - 再写 b：b=10。  
  结果 c 错了。
- **正确做法**：按**依赖顺序**写——谁被别的 phi 引用，谁先写。这里 b 被 c 引用，所以先写 b 再写 c：  
  - 写 b：LoadValueToReg(10, "\$t0"); sw \$t0, b_slot；  
  - 写 c：LoadValueToReg(b, "\$t0") 即从 b_slot 读，得到 10；sw \$t0, c_slot。  
  结果 b=10、c=10，正确。

“两阶段”的等价做法（当 phi 多、依赖复杂时）：  
- 阶段一：按依赖顺序，把每个 phi 的 incoming 值 load 到**临时**（不同寄存器或临时栈槽），不写 phi 槽；  
- 阶段二：再按同样顺序，把临时 store 到各 phi 结果槽。  
这样不会出现“刚写进 phi1 的槽又被 phi2 的 load 读走时，phi1 的槽还没写”的问题。当前实现采用**按依赖顺序一次 load+store**（先写被依赖的 phi），等价且更省临时空间。

**（4）BuildStackFrame**
为 PhiInst 分配 4 字节结果槽（与其它产生结果的指令一致）。

**（5）EmitBody 中**
PhiInst 在**本块**不发射指令（只在各前驱块末尾发射 move）；EmitBody 遍历到 PhiInst 时跳过即可。在发射某块 P 的**最后一条指令（BranchInst）之前**，先调用“为该块 P 发射所有 phi move（目标块为 P 的后继中、有 phi 且 incoming 来自 P 的）”。
具体：对 P 的每个后继 S，对 S 内每个 PhiInst phi，若存在 (val, P)，则生成“把 val 写入 phi 结果槽”的代码（两阶段避免覆盖）。

### 3.2 验收样例（你本地验证）

- `int main(){ int a=1, b; if(a>0) b=10; else b=20; return b; }` → **10**（b 为 phi，两条路径各写一次 incoming）
- `int main(){ int a=0, b; if(a>0) b=10; else b=20; return b; }` → **20**
- 简单循环：`int main(){ int i=0; while(i<3) i=i+1; return i; }` → **3**（循环出口处 i 为 phi）

---

## 四、再下一阶段：M3 库函数调用

**目标**：支持 getint、getchar、putint、putch、putstr（MARS syscall 5/12/1/11/4）；CallInst 的 callee 为库函数时发射对应 syscall，参数从栈\$a0 取，返回值写\$v0 并存入 CallInst 结果槽（若有）。

### 4.1 实现思路（伪代码）

- **识别库函数**：callee 为 Function*，GetName() 为 "getint"/"getchar"/"putint"/"putch"/"putstr"。
- **BuildStackFrame**：对 CallInst，若返回类型非 void，分配 4 字节结果槽。
- **EmitCallInst**：
  - getint/getchar：无参，li \$v0 5/12，syscall；sw \$v0, result_offset(\$sp)。
  - putint/putch：一个参数，LoadValueToReg(arg0, "\$a0")，li \$v0 1/11，syscall。
  - putstr：一个参数（i8*），LoadValueToReg(arg0, "\$a0")（地址），li \$v0 4，syscall。
- 字符串字面量：.data 段中为 printf/putstr 用到的字符串生成 .asciiz，标签可 str_0, str_1, ...（若 IR 中已有全局常量则复用其标签）。

### 4.2 验收样例

- `int main(){ putint(getint()); return 0; }`：输入整数，输出同一整数。
- `int main(){ putch(10); return 0; }`：输出换行。
- `int main(){ putstr("hello"); return 0; }`：输出 hello（需 .data 中 .asciiz "hello"）。

---

## 五、建议顺序

**M5（Phi）→ M3（库函数）**：先做 M5，则带 if/else、循环的用例（含 phi）可全部跑通；再做 M3，即可用 getint/putint 等写完整测试。

以上为 M4 收尾说明与 M5、M3 的下一阶段计划。

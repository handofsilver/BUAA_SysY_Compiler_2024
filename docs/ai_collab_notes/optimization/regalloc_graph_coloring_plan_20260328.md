# 下一步优化：O7 图着色寄存器分配

**日期**: 2026-03-28
**前置状态**: O3（乘除法强度削减）+ O4（窥孔：sw/lw 消除）+ O5（冗余跳转消除）已接入并通过验证
**关联文档**: [优化阶段规划](optimization_phase_plan_20260310.md)、[MIPS 后端三项优化](mips_backend_opt_20260328.md)、[课程优化教程 §（3）](../../course_info/optimization_course_guide.md)

---

## 一、当前性能瓶颈分析

完成 O3/O4/O5 之后，MIPS 输出已经相当干净。对一个典型的含循环测试用例做指令分布分析，通常能看到以下格局：

| 指令类别 | 比例（典型值） | 是否已优化 |
|---------|-------------|-----------|
| `sw` / `lw`（栈读写） | ~50% | O4 已消除部分相邻对 |
| 算术指令（`addu`/`sll` 等） | ~20% | O3 已将 `mul` 换为移位 |
| `j` / `bnez` / `beqz`（跳转） | ~10% | O5 已消除 fall-through |
| `li`（加载立即数） | ~10% | 无 |
| `jal`/`jr`/`syscall` 等 | ~10% | 无 |

**根本瓶颈**：栈读写仍占大头。O4 的窥孔只能消除**紧邻的** `sw/lw` 对，而实际情况中同一个 SSA 值往往在多个不同指令中被读取——每次读取都需要一次 `lw`，这些不紧邻的冗余加载 O4 覆盖不到。

消除这类冗余的唯一系统性方法是**寄存器分配**：给活跃的 SSA 值分配物理寄存器，持有期间无需经过栈，`lw`/`sw` 只在溢出（spill）时才出现。

---

## 二、图着色寄存器分配概述

### 2.1 为什么选 Chaitin-Briggs

课程教程 §（3）推荐的 Chaitin-Briggs 算法是工业界最经典的图着色方案（LLVM/GCC 早期均使用变体）。与线性扫描相比：

| 比较项 | 线性扫描 | 图着色（Chaitin-Briggs） |
|--------|---------|------------------------|
| 实现复杂度 | 低 | 中-高 |
| 寄存器利用率 | 次优 | 接近最优 |
| 溢出质量 | 一般 | 好（启发式选择代价最小者溢出） |
| 适合阶段 | 快速 JIT | AOT 编译优化 |

对于本项目（AOT 编译，以 FinalCycle 评测），Chaitin-Briggs 是正确选择。

### 2.2 算法七步流程

```
Build  →  Simplify  →  Coalesce  →  Freeze  →  Spill  →  Select  →  Restart
```

| 步骤 | 做什么 |
|------|--------|
| **Build** | 活跃变量分析 → 构建干涉图（interference graph） |
| **Simplify** | 从图中移除度数 < K 的节点并压栈 |
| **Coalesce** | 保守合并 move 指令的源/目的节点以消除冗余 move |
| **Freeze** | 冻结无法合并的低度数 move 相关节点，重回 Simplify |
| **Spill** | 无低度数节点时，启发式选取高代价节点标记为溢出候选 |
| **Select** | 从栈中弹出节点着色；溢出节点插入 load/store 并重启 |
| **Restart** | 若有实际溢出则重跑整个流程（通常 1–2 次即收敛） |

---

## 三、前置工作：MipsInst 结构化表示

### 3.1 为什么寄存器分配需要结构化指令

活跃变量分析需要对每一条 MIPS 指令精确知道：

- `def(inst)`：该指令**写入**哪个寄存器
- `use(inst)`：该指令**读取**哪些寄存器

当前 `AsmWriter` 输出的是字符串，从字符串反向解析 `def`/`use` 集合不仅繁琐，而且脆弱。正确做法是在输出字符串之前，先在内存中保留结构化的指令表示。

因此，**寄存器分配阶段的第一步就是把 `AsmWriter` 的缓冲行从 `string` 升级为 `MipsInst` 结构体**。这也是[MIPS 后端三项优化文档](mips_backend_opt_20260328.md)中延迟到 O7 再做的那个"欠债"。

### 3.2 MipsInst 结构草图

```cpp
// include/mips/MipsInst.h

namespace mips {

    /// Physical register index (0–31 = $zero–$ra; -1 = none).
    using Reg = int;
    constexpr Reg kNoReg = -1;

    /// Opcode categories sufficient for liveness and coloring.
    enum class Opcode {
        // Arithmetic / logical
        ADDU, SUBU, AND, OR, SLL, SRL, SRA, SLT, SEQ, SNE, SGT, SLE, SGE,
        MUL, DIV, MFLO, MFHI,
        // Memory
        LW, SW, LBU, SB,
        // Immediate variants
        ADDIU, LI, LA,
        // Control
        J, JAL, JR, BNEZ, BEQZ,
        // Copy
        MOVE,
        // Other
        SYSCALL,
        // Pseudo / label marker (not a real instruction)
        LABEL,
    };

    struct MipsInst {
        Opcode     op   = Opcode::SYSCALL;
        Reg        dst  = kNoReg;   // destination register (kNoReg if none)
        Reg        src1 = kNoReg;   // first source register
        Reg        src2 = kNoReg;   // second source register
        int64_t    imm  = 0;        // immediate / stack offset
        std::string label;          // target label (for j, jal, beqz, bnez, la)
    };

} // namespace mips
```

所有 `Emit*` 方法填充 `MipsInst` 而非字符串；序列化为文本在 `FlushBuffer`（改名为 `Serialize`）时统一处理。

---

## 四、Build：活跃变量分析

### 4.1 数据流方程

对 MIPS 基本块级别：

$$
\text{in}[B] = \text{use}[B] \cup (\text{out}[B] - \text{def}[B])
$$
$$
\text{out}[B] = \bigcup_{S \in \text{succ}(B)} \text{in}[S]
$$

其中 **use[B]** 为块内**向上暴露的** use（gen），**def[B]** 为块内**所有** def 的并集（kill），二者**不对称**——不可把 def[B] 理解成「定义前未被 use」之类与 use[B] 对偶的集合。

逆后序（reverse postorder）迭代，通常 3–5 轮收敛。

### 4.2 指令级活跃变量

在块级分析收敛后，对每个块从末尾向前扫描，逐指令维护活跃集合：

```
live = out[B]
for inst in B.reversed():
    inst.live_out = live
    live = use(inst) ∪ (live - def(inst))
    inst.live_in  = live
```

这给出了"在每条指令执行时哪些寄存器活跃"，是构造干涉图的直接输入。

---

## 五、Build：干涉图构建

**规则**：对每条指令 `inst`，令 `d = def(inst)`：

- 对每个 `r ∈ live_out(inst)`，若 `r ≠ d`，则 `d` 与 `r` 干涉（加边）
- 对 `move` 指令特殊处理：`move dst, src` 中 `dst` 与 `src` 不加干涉边（它们是 move-related，后续 Coalesce 可能合并）

干涉图用邻接集表示（`unordered_set<Reg>` per node），便于 O(1) 度数查询和邻居枚举。

---

## 六、Simplify / Coalesce / Freeze / Spill / Select

这五个步骤的实现逻辑在课程教程 §（3）中有完整的伪代码，这里只列出关键工程决策：

### 6.1 可用寄存器集合

MIPS 32 个寄存器中，可供分配的物理寄存器为：

| 类型 | 寄存器 | 说明 |
|------|--------|------|
| 临时（caller-saved） | `$t0`–`$t9`（10个） | 调用者保存，可自由分配 |
| 保存（callee-saved） | `$s0`–`$s7`（8个） | 被调用者保存，使用时需 prologue/epilogue 保存恢复 |
| 合计 | **18个** | K = 18 |

`$zero`、`$at`、`$v0`–`$v1`、`$a0`–`$a3`、`$k0`–`$k1`、`$gp`、`$sp`、`$fp`、`$ra` 均为预留，不参与图着色。

### 6.2 Coalesce 策略

采用 **George 合并准则**（虎书）：合并 `move x, y` 当且仅当对 y 的每个邻居 t，要么 `degree(t) < K`，要么 `t` 与 x 已经干涉。这保证合并后不会使原本可着色的图变得不可着色。

### 6.3 溢出代价启发式

选择溢出代价最小的节点，代价公式参考循环深度：

$$
\text{cost}(v) = \frac{\text{def\_use\_count}(v)}{degree(v)} \times 10^{\text{loop\_depth}}
$$

循环深度信息来自后端 CFG 上的简单循环检测（识别回边即可，不需要完整的循环分析框架）。

### 6.4 溢出代码插入

溢出节点 v 处理方式：
- 每次**定义** v 的指令后插入 `sw v_reg, spill_slot($sp)`
- 每次**使用** v 的指令前插入 `lw v_reg, spill_slot($sp)`
- 使用新的临时虚拟寄存器（活跃区间极短，通常不再溢出）

---

## 七、与现有架构的集成点

### 7.1 ValueLocation 桥梁

`ValueLocation`（`STACK` / `REGISTER`）已在后端预留。分配结果写入 `StackFrame` 后，`InstructionEmitter::LoadValueToReg` 的通用分支自动切换：

```cpp
// 当前（全栈分配）
ValueLocation loc = frame_.GetLocation(val);
if (loc.kind == ValueLocation::REGISTER) {
    writer_.EmitMove(reg, loc.reg_name);   // 寄存器分配后大量走这里
} else {
    writer_.EmitLwSp(reg, loc.stack_offset); // 只有溢出才走这里
}
```

这意味着 `InstructionEmitter` 的指令选择逻辑**几乎不需要改动**，寄存器分配的效果通过 `ValueLocation` 注入。

### 7.2 MipsInst 结构升级的触发时机

在 O7 开始时，**第一步**是完成 MipsInst 结构升级（§三），然后所有后续步骤（活跃分析、干涉图、着色）基于结构化指令操作，最后在序列化时输出字符串。O4 的窥孔规则相应改为字段匹配，代码更简洁且不易出错。

---

## 八、预估工作量与实施顺序

| 步骤 | 内容 | 预估行数 |
|------|------|---------|
| Step 1 | MipsInst 结构 + AsmWriter 升级 | ~150 行 |
| Step 2 | 活跃变量分析（块级 + 指令级） | ~150 行 |
| Step 3 | 干涉图构建 | ~100 行 |
| Step 4 | Simplify + Freeze + Spill（worklist 循环） | ~200 行 |
| Step 5 | Coalesce（George 准则） | ~100 行 |
| Step 6 | Select + 着色结果写入 ValueLocation | ~100 行 |
| Step 7 | 溢出代码插入 + Restart 循环 | ~150 行 |
| **合计** | | **~950 行** |

**推荐实施顺序**：Step 1 → Step 2 → Step 3 → Step 4+6（先不做 Coalesce）→ 验证 → Step 5+7 → 最终验证。

在 Step 4+6 完成后（无 Coalesce 的基础版）就可以量化一次收益；Coalesce（Step 5）能进一步消除大量 `move` 指令，是最后的增益来源。

---

## 九、验证策略

1. **首先在全量测试集上验证语义不变**：寄存器分配不应改变任何程序输出
2. **量化 lw/sw 减少量**：
   ```bash
   grep -c "lw\s\|sw\s" mips_before.txt
   grep -c "lw\s\|sw\s" mips_after.txt
   ```
3. **量化 move 减少量**（Coalesce 效果）：
   ```bash
   grep -c "move\s" mips_before.txt
   grep -c "move\s" mips_after.txt
   ```
4. **总指令条数对比**：`wc -l mips_before.txt mips_after.txt`
5. **回归边界用例**：溢出极多的用例（局部变量数 > 18 的函数）；递归函数（`$s`-寄存器的 callee-save 逻辑）

---

*本文档记录 O7 图着色寄存器分配的规划与设计方向。具体实施时另立实现文档，记录 Build/Simplify/Select 各步骤的代码设计细节与 before/after IR 对照。*

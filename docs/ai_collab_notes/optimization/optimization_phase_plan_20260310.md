# 优化阶段规划：方向选择与实施路线图

**日期**: 2026-03-10
**前置状态**: 全部功能阶段（词法→语法→语义→IR→Mem2Reg→MIPS）已完成并通过全部测试
**参考**: [课程优化教程](../../course_info/optimization_course_guide.md)
**目标**: 在保证正确性的前提下，选择 ROI 最高的优化项逐步实施

---

## 一、现有基础设施盘点

在决定做哪些优化之前，先盘点一下我们**已经拥有什么**——这直接决定了哪些优化可以低成本接入。

| 基础设施 | 位置 | 可复用于 |
|---------|------|---------|
| **完全 SSA 形式 IR** | Mem2Reg 输出 | 所有 IR 级优化的前提 |
| **CFG 构建器** | `pass/CFGBuilder` | DCE、活跃变量分析、循环检测 |
| **Cooper 支配树 + 支配边界** | `pass/DomTree` | GVN/GCM 的调度、循环分析 |
| **FunctionPass 基类** | `pass/Pass.h` | 新增 IR Pass 只需继承并实现 `Run()` |
| **def-use 链**（User/Use/Value） | `ir/Value.h`、`ir/User.h` | DCE 的有用性传播、常量传播 |
| **MipsOptions 优化开关** | `mips/MipsOptions.h` | 后端优化的开关控制 |
| **ValueLocation 抽象** | `mips/ValueLocation.h` | 寄存器分配的桥梁（STACK→REGISTER） |
| **AsmWriter 单一输出口** | `mips/AsmWriter.h` | 窥孔优化的缓冲层 |
| **ConstExpEvaluator** | `irgen/ConstExpEvaluator.h` | 常量折叠的参考逻辑 |

---

## 二、课程教程提及的优化项可行性评估

### 2.1 中端优化（IR 层）

| 优化项 | 复杂度 | 前置条件满足？ | 预期收益 | 推荐 |
|-------|--------|--------------|---------|------|
| **常量折叠 + 代数化简** | ⭐ 低 | ✅ SSA + ConstExpEvaluator 可参考 | 中（消除冗余算术） | ✅ 必做 |
| **死代码消除 (DCE)** | ⭐⭐ 低-中 | ✅ SSA + def-use 链 | 中（缩减代码量） | ✅ 必做 |
| **LVN（局部值编号）** | ⭐⭐ 中 | ✅ SSA 形式 | 中（消除块内公共子表达式） | ✅ 推荐 |
| **GVN + GCM** | ⭐⭐⭐⭐ 高 | ⚠️ 需要循环深度分析（新增） | 高（循环外提 + 全局 CSE） | ⚠️ 时间充裕再做 |
| **函数内联** | ⭐⭐⭐ 中-高 | ⚠️ 需要 IR 克隆机制（unique_ptr 模型下不简单） | 中 | ❌ 暂不推荐 |

### 2.2 后端优化（MIPS 层）

| 优化项 | 复杂度 | 前置条件满足？ | 预期收益 | 推荐 |
|-------|--------|--------------|---------|------|
| **乘除法强度削减** | ⭐ 低 | ✅ MipsOptions 开关已就位 | 中-高（循环中的乘法尤甚） | ✅ 必做 |
| **窥孔优化** | ⭐⭐ 低-中 | ✅ AsmWriter 单一出口 | 中（消除冗余 sw/lw） | ✅ 推荐 |
| **基本块合并 + 冗余跳转消除** | ⭐⭐ 中 | ✅ FunctionEmitter 中可接入 | 中（减少 j 指令） | ✅ 推荐 |
| **图着色寄存器分配** | ⭐⭐⭐⭐⭐ 极高 | ✅ ValueLocation 桥梁已搭好 | 极高（消除绝大部分 lw/sw） | ⚠️ 量力而行 |

---

## 三、推荐实施方案

基于 **ROI（投入产出比）排序** 与 **增量验证原则**，将优化项分为三个层级。

### 第一梯队：必做（低成本、高确定性、高学习价值）

#### O1. 常量折叠 + 代数化简（IR Pass）

**原理**：扫描每条指令，若所有操作数均为 `ConstantInt`，在编译时直接计算结果，用 `ConstantInt` 替换（RAUW）。同时识别代数恒等式（`a + 0 = a`、`a * 1 = a`、`a * 0 = 0` 等）并化简。

**接入方式**：新增 `pass::ConstFoldPass : public FunctionPass`，在 `main.cpp` 中 Mem2Reg 之后、MIPS 生成之前插入。

**预估工作量**：~100 行。

#### O2. 死代码消除 DCE（IR Pass）

**原理**：从"根本有用"的指令（`ReturnInst`、`StoreInst`、`CallInst`、`BranchInst`）出发，沿 def-use 链反向标记所有被依赖的指令。未被标记的指令即为死代码，删除之。

**接入方式**：新增 `pass::DCEPass : public FunctionPass`。

**注意**：`PhiInst` 的 incoming 不在标准 def-use 链中（见 Mem2Reg 设计文档 §5.2），需要额外遍历 phi 操作数。

**预估工作量**：~120 行。

**建议顺序**：先做常量折叠，再做 DCE。常量折叠会暴露出新的死代码（如 `%x = add 3, 5` 折叠为 `8` 后，原操作数的定义可能变成无用指令）。

#### O3. 乘除法强度削减（MIPS 层）

**原理**：
- 乘以 2 的幂 → `sll`（左移）
- 乘以小常数（如 3、5、7）→ 移位 + 加减组合
- 除以 2 的幂 → `sra`（算术右移），需处理负数修正
- 除以一般常数 → 乘以 magic number + 取 HI + 右移（论文 *Division by Invariant Integers using Multiplication*）

**接入方式**：在 `InstructionEmitter::EmitBinaryInst` 中，检查 `MipsOptions::enable_mul_div_opt`，对常数操作数走优化路径。

**预估工作量**：~80 行（乘法优化）；除法 magic number 算法约 ~120 行。

### 第二梯队：推荐（中等投入、显著收益）

#### O4. 窥孔优化（MIPS 层）

**原理**：在 AsmWriter 内部引入指令缓冲，输出前对缓冲区做模式匹配：
- `sw $t, X($sp)` 紧接 `lw $t, X($sp)` → 删除 lw
- `move $t, $t` → 删除
- `li $t, 0` + `addu $d, $s, $t` → `move $d, $s`
- 连续两次 `addiu $sp` → 合并

**接入方式**：AsmWriter 新增 `SetBufferMode(true)` + `Flush()`，在 Flush 前执行窥孔 pass。

**预估工作量**：~150 行。

#### O5. 基本块合并 + 冗余跳转消除（MIPS 层）

**原理**：
- 如果块 A 的唯一终止指令是 `j B` 且 B 紧随 A 排列 → 省去 `j` 指令
- 如果块 B 只有一个前驱 A 且 A 只有一个后继 B → 合并两块
- 条件分支优化：`bnez $t, L1; j L2` 且 L2 紧随 → 只需 `beqz $t, L2`（反转条件）

**接入方式**：FunctionEmitter 在 EmitBody 中分析块排列顺序。

**预估工作量**：~100 行。

#### O6. LVN（局部值编号）（IR Pass）

**原理**：在每个 BasicBlock 内，用哈希表记录 `(操作符, 操作数1, 操作数2) → Value*`。遇到相同的表达式直接 RAUW，消除块内公共子表达式。可与常量折叠合并在同一个 pass 中。

**接入方式**：扩展 O1 的 ConstFoldPass 为 ConstFoldLVNPass。

**预估工作量**：在 O1 基础上增加 ~80 行。

### 第三梯队：挑战（高投入、旗舰级优化）

#### O7. 图着色寄存器分配（MIPS 层）

**原理**：经典的 Chaitin-Briggs 算法——Build（活跃变量分析 → 冲突图）→ Simplify → Coalesce → Freeze → Spill → Select → Restart。

**前置准备**：
- 需要实现 MIPS 级别的**活跃变量分析**（数据流方程：`in[B] = use[B] ∪ (out[B] - def[B])`，`out[B] = ∪ in[S], S ∈ succ[B]`）
- 冲突图构建
- 溢出代码生成（spill → load/store 插入）

**ValueLocation 桥梁**：分配结果写入 `StackFrame` 的 `ValueLocation`，`LoadValueToReg` 的通用分支自动切换 `lw` → `move`。

**预估工作量**：~500-800 行。这是最复杂的单项优化，但也是对编译原理理解最深的实践。

**建议**：如果时间有限，可以先做一个简化版——**线性扫描寄存器分配（Linear Scan）**，复杂度远低于图着色，但效果也相当可观。

---

## 四、推荐实施顺序

```
O1 常量折叠    →  O2 DCE    →  O3 乘除优化  →  O4 窥孔  →  O5 块合并
      ↑                                                        ↓
   IR 层优化                                              MIPS 层优化
                                                               ↓
                         O6 LVN  ←───  时间允许  ───→  O7 寄存器分配
```

**每步验证策略**：
1. 每个 Pass 实现后，先 `diff llvm_ir.txt`（IR Pass）或 `diff mips.txt`（MIPS Pass）确认变化合理
2. 用 MARS 4.5 运行全部测试用例确认输出正确
3. 对比优化前后的 MIPS 指令条数（`wc -l mips.txt`）量化收益

**开关控制**：每个优化都可在 `main.cpp` 中通过 `const bool kEnableXxx = true/false;` 独立开关，便于回归对比。

---

## 五、不推荐做的优化及原因

| 优化项 | 不推荐原因 |
|-------|-----------|
| **完整 GVN + GCM** | 需要循环深度分析（新增基础设施）、指令调度算法复杂、容易引入正确性 bug；LVN 能覆盖大部分 GVN 收益 |
| **函数内联** | 我们的 IR 采用 `unique_ptr` 所有权模型，克隆 Function/BasicBlock/Instruction 需要深拷贝整棵子树，工程量大且收益不确定 |
| **完整的 Phi 消除 + 关键边拆分** | 我们的 MIPS 后端已经用栈上拓扑排序解决了 Phi 下降，重走"消 Phi → PC → move 序列"的经典路线反而增加复杂度 |
| **指令调度 / 流水线优化** | MARS 是模拟器，不模拟真实 CPU 流水线，指令调度无收益 |

---

## 六、与现有架构的集成点

### 6.1 IR Pass 接入模式

```cpp
// main.cpp（示意）
if (kEnableMem2Reg)   { /* ... Mem2Reg ... */ }
if (kEnableConstFold) { pass::ConstFoldPass cf; for_each_func(cf); }
if (kEnableDCE)       { pass::DCEPass dce;     for_each_func(dce); }
// 可以多轮迭代：常量折叠 → DCE → 常量折叠 → DCE，直到不再变化
```

### 6.2 MIPS 优化接入模式

```cpp
mips::MipsOptions mips_opts;
mips_opts.enable_mul_div_opt = kEnableMulDivOpt;
mips_opts.enable_peephole    = kEnablePeephole;
mips_opts.enable_reg_alloc   = kEnableRegAlloc;
mips::MipsEmitter emitter(mips_out, *result.module, mips_opts);
emitter.Emit();
```

后端优化全部通过 `MipsOptions` 控制，与初版行为 100% 兼容（所有开关默认 false）。

---

*本文档作为优化阶段的方向规划与实施参考。具体每个优化项的设计细节将在实施时另立文档记录。*

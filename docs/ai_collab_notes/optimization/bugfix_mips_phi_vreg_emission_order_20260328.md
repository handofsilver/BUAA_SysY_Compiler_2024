# Bug 修复：O7 虚拟寄存器路径下 Phi 发射顺序与错误栈加载（case_012 等）

**日期**: 2026-03-28
**严重程度**: 语义错误（MIPS 运行结果错误，LLVM IR 仍正确）
**典型复现**: `SysY_Test_2024` 的 `case_012`（`complex_logic` + 多层循环/逻辑），期望 `Result: 6`，实际 `Result: 0`
**修复涉及文件**:

- `include/mips/InstructionEmitter.h` — 声明 `ReservePhiVRegsForFunction()`
- `src/mips/InstructionEmitter.cpp` — 预分配 phi 的 vreg；`Emit()` 内对 `PhiInst` 调用 `DefValue`（幂等）
- `src/mips/FunctionEmitter.cpp` — 在 `EmitBody()` 之前调用预分配

---

## 一、现象

- **`llvm_ir.txt`**：Mem2Reg 后的 SSA 与 phi 布局正确，用 `lli` 或课程工具验证**可得到正确结果**（如 `Result: 6`）。
- **`mips.txt`**：在开启 **`enable_reg_alloc` + vreg 发射** 时，同一源程序在 MARS 上运行得到 **`Result: 0`** 等错误输出。

对 `complex_logic` 生成的 MIPS 中可观察到异常模式，例如在外层 `for` 的步进块末尾：

```mips
complex_logic_for_step_9:
    ...
    lw    $t0, 96($sp)    # 或类似正偏移的 lw
    move  $t2, $t0        # 将“和”寄存器覆盖为从栈读出的值
    j     complex_logic_for_cond_7
```

同时，`if.then` 路径上已将**新的累加和**算在 **`$t0`** 中，但跳转到 `for_step` 前**没有**把该值写回 `sum` 对应的虚拟寄存器/物理寄存器，而是依赖上述 **`lw`**。若栈槽**从未在 vreg 路径下被 `sw` 更新**，读出的往往是 **0**，导致整个循环的 `sum` 被反复清零。

---

## 二、根本原因

### 2.1 SSA 与“按块线性发射”的错位

Mem2Reg 之后，循环头结点上的变量由 **phi** 表示，例如（与 `llvm_ir.txt` 中 `complex_logic` 一致的结构）：

- `%4 = phi i32 [ 0, %entry ], [ %21, %for.step.3 ]` —— 外层累加和。
- `%21 = phi i32 [ %20, %if.then.5 ], [ %38, %if.next.15 ]` —— 定义在 **`if.next.6`**，供 **`for.step.3` → `for.cond.1`** 的边作为 `%4` 的 incoming 使用。

**控制流上**：必须先执行 **`if.next.6`**（完成 `%21` 的 phi），再进入 **`for.step.3`**，最后回到 **`for.cond.1`**。

**发射顺序上**：`FunctionEmitter::EmitBody()` 按 **`ir::Function::GetBlocks()` 的向量顺序**依次发射基本块。该顺序**不一定**与 CFG 的拓扑序一致；常见情况是 **`for.step.3` 在 IR 列表里排在 `if.next.6` 之前**。

因此会出现：

1. 先发射 **`for.step.3`** 末尾的 **`br label %for.cond.1`**；
2. 在 **`EmitPhiMovesForEdge(pred=for.step, branch)`** 里，要为 **`for.cond.1`** 上的 phi 插入 **`move dst_vreg, UseValue(incoming)`**；
3. 其中一条 incoming 是 **`%21`**，但 **`%21` 所在块尚未被扫描**，`InstructionEmitter::value_to_vreg_` 里**还没有** `%21` 的条目。

### 2.2 `UseValue` 的错误后备路径

`UseValue` 的逻辑（简化）：

1. 常量、`GlobalVar`、`Alloca` 等单独处理；
2. 若在 `value_to_vreg_` 中找到 **`val`**，直接返回对应 **`$vrN`**；
3. 否则：断言有栈槽，**分配新 vreg 并 `EmitLwSp`**，从 **`StackFrame`** 为 SSA 值预留的槽位加载。

全栈 lowering 时代，每个 SSA 结果会配合 **`sw`** 写回槽位，因此 **`lw` 后备**语义正确。
在 **vreg 为主路径** 下，**正常 def 不再写栈**，该槽位**不会**被更新；此时 **`lw` 读到的是垃圾或 0** —— 这正是 **`Result: 0`** 的来源。

调试阶段曾用 NDJSON 日志证实：fallback 触发时 **`kind` 为 `PhiInst`**，且出现在 **`complex_logic`** 等多 phi 函数中（日志已移除，不再写入仓库）。

### 2.3 为何“仅在块首对 phi 调用 `DefValue`”仍不够

若只在 **`Emit()`** 遍历到某块的第一条指令时对 **`PhiInst`** 调用 **`DefValue(phi)`**：

- 对**当前块**内后续 **`UseValue(该 phi)`** 有效；
- 但对**更早发射**的块里、在 **`value_to_vreg_` 仍无 `%21`** 时发生的 **`UseValue(%21)`** 仍无效 —— **定义块更晚**时问题依旧。

因此需要**在任意 MIPS 指令发射之前**，保证**全函数的每个 phi**都已在映射表里占有 vreg。

---

## 三、修复方式

### 3.1 全函数 phi vreg 预分配：`ReservePhiVRegsForFunction`

在 **`FunctionEmitter::Emit()`** 中，**`EmitPrologue()` 之后、`EmitBody()` 之前**（且仅当 **`options_.enable_reg_alloc`**）调用：

```cpp
inst_emitter_.ReservePhiVRegsForFunction();
```

实现要点（`InstructionEmitter.cpp`）：

- 遍历 **`func_.GetBlocks()`** 中每一个基本块；
- 从块首开始，**连续**扫描 **`PhiInst`**（Mem2Reg 约定：phi 集中在块首），对每个 phi 调用 **`DefValue(phi)`**；
- **`DefValue`** 仅为 phi **分配 ` $vr* ` 并登记 `value_to_vreg_` / spill 槽元数据**，**不发射任何 MIPS 指令**。

此后，任意前驱块在 **`EmitPhiMovesForEdge`** 里 **`UseValue(phi_result)`** 都能命中映射，走 **vreg → vreg 的 `move`**，而不会再误走 **`EmitLwSp`**。

### 3.2 `Emit()` 内对 `PhiInst` 的分支（保留）

在 **`enable_reg_alloc`** 分支最前增加：

```cpp
if (auto* phi = dynamic_cast<const ir::PhiInst*>(inst)) {
    (void)DefValue(phi);
    return;
}
```

与预分配**幂等**：第二次 **`DefValue`** 直接返回已有 vreg，避免 phi 在 **`if-else` 链**里被静默跳过导致遗漏。

### 3.3 与其它 RegAlloc 修复的关系（简述）

以下问题与**本次 phi 顺序 bug 不同**，但曾在同一阶段排查：

- **`RewriteInstruction`** 对 **`BNEZ`/`BEQZ`** 的 **`src1`** 必须做 vreg→物理寄存器重写，否则条件仍带 **`$vr*`**。
- **`AdjustStackFrameForCalleeSaved`** 对 **`addiu $reg, $sp, imm`（alloca 基址）** 的 **`imm`** 需随 callee-saved 区域扩大而 bump，与仅处理 **`addiu $sp,$sp,±F`** 的序言/尾声不同。

本文档**不展开**其细节，避免与 phi 问题混淆。

---

## 四、代码导读（阅读顺序）

```
main.cpp
  └─ MipsEmitter / FunctionEmitter::Emit()
       ├─ EmitPrologue()          … 含 EmitIncomingArguments()（vreg 路径）
       ├─ ReservePhiVRegsForFunction()  … ★ 全函数 phi → vreg 预登记（仅 O7）
       └─ EmitBody()
            └─ 每块：EmitLabel + 对每条 IR InstructionEmitter::Emit()
                 ├─ PhiInst → DefValue(phi); return;
                 ├─ BranchInst → EmitPhiMovesForEdge → UseValue(incoming) …
                 └─ …

InstructionEmitter::UseValue()
  └─ 未命中 value_to_vreg_ 且 HasSlot → EmitLwSp（vreg 路径下易错，依赖预分配避免对 phi/已 def SSA 误用）
```

---

## 五、验证建议

1. **`case_012`**：`Result: 6`，与 IR 一致。
2. 检查 **`mips.txt`** 中 **`complex_logic_for_step_9`**：对 **`sum` 相关 phi 的合并**应为 **`move`** 链，**不应**依赖**未在 vreg 路径写回的栈槽 `lw`** 来恢复累加和。
3. 全量 **`SysY_Test`** 或课程测试集回归。

---

## 六、小结

| 项目 | 说明 |
|------|------|
| **触发条件** | `enable_reg_alloc` + 按块线性发射 + phi 定义块在 IR 列表中晚于使用该 phi 作为 phi incoming 的前驱边发射点 |
| **直接机制** | `UseValue` 无映射时 `EmitLwSp`，栈槽在 vreg 路径下未维护 → 读到 0 |
| **修复核心** | 发射函数体前 **`ReservePhiVRegsForFunction()`**，为所有 phi 预先 `DefValue`，保证 `EmitPhiMovesForEdge` 中 `UseValue` 恒可走 vreg |
| **辅助** | `Emit()` 显式处理 `PhiInst`，与预分配幂等配合 |

以上即为本次 bug 的**原理**与**工程化修复**的完整说明。

# Bug Fix: Mem2Reg 生成缺少 incoming 的非法 phi 节点

**Date**: 2026-03-04
**File**: `src/pass/Mem2Reg.cpp`（`RenameBlock` → fill-incoming 段）
**症状**: 生成的 IR 中出现只有 1 个 incoming 的 phi，例如：
```llvm
%2 = phi i32 [ %18, %for.step.3 ]   ; 错误：for.cond.1 有 2 个前驱，phi 只有 1 个 incoming
```

---

## 根因分析

**场景**：`arr[i] != 0 && arr[i] < 10` 的短路逻辑产生一个 `sc_alloca`（alloca i32，用于存储 `&&` 的布尔结果）。

**DF 传播**：
`sc_alloca` 的 `def_blocks = {and.then.7, and.false.8}`，Mem2Reg 的迭代 phi 插入依次触发：
- Round 1：在 `and.merge.9` 插入 phi（`DF[and.then.7] = {and.merge.9}`）
- Round 2：`and.merge.9` 成为新的 def，`DF[and.merge.9] = {for.cond.1}` → **在 `for.cond.1` 也插入 phi**

这步在数学上完全正确：由于存在 `and.merge.9 → if.next.6 → for.step.3 → for.cond.1` 的回路，`for.cond.1` 确实在 `and.merge.9` 的支配边界内。

**重命名时的 bug**：
`for.cond.1` 有两个前驱：`entry` 和 `for.step.3`。
- 处理 `for.step.3` 时，`sc_alloca` 已经有值（`%18`），`AddIncoming(%18, for.step.3)` ✓
- 处理 `entry` 时，`sc_alloca` 尚未被 store 过，`current_val[sc_alloca]` 为空：

```cpp
// 修复前（有 bug）：
ir::Value* cur = stack.empty() ? nullptr : stack.back();
if (cur) {
    phi->AddIncoming(cur, bb);  // ← 栈为空时直接跳过，导致 entry 这条 incoming 缺失！
}
```

结果：phi 只收到 1 个 incoming，`lli` 拒绝执行（phi 的 incoming 数必须等于所在块的前驱数）。

---

## 修复方案

移除 `if (cur)` 守卫，**始终添加 incoming**，当栈为空时传入 `nullptr`（`PhiInst::Print` 已将 `nullptr` 打印为 `undef`）：

```cpp
// 修复后：
ir::Value* cur = stack.empty() ? nullptr : stack.back();
phi->AddIncoming(cur, bb);  // nullptr → printed as "undef"，产生合法 IR
```

修复后生成：
```llvm
%2 = phi i32 [ undef, %entry ], [ %18, %for.step.3 ]
```

这是合法的 LLVM IR。`%2` 在此程序中从未被使用（dead phi），不影响语义正确性；`lli` 可以正常执行。

---

## 设计启示

phi 节点的 "缺少 incoming" 问题是 Mem2Reg 中极常见的陷阱，根本原因是：
**DF 算法会在"变量从未在某条前驱路径上被定义"的位置合法地插入 phi**——这种 phi 的某些 incoming 在语义上是未定义的（undef），必须显式填入，而不能跳过。

真实 LLVM 的 `mem2reg` pass 用 `llvm::UndefValue::get(type)` 处理此情形。本项目中 `PhiInst::Print` 已用 `nullptr` 代表 undef，保持一致即可。

# PhiInst 操作数模型升级：让 Phi 进入 def-use 链

**日期**: 2026-03-27
**前置状态**: 全部功能阶段已完成，进入优化阶段
**关联文档**: [Mem2Reg 设计文档 §5](../../design_documents/mem2reg.md)、[优化阶段规划](optimization_phase_plan_20260310.md)

---

## 一、背景：什么是 def-use 链，为什么重要

### 1.1 IR 的三层结构

本项目的 IR 继承体系模仿 LLVM：

```
Value（所有 IR 实体的基类）
  ├── User（使用其他 Value 的实体，持有 operands_）
  │     └── Instruction（具体指令：BinaryInst、CallInst、PhiInst ...）
  ├── Constant（常量）
  ├── BasicBlock（基本块）
  └── Argument（函数参数）
```

其中的关键机制是 **def-use 链**——"谁定义了值，谁使用了值"的双向索引。

### 1.2 def-use 链的运作方式

三个角色：

| 类 | 文件 | 角色 |
|---|---|---|
| **`Value`** | `include/ir/Value.h` | 每个 Value 维护一个 `use_list_`（`list<Use*>`），记录"谁在用我" |
| **`Use`** | `include/ir/Use.h` | 一条"使用边"：连接一个 User（使用者）和一个 Value（被使用者），存储在 User 的 `operands_` 向量中 |
| **`User`** | `include/ir/User.h` | 持有 `operands_`（`vector<Use>`），表示"我使用了哪些 Value" |

完整的注册流程如下。以 `%r = add i32 %a, %b` 为例：

```
BinaryInst 构造时：
  1. ResizeOperands(2)       → operands_ 变为 [Use₀, Use₁]，每个 Use 记录 user_=this
  2. SetOperand(0, %a)       → Use₀.value_ = %a;  %a 的 use_list_ 添加 &Use₀
  3. SetOperand(1, %b)       → Use₁.value_ = %b;  %b 的 use_list_ 添加 &Use₁
```

于是形成了双向索引：
- **def→use 方向**：`%a->GetUseList()` 可以找到所有使用 `%a` 的指令
- **use→def 方向**：`addInst->GetOperand(0)` 可以得到 `%a`

### 1.3 ReplaceAllUsesWith (RAUW)

`Value::ReplaceAllUsesWith(Value* new_val)` 是 IR 优化的核心操作。它遍历 `this->use_list_` 中的每条 Use 边，把 `value_` 从 this 改为 new_val，并更新两边的 use_list_：

```cpp
// Value.cpp
void Value::ReplaceAllUsesWith(Value* new_val) {
    if (!new_val) return;
    auto use_list_copy = use_list_;           // 拷贝一份，因为循环中会修改 use_list_
    for (Use* use : use_list_copy) {
        RemoveUse(use);                       // 从 this 的 use_list_ 移除
        use->SetValue(new_val);               // 改指向
        new_val->AddUse(use);                 // 加入 new_val 的 use_list_
    }
}
```

**这是所有 IR 优化的基石**：常量折叠把 `%r = add 3, 5` 替换为常量 8，就是对 `%r` 调用 `RAUW(const_8)`，所有使用 `%r` 的地方自动改为使用 `const_8`。

---

## 二、问题：PhiInst 的 incoming values 不在 def-use 链中

### 2.1 现状

当前 `PhiInst` 的 incoming values 存储在一个**独立**的 `vector<IncomingPair>` 中，没有注册到 `User::operands_`：

```cpp
// include/ir/Instruction.h（PhiInst 类）
class PhiInst : public Instruction {
private:
    struct IncomingPair {
        Value* val;          // ← 裸指针，没有对应的 Use 对象
        BasicBlock* pred;
    };
    std::vector<IncomingPair> incoming_;   // ← 独立存储
public:
    void AddIncoming(Value* val, BasicBlock* pred) {
        incoming_.push_back({val, pred});  // ← 只是 push_back，不通知 val 的 use_list_
    }
    Value* GetIncomingValue(int i) const {
        return incoming_[i].val;           // ← 直接从 incoming_ 读取
    }
};
```

对比其他指令（以 BinaryInst 为例），它们的操作数都通过 `SetOperand` 注册到 def-use 链：

```cpp
// src/ir/Instruction.cpp（BinaryInst 构造）
BinaryInst::BinaryInst(..., Value* lhs, Value* rhs) : ... {
    ResizeOperands(2);
    SetOperand(0, lhs);    // ← lhs 的 use_list_ 会记录这条使用
    SetOperand(1, rhs);    // ← rhs 的 use_list_ 会记录这条使用
}
```

### 2.2 为什么当时这样设计

原因在 `mem2reg.md` §5.1 中有记录：

> φ 节点的操作数（incoming 对）需要动态增长（逐个 `AddIncoming`）。若使用 `User::operands_`（`vector<Use>`），vector 扩容时会重分配内存，使已注册进 value `use_list_` 的 `Use*` 指针**悬空**，造成内存安全漏洞。

具体来说，假设 PhiInst 的 `AddIncoming` 改为往 `operands_` push_back：

```
状态：operands_ = [Use₀]，Use₀ 的地址 = 0x1000
       %a 的 use_list_ 中记录了 0x1000（指向 Use₀）

调用 AddIncoming(%b, bb2)：
  operands_.push_back(Use₁)
  → vector 内存不够，重分配！
  → Use₀ 被 move 到新地址 0x2000
  → 但 %a 的 use_list_ 中仍然记录 0x1000（旧地址）
  → 悬垂指针！后续通过 use_list_ 访问 Use₀ 是未定义行为
```

这就是 `vector<Use>` + `push_back` + use_list 三者组合的经典问题。其他指令不会遇到这个问题，因为它们在构造时就调用 `ResizeOperands(N)` 确定了 operands_ 的大小，此后不再增长。

### 2.3 这在之前为什么不是问题

Mem2Reg 是当前唯一创建 PhiInst 的地方，而 Mem2Reg 的两个操作都不依赖 def-use 链：

1. **写入 Phi**：`phi->AddIncoming(cur, bb)`——直接写入 incoming_，不需要 def-use。
2. **MIPS 后端读取 Phi**：通过 `phi->GetIncomingValue(i)` 直接读取 incoming_，也不需要 def-use。

所以在"IR 生成 → Mem2Reg → MIPS 后端"的流水线中，PhiInst 脱离 def-use 链是**无害**的。

### 2.4 为什么优化阶段必须修复

进入优化阶段后，IR Pass 会大量使用 RAUW。例如常量折叠：

```
原始 IR：
  %x = add i32 3, 5
  %y = phi i32 [ %x, %bb1 ], [ 0, %bb2 ]

常量折叠：发现 %x = add 3, 5 → 编译时直接算出 8
  → %x.ReplaceAllUsesWith(const_8)
  → 所有通过 operands_ 使用 %x 的指令自动更新为使用 const_8
  → 但 PhiInst 的 incoming_[0].val 仍然指向 %x（裸指针，不在 use_list_ 中）
  → 接下来 %x 被删除（EraseFromParent）
  → Phi 的 incoming_[0].val 变成悬垂指针 → 崩溃或静默错误
```

类似地，DCE（死代码消除）需要通过 `val->GetUseList()` 判断一个值是否还有使用者。如果 Phi 的引用不在 use_list_ 中，DCE 会误判"没人用这个值"而错误删除。

**结论**：任何需要 RAUW 或遍历 use_list_ 的 IR Pass 都会因为 PhiInst 的特殊建模而出错。这是进入优化阶段的第一个阻塞项。

---

## 三、解决方案

### 3.1 设计目标

1. PhiInst 的 incoming values 进入 `User::operands_` 体系，RAUW 自动覆盖
2. `GetIncomingValue(i)` / `GetIncomingBlock(i)` 的外部接口不变，调用方无感
3. 不引入 vector 重分配导致的悬垂指针风险
4. Mem2Reg 的 DFS 逻辑（逐个 `AddIncoming`）改动最小
5. MIPS 后端完全不需要改

### 3.2 方案：Finalize 两阶段模式

核心思想：**AddIncoming 阶段先攒数据，Finalize 阶段一次性注册到 operands_**。

#### 阶段一：AddIncoming（构建期，与现在相同）

`AddIncoming(val, pred)` 仍然只做 `incoming_.push_back({val, pred})`，不碰 `operands_`。这保证了构建期间可以任意次调用 `AddIncoming` 而不触发 vector 重分配问题。

#### 阶段二：FinalizeOperands（构建完成后调用一次）

新增方法 `PhiInst::FinalizeOperands()`，在所有 incoming 都添加完之后调用一次：

```cpp
void PhiInst::FinalizeOperands() {
    size_t n = incoming_.size();
    ResizeOperands(n);                              // 一次性分配，之后不再增长
    for (size_t i = 0; i < n; ++i) {
        SetOperand(static_cast<int>(i), incoming_[i].val);  // 注册到 def-use 链
    }
}
```

调用时机：在 Mem2Reg 的 `Run()` 方法末尾，DFS 重命名完成后、删除 dead loads/stores 之前，遍历函数中所有 PhiInst 调用 `FinalizeOperands()`。

#### 读取接口统一到 operands_

`GetIncomingValue(i)` 改为从 `operands_` 读取（Finalize 之后这是 source of truth）：

```cpp
Value* PhiInst::GetIncomingValue(int i) const {
    return GetOperand(i);       // 现在走 operands_，受 RAUW 管理
}
```

`GetIncomingBlock(i)` 仍然从 `incoming_` 读取（BasicBlock 映射关系不需要进 def-use 链）：

```cpp
BasicBlock* PhiInst::GetIncomingBlock(int i) const {
    return incoming_[i].pred;   // 不变
}
```

`incoming_` 的 `val` 字段在 Finalize 之后不再被读取，仅 `pred` 字段仍然有用。

### 3.3 为什么这个方案是安全的

| 风险点 | 分析 |
|-------|------|
| **vector 重分配** | `ResizeOperands(n)` 只在 `FinalizeOperands()` 中调用一次，此后 `operands_` 不再增长。`SetOperand` 不改变 vector 大小，所以注册到 use_list_ 的 `Use*` 地址永远稳定。 |
| **Finalize 前的 RAUW** | Finalize 发生在 Mem2Reg 的 `Run()` 内部。在 Mem2Reg 完成之前不会有其他 Pass 对 IR 做 RAUW，所以 Finalize 前 incoming_.val 不会被外部改动。 |
| **Finalize 后 incoming_.val 过时** | Finalize 后 `GetIncomingValue(i)` 改为读 `GetOperand(i)`，不再读 `incoming_.val`。RAUW 只更新 `operands_`，而读取也只从 `operands_` 来，所以一致性有保障。 |

### 3.4 涉及的改动清单

| 文件 | 改动 |
|------|------|
| `include/ir/Instruction.h` | PhiInst 类：新增 `void FinalizeOperands()` 声明 |
| `src/ir/Instruction.cpp` | `FinalizeOperands()` 实现；`GetIncomingValue` 改为返回 `GetOperand(i)`；`AddIncoming` 不变；`Print` 中读 value 改为走 `GetOperand` |
| `src/pass/Mem2Reg.cpp` | `Run()` 末尾（Step 7 之前）：遍历函数所有 PhiInst 调用 `FinalizeOperands()` |

不需要改动的文件：
- `include/ir/Value.h`、`include/ir/User.h`、`include/ir/Use.h`：def-use 基础设施不变
- `src/mips/InstructionEmitter.cpp`：MIPS 后端通过 `GetIncomingValue(i)` 和 `GetIncomingBlock(i)` 访问，接口签名不变，行为自动跟随
- `src/mips/StackFrame.cpp`：不涉及 Phi incoming 的读取

### 3.5 验证策略

1. 编译通过，无 warning
2. 用全部测试用例验证 `mips.txt` 输出与升级前完全一致（功能不变性）
3. 在 `FinalizeOperands()` 后手动检查：任意 PhiInst 的 `GetNumOperands()` == `GetNumIncoming()`
4. 写一个小验证：对某个 Phi 的 incoming value 调用 `RAUW(some_const)`，确认 `GetIncomingValue(i)` 返回的是 `some_const`

---

## 四、后续展望

PhiInst operand 模型升级完成后，IR 的 def-use 链就是**完整**的——所有指令的所有操作数都在 `operands_` 中，RAUW 可以无差别覆盖全部引用。这为后续的优化 Pass 扫清了障碍：

- **常量折叠**：RAUW 替换常量表达式结果，Phi 中的引用自动更新
- **DCE**：通过 `val->GetUseList().empty()` 判断值是否无人使用，Phi 的引用被正确计入
- **LVN**：公共子表达式消除的 RAUW 同样覆盖 Phi

---

*本文档记录 PhiInst operand 模型升级的设计背景与方案。实施后更新 `mem2reg.md` §5 的状态说明。*

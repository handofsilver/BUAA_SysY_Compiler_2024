# Bug Fix：DCE 两阶段删除（use-list 悬垂指针 SEGV）

**日期**: 2026-03-28  
**严重程度**: Crash（Segmentation Fault）  
**复现路径**: 含有复杂控制流（for + if/else + continue/break）的 SysY 源文件  
**修复文件**: `src/pass/DCE.cpp`  
**关联文档**: [DCE 设计文档](dce_20260327.md)

---

## 一、现象

编译以下测试程序后立即崩溃（exit code 139）：

```c
int main() {
    int sum = 0;
    int i;
    int limit = 20;
    for (i = 0; i < 50; i = i + 1) {
        if (i == 10) { continue; }
        if (i > limit) { break; }
        if (i % 2 == 0 && sum < 100 || !limit) {
            sum = sum + i;
        } else {
            sum = sum - 1;
        }
    }
    return sum;
}
```

gdb 回溯：

```
#0  std::__detail::_List_node_base::_M_transfer(...)   # libstdc++
#1  std::list::splice(...)
#2  std::list::remove(...)
#3  ir::Value::RemoveUse(this=0x..., use=0x...)        # use-list 操作
#4  ir::User::SetOperand(this=0x..., i=0, val=0x0)
#5  pass::EraseFromParent(inst=0x...)
#6  pass::DCEPass::Run(...)                             ← 崩溃源头
#7  main()
```

---

## 二、根本原因

### 2.1 问题模型

DCE 的清扫阶段（Step 5）遍历 `to_erase` 列表，对每一条死指令调用原 `EraseFromParent(inst)`：

```cpp
// 原始实现（有 bug）
static void EraseFromParent(ir::Instruction* inst) {
    for (int i = 0; ...; ++i) {
        inst->SetOperand(i, nullptr);  // ← 步骤 A：清除操作数引用
    }
    // ← 步骤 B：从基本块中移除（unique_ptr 析构，对象释放）
    bb->GetInstructions().erase(it);
}

for (ir::Instruction* inst : to_erase) {
    EraseFromParent(inst);  // ← 对每条死指令依次执行 A+B
}
```

`to_erase` 的顺序是"按基本块内指令出现顺序"。当死代码子图中**定义者 D 排在使用者 E 之前**时，顺序如下：

```
1. 处理 D：先清除 D 的操作数（A）→ 再 erase D（B，D 的 unique_ptr 析构，D 所在内存释放）
2. 处理 E：调用 E->SetOperand(i, nullptr)（A）
              → 内部：old_val->RemoveUse(&use)
              → 此处 old_val == D，但 D 已经被析构！
              → 访问释放内存 → SEGV / UB
```

### 2.2 为什么在这个用例出现

对于简单的直线型代码，DCE 大多数情况下不会产生"死指令引用另一条死指令"的情形。但 for + if/else + continue/break 会产生大量控制流块，经 ConstFoldLVN 折叠后暴露出**多层级的死代码子图**：

```llvm
; 折叠后（伪示例）：%cond 所有使用者都死了，%cond 自身也是死指令
%cond  = icmp eq i32 %v, 0    ← 死定义 D
%branch_use = ... %cond ...    ← 死使用者 E，且 D 在 E 之前出现
```

此时 `to_erase` 中 D 排在 E 前，触发 bug。

---

## 三、修复方案

### 3.1 原则

**解挂操作数（detach）** 和**析构对象（destroy）** 必须完全分离成两个独立的全局遍历，而不是在同一个 `for` 循环里对每条指令各做一次。

原因：`SetOperand(i, nullptr)` 会回调**被引用值**的 `RemoveUse`，因此要求被引用值此时仍然存活。只要在所有 `erase`（析构）之前完成所有 `SetOperand(nullptr)`，就保证了这一点，与 `to_erase` 的顺序无关。

### 3.2 代码变更

将原来的一个 helper `EraseFromParent` 拆为两个职责分离的函数，并在 `Run()` 里用两个独立的 `for` 循环代替原来的单次遍历：

```cpp
// 阶段 5a：仅清除操作数引用（所有被引用指令此时仍存活）
static void DetachOperands(ir::Instruction* inst) {
    for (int i = 0; i < static_cast<int>(inst->GetNumOperands()); ++i) {
        inst->SetOperand(i, nullptr);
    }
}

// 阶段 5b：从基本块中移除 unique_ptr（才真正析构对象）
static void EraseInstFromBlockList(ir::Instruction* inst) {
    ir::BasicBlock* bb = inst->GetParent();
    assert(bb && "Instruction has no parent block");
    auto& insts = bb->GetInstructions();
    auto it = std::find_if(insts.begin(), insts.end(),
                           [inst](const auto& up) { return up.get() == inst; });
    assert(it != insts.end() && "Instruction not found in its parent block");
    insts.erase(it);
}

// Step 5（修复后）
for (ir::Instruction* inst : to_erase) {
    DetachOperands(inst);           // 5a：全部解挂，无一析构
}
for (ir::Instruction* inst : to_erase) {
    EraseInstFromBlockList(inst);   // 5b：全部析构，use-list 已净
}
```

### 3.3 正确性论证

- **5a 全部完成后**：`to_erase` 中所有指令的操作数已全部置空，它们的 `use_list_` 中也不再有来自任何死指令的 `Use*`。
- **5b 开始时**：任何一条死指令被 `erase` 析构，都不会再通过 `use_list_` 被别人引用（5a 已清理），也不需要自己再调用 `RemoveUse`（操作数已为空）。
- `to_erase` 的**顺序不再重要**：5a 和 5b 各自内部的顺序均无约束。

---

## 四、经验教训

### 为什么原设计有这个潜在 bug

[DCE 设计文档](dce_20260327.md) §4 写道：

> **必须先 A 后 B**（先 `SetOperand(nullptr)` 再 `erase`）

这个约束是正确的，但它只描述了**对单条指令**的操作顺序。设计文档没有考虑到：当多条死指令**互相引用**时，对每条指令的 A+B 单次循环并不等价于"所有 A 都先于所有 B"。

换句话说：

```
❌ 原语义："每条死指令的 A 先于它自己的 B"
✅ 正确语义："所有死指令的 A 都先于任何死指令的 B"
```

在没有死指令互相引用的情形（简单程序）下，两者等价；一旦死代码子图出现跨指令引用，原写法就会崩溃。

### 触发条件总结

| 条件 | 说明 |
|------|------|
| 死代码子图存在跨指令引用 | 即死指令 E 引用另一条死指令 D 的结果 |
| D 在 `to_erase` 中排在 E 之前 | `to_erase` 按块内顺序，D 定义先于 E 使用 |
| ConstFoldLVN 已经运行 | 折叠后大量中间值的使用者变成死代码，暴露此场景 |

---

## 五、验证

修复后，使用上述 `testfile.txt` 编译通过，正常生成 `llvm_ir.txt` 和 `mips.txt`，无崩溃（exit code 0）。

全量测试集（`SysY_Test_2024/`）未受影响——两阶段删除对正常程序与原行为完全等价。

---

*本文档记录 DCE 两阶段删除 Bug 的定位与修复。后续若升级为「支持不可达基本块消除的 DCE」，需注意从 CFG 中移除整个 BasicBlock 时同样应遵循此两阶段原则。*

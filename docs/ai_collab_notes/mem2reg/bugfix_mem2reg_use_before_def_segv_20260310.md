# Bug Fix: Mem2Reg 下 use-before-def 导致 SEGV（短路求值等）

**Date**: 2026-03-10
**Files**: `src/pass/Mem2Reg.cpp`（`RenameBlock` 中 Load 处理）、`include/pass/Mem2Reg.h`、`src/main.cpp`
**症状**: 开启 Mem2Reg 后，对含有**短路求值**（`&&` / `||`）或其它在部分路径上“先读后写”的样例运行编译器时，出现 **segmentation fault**。有时同一样例有时不崩、有时崩，表现不稳定（见下文“为何像玄学”）。

---

## 1. 触发情形与样例

### 1.1 最小复现

例如 `testfile.txt`：

```c
int main(){
    int a, b, c;
    if (a == 1 || b == 2) {
        c = 3;
    } else {
        c = 2;
    }
    return 0;
}
```

- `a == 1 || b == 2` 会生成短路控制流：一条路径上可能只对 `a` 求值就跳到 then，另一条路径才求值 `b`。
- IR 里会有**多个基本块**，某些变量（如短路结果或中间量）在**部分路径上只有 load、没有 store**（即该 alloca 的“当前值”在这条路径上从未被定义）。

### 1.2 为何会“先读后写”

- 对**可提升的 alloca**，Mem2Reg 按支配树做 SSA 重命名，用 `current_val[alloca]` 的栈表示“当前定义”。
- 若某条控制流路径上**没有**对该 alloca 的 store，只有 load，则进入该块时 `stack = current_val[alloca]` **为空**。
- 此时遇到 `load` 即典型的 **use-before-def**：语义上该路径未定义，但 IR 里仍有一条“从该 alloca load”的指令。

---

## 2. 根因分析

### 2.1 Mem2Reg 中 Load 的处理逻辑（修复前）

`RenameBlock` 里对 LoadInst 的处理大致如下（修复前）：

```cpp
// ── LoadInst from a promotable alloca: replace with current SSA value.
if (auto* load = dynamic_cast<ir::LoadInst*>(inst)) {
    for (ir::AllocaInst* alloca : ctx.allocas) {
        if (load->GetPointerOperand() == alloca) {
            auto& stack = ctx.current_val[alloca];
            if (!stack.empty()) {
                load->ReplaceAllUsesWith(stack.back());
            }
            // 无论栈是否为空，都把 load 加入待删除列表
            ctx.to_erase.push_back(load);
            break;
        }
    }
    continue;
}
```

- **栈非空**：用当前 SSA 值替换该 load 的所有 use，再删除 load → 正确。
- **栈为空**（use-before-def）：**没有**调用 `ReplaceAllUsesWith`，但仍把该 load 加入 `to_erase`，后续会 **EraseFromParent(load)**。

### 2.2 Value::ReplaceAllUsesWith 对 nullptr 的行为

`src/ir/Value.cpp`：

```cpp
void Value::ReplaceAllUsesWith(Value* new_val) {
    if (!new_val) {
        return;   // 传入 nullptr 时直接返回，不替换任何 use
    }
    // ...
}
```

因此即便写成 `load->ReplaceAllUsesWith(stack.back())`（此时 `stack.back()` 未定义）或 `ReplaceAllUsesWith(nullptr)`，也**不会**把该 load 的 use 从 def-use 链上移走。

### 2.3 实际发生的错误序列

1. **RenameBlock**：某 load 对应 alloca 的 `stack` 为空 → 不替换 use → 仍把该 load 加入 `to_erase`。
2. **Step 7**：对 `to_erase` 中每条指令调用 **EraseFromParent(load)**：
   - 从基本块中移除该指令并 **析构**；
   - 但其它指令（如 phi、分支、二元运算等）的 operand / incoming 里**仍持有对该 load 的 use**（指针仍指向已释放内存）。
3. **Mem2Reg 返回后**，主流程继续执行，例如：
   - `result.module->Print(llvm_out, ...)` 遍历 IR 并访问每个 Value；
   - 或 MIPS 后端遍历指令与操作数。
4. 一旦访问到这些“仍指向已删除 load”的 use，就会对**已释放内存**解引用 → **segmentation fault**。

### 2.4 为何“有时报错有时不报错”（像玄学）

这是典型的 **use-after-free**：

- 崩溃发生在**第一次解引用悬空指针**的时刻，可能是 Print，也可能是后端或其它遍历。
- 是否崩溃、以及崩溃在哪儿，取决于：
  - **堆布局**：被 free 掉的那块内存是否已被复用、是否被覆盖；
  - **遍历顺序**：先遍历到“悬空 use”还是先遍历到其它无关代码。
- 因此：换一个 testcase、多几条/少几条变量、换编译器/优化级别，都可能改变分配和遍历顺序，从而出现“同一份源码有时崩有时不崩”的现象。这不是逻辑有时对有时错，而是**未定义行为**的典型表现。

---

## 3. 修复方案

### 3.1 思路

在 **use-before-def**（`stack.empty()`）时，仍然要给该 load 的**所有 use** 一个合法替代值，再删除 load，这样删除后不会留下指向已释放指令的 use。

- 不能依赖 `ReplaceAllUsesWith(nullptr)`，因为当前实现会直接 return，不替换。
- 采用：在 Mem2Reg 能拿到 **Module** 的前提下，用 Module 的**常量**（如 `GetInt32Constant(0)` / `GetInt8Constant(0)`）作为“未定义路径”的占位，对该 load 做一次真正的 **ReplaceAllUsesWith(占位常量)**，然后再把该 load 加入 `to_erase` 并删除。

语义上相当于把“未定义读”规约为“用常量 0”（与 LLVM 的 undef 可被优化为常量的常见做法一致；若需严格 undef 可后续再引入 UndefValue）。

### 3.2 接口与调用

- **Mem2Reg** 增加重载：`bool Run(ir::Function& func, ir::Module* module)`；原 `Run(ir::Function& func)` 改为调用 `Run(func, nullptr)` 保持兼容。
- **RenameContext** 增加可选字段：`ir::Module* module = nullptr`。
- **main** 中调用改为：`mem2reg.Run(*func, result.module.get())`，这样 Mem2Reg 在 use-before-def 时能拿到 Module 并取常量。

### 3.3 RenameBlock 中 Load 的修复实现

在“栈为空”时，若 `ctx.module` 非空，则根据 load 的结果类型取占位常量并替换 use，再照常加入 `to_erase`：

```cpp
if (!stack.empty()) {
    load->ReplaceAllUsesWith(stack.back());
} else if (ctx.module) {
    // Use-before-def: 用常量 0 替换所有 use，避免删除 load 后产生悬空引用
    ir::Value* undef_placeholder = nullptr;
    ir::Type* load_ty = load->GetType();
    if (load_ty->GetTypeId() == ir::TypeID::INTEGER_TY_ID) {
        auto* int_ty = static_cast<ir::IntegerType*>(load_ty);
        undef_placeholder = (int_ty->GetBits() <= 8)
                                 ? ctx.module->GetInt8Constant(0)
                                 : ctx.module->GetInt32Constant(0);
    }
    if (undef_placeholder) {
        load->ReplaceAllUsesWith(undef_placeholder);
    }
}
// 无论是否替换，都标记删除
ctx.to_erase.push_back(load);
```

- 仅对整数类型做了占位（i8/i32）；其它类型若出现 use-before-def，仍不替换（与之前行为一致），但至少不会因整数 load 导致 SEGV。
- 若将来需要严格 undef，可在此处改为使用 Module 或 Function 级别的 UndefValue 池。

### 3.4 涉及文件与修改点小结

| 文件 | 修改内容 |
|------|----------|
| `include/pass/Mem2Reg.h` | 声明 `Run(ir::Function&, ir::Module*)`，前向声明 `ir::Module` |
| `src/pass/Mem2Reg.cpp` | RenameContext 增加 `module`；Load 分支中 `stack.empty() && ctx.module` 时用常量替换 use；实现 `Run(func)` 与 `Run(func, module)` |
| `src/main.cpp` | 调用 `mem2reg.Run(*func, result.module.get())` |

---

## 4. 与“phi 缺少 incoming”的关系

同一目录下已有文档 [bugfix_mem2reg_phi_missing_incoming_20260304.md](bugfix_mem2reg_phi_missing_incoming_20260304.md)：那里处理的是 **phi 的 incoming 数量必须等于前驱数**，在“某前驱路径上未定义”时也要填 `nullptr`（打印为 undef），否则 phi 不合法。

本次 bug 是另一面：**被提升的 alloca 的 load** 在 use-before-def 路径上若**不**做 ReplaceAllUsesWith 就删除，会留下对已删除 Instruction 的 use，导致后续任意遍历 IR 时出现 use-after-free 和 SEGV。两者都是“未定义路径”上的处理，但一个针对 phi 的 incoming，一个针对 load 的 use 链。

---

## 5. 设计启示

- Mem2Reg 中凡是“可能没有当前定义”的路径（短路、部分分支未写等），都要显式处理：
  - **Phi**：每条前驱边都要有 incoming（可为 undef/nullptr）。
  - **Load**：若该路径无定义，要么不删除该 load（难以与 alloca 删除一致），要么用合法 Value 替换其全部 use 再删除，避免悬空 use。
- Use-after-free 的崩溃时机不确定，会表现为“同一程序有时崩有时不崩”；修复时应从“消除悬空引用”入手，而不是依赖“这次没崩”。

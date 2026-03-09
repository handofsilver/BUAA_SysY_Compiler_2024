# IR 打印时 SSA 重编号与 Entry Alloca 置顶

**Date**: 2026-02-21
**Context**: 代码生成阶段：解决 .ll 输出中 SSA 编号不单调递增导致的 `lli` 报错，以及将全部 alloca 置于 entry 块最上方以符合规范形式。

---

## 一、问题现象

### 1.1 SSA 编号不递增

当控制流嵌套（例如 `for` 内层有 `if`）时，**生成 IR 时的创建顺序**和**打印时基本块的布局顺序**不一致：

| 维度 | 说明 |
|------|------|
| **创建顺序** | 先填 `for.body`（内含 `if.then`、`if.next` 的指令），再填 `for.step`、`for.after`，所以 `if.next` 里的指令先拿到小号（如 %16、%17、%18）。 |
| **布局顺序** | `blocks_` 里 for 的四个块先加入，if 的两个块后加入，因此打印顺序是：… → `for.after` → `if.then` → `if.next`。 |

结果：.ll 里先出现 `%21`（for.after 的 ret），再出现 `%16`（if.next 的 load），违反「同一函数内 SSA 名字按出现顺序单调递增」。

**错误示例（修改前）**：

```llvm
for.after.5:
  %21 = load i32, ptr %0, align 4
  ret i32 %21
if.then.14:
  br label %for.step.4
if.next.15:
  %16 = load i32, ptr %0, align 4   ;; lli: error: expected '%22' or greater
  %17 = load i32, ptr %1, align 4
  ...
```

**`lli` 报错**：

```text
lli: llvm_ir.txt:31:3: error: instruction expected to be numbered '%22' or greater
  %16 = load i32, ptr %0, align 4
```

### 1.2 Alloca 未集中在 Entry 最上方

规范做法是：函数内**所有** alloca（含嵌套 if/for 中的“局部变量”）都放在 **entry 块**，且集中在 entry 的**最上方**。原先用 `entry->AddInstruction(std::move(inst))` 把 alloca **追加**到 entry 末尾，导致 alloca 与 param 的 store 等交错，没有形成「全部 alloca 在最上端」的规范形式。

---

## 二、解决思路概览

| 问题 | 方案 | 要点 |
|------|------|------|
| SSA 不递增 | **打印时按布局顺序统一重编号** | 在输出 .ll 前，按「函数内块顺序 + 块内指令顺序」为所有 SSA 值分配新的 0,1,2,...，打印时只用该编号，与构建时编号解耦。 |
| Alloca 位置 | **插入 entry 时使用 push_front** | 使用 `AddInstructionAtFront`，使每次新 alloca 都插到 entry 最前，保证所有 alloca 集中在 entry 顶部。 |

---

## 三、实现一：Alloca 置于 Entry 最上方

### 3.1 修改位置与代码

仅改一处：`src/IRGenVisitor.cpp` 中 `CreateEntryBlockAlloca`，把「追加到末尾」改为「插入到最前」。

```cpp
// src/IRGenVisitor.cpp (CreateEntryBlockAlloca 末尾)

ir::BasicBlock* entry = blocks.front().get();
// ...
auto inst = std::make_unique<ir::AllocaInst>(name, ptr_type, entry);
ir::Instruction* result = inst.get();
// 原：entry->AddInstruction(std::move(inst));
entry->AddInstructionAtFront(std::move(inst));   // 改：插入到 entry 最前
return result;
```

`BasicBlock::AddInstructionAtFront` 在 `src/ir/BasicBlock.cpp` 中就是用 `push_front`：

```cpp
// src/ir/BasicBlock.cpp

void BasicBlock::AddInstructionAtFront(std::unique_ptr<Instruction> inst) {
    if (inst) {
        inst->SetParent(this);
    }
    instructions_.push_front(std::move(inst));
}
```

### 3.2 效果

- 所有 alloca 都集中在 entry 块**最上方**（后创建的会插到更前面，整体仍都在顶部）。
- 不改变语义，仅满足「all allocas at the very top of entry」的规范形式。

**修改后 entry 段示例**：

```llvm
entry:
  %0 = alloca i32, align 4
  %1 = alloca i32, align 4
  store i32 0, ptr %1, align 4
  store i32 0, ptr %0, align 4
  br label %for.cond.1
```

---

## 四、实现二：打印时 SSA 与块标签重编号

### 4.1 核心思路：「是否已分配打印名」由 Map 决定

- **不在 Value 上存“是否已分配”的状态**，而是：在**打印函数体之前**，对整函数按**布局顺序**做一次预扫描，为需要编号的 SSA 值和基本块分配打印用名字，存入 `IRPrintContext` 的两个 map。
- **判断“是否已为当前 Value 分配打印名”**：打印时若传入的 `IRPrintContext* context` 非空，则查 map：
  - `context->GetSSAName(v, out)` 为 true → 用 `out` 作为 SSA 打印名；
  - 若为 BasicBlock 且 `context->GetBlockLabel(bb, out)` 为 true → 用 `out` 作为块标签；
  - 否则走原有默认打印（如 ConstantInt 的 `i32 0`、GlobalVar 的 `@name`）。

即：**“是否已分配” = 该 Value/Block 是否出现在本次打印前构建的 map 中**，不依赖 Value 自身字段。

### 4.2 块标签可读性

保留语义名（如 `for.cond`、`if.then`），再按**布局顺序**加一个序号：

- 第 0 个块：`entry`（不加重编号）。
- 其余块：`<base_name>.<layout_index>`，例如 `for.cond.1`、`for.body.2`、`for.step.3`、`for.after.4`、`if.then.5`、`if.next.6`。

若块名在构建时已带后缀（如 `for.cond.2`），会先去掉末尾的 `.数字` 得到 base（如 `for.cond`），再与本次分配的块序号拼接。

---

### 4.3 新增：IRPrintContext

**头文件** `include/ir/IRPrintContext.h`：只暴露 Build 与两个查询接口，内部用两个 map 存「Value → 打印用 SSA 名」「BasicBlock → 打印用块标签」。

```cpp
// include/ir/IRPrintContext.h (节选)

class IRPrintContext {
public:
    void Build(const Function* func);
    bool GetSSAName(const Value* val, std::string& out) const;
    bool GetBlockLabel(const BasicBlock* bb, std::string& out) const;

private:
    std::unordered_map<const Value*, std::string> value_to_name_;
    std::unordered_map<const BasicBlock*, std::string> block_to_label_;
};
```

**Build 逻辑**（`src/ir/IRPrintContext.cpp`）：按「参数 → 按顺序的 blocks → 每块内指令」遍历，为参数和“产生结果的指令”分配递增的 SSA 打印名，为每块分配块标签。

```cpp
// src/ir/IRPrintContext.cpp (Build 核心)

void IRPrintContext::Build(const Function* func) {
    value_to_name_.clear();
    block_to_label_.clear();
    if (!func) return;

    int next_ssa = 0;
    int block_index = 0;

    // 1) 参数先占 0, 1, 2, ...
    for (const auto& arg : args) {
        if (arg) value_to_name_[arg.get()] = std::to_string(next_ssa++);
    }

    // 2) 按块顺序：给块赋标签，给“产生结果的指令”赋 SSA 名
    for (const auto& block : blocks) {
        BasicBlock* bb = block.get();
        std::string label = (block_index == 0)
            ? "entry"
            : (GetBlockBaseName(bb) + "." + std::to_string(block_index));
        block_to_label_[bb] = label;
        block_index++;

        for (const auto& inst : bb->GetInstructions()) {
            if (inst && InstructionProducesValue(inst.get())) {
                value_to_name_[inst.get()] = std::to_string(next_ssa++);
            }
        }
    }
}
```

**哪些指令“产生 SSA 值”**：非 Store/Branch/Return，且 Call 非 void 返回。用本地辅助函数判断：

```cpp
// src/ir/IRPrintContext.cpp (匿名 namespace)

bool InstructionProducesValue(const Instruction* inst) {
    if (dynamic_cast<const StoreInst*>(inst)) return false;
    if (dynamic_cast<const BranchInst*>(inst)) return false;
    if (dynamic_cast<const ReturnInst*>(inst)) return false;
    const CallInst* call = dynamic_cast<const CallInst*>(inst);
    if (call && call->GetType() && call->GetType()->GetTypeId() == TypeID::VOID_TY_ID)
        return false;
    return true;
}
```

**GetBlockBaseName**：从块名去掉末尾的 `.\d+`，得到可读的 base（如 `for.cond.2` → `for.cond`），用于和 `block_index` 拼成 `for.cond.1` 等。

```cpp
// src/ir/IRPrintContext.cpp (GetBlockBaseName 思路)

std::string GetBlockBaseName(const BasicBlock* bb) {
    const std::string& name = bb->GetName();
    if (name.empty() || name == "entry") return name.empty() ? "0" : "entry";
    std::string::size_type pos = name.find_last_of('.');
    // 若后缀全是数字则截掉，否则保留整名
    // ...
    return name.substr(0, pos);  // e.g. "for.cond.2" -> "for.cond"
}
```

---

### 4.4 打印链路如何接入 Context

**1) Function::Print**（`src/ir/Function.cpp`）：对有 body 的函数先 Build 再打印；参数名和块打印都带 `ctx`。

```cpp
// src/ir/Function.cpp (定义部分)

IRPrintContext ctx;
ctx.Build(this);

// 参数列表：用 ctx 里的 SSA 名
for (size_t i = 0; i < args_.size(); ++i) {
    Argument* arg = args_[i].get();
    if (arg && arg->GetType()) {
        arg->GetType()->Print(os);
        std::string name;
        if (ctx.GetSSAName(arg, name)) {
            os << " %" << name;
        } else {
            os << " %" << (arg->GetName().empty() ? std::to_string(i) : arg->GetName());
        }
    }
}
// ...
for (const auto& block : blocks_) {
    if (block) block->Print(os, &ctx);
}
```

**2) BasicBlock::Print**（`src/ir/BasicBlock.cpp`）：块标签从 context 取，指令打印传入 context。

```cpp
// src/ir/BasicBlock.cpp

void BasicBlock::Print(std::ostream& os, const IRPrintContext* context) const {
    std::string label;
    if (context && context->GetBlockLabel(this, label)) {
        os << label << ":\n";
    } else {
        os << (GetName().empty() ? "0" : GetName()) << ":\n";
    }
    for (const auto& inst : instructions_) {
        if (inst) {
            inst->Print(os, context);
            os << "\n";
        }
    }
}
```

**3) Instruction::Print**（`src/ir/Instruction.cpp`）：左侧结果名用 `InstPrintName(this, context)`，操作数用 `PrintAsOperand(os, context)`。

辅助函数统一从 context 取 SSA 名（有则用，无则用 `GetName()`）：

```cpp
// src/ir/Instruction.cpp (匿名 namespace)

std::string InstPrintName(const Instruction* inst, const IRPrintContext* context) {
    if (!context) return inst->GetName().empty() ? "0" : inst->GetName();
    std::string name;
    if (context->GetSSAName(inst, name)) return name;
    return inst->GetName().empty() ? "0" : inst->GetName();
}
```

指令打印示例（AllocaInst）：

```cpp
// src/ir/Instruction.cpp

void AllocaInst::Print(std::ostream& os, const IRPrintContext* context) const {
    os << "  %" << InstPrintName(this, context) << " = alloca ";
    // ... 类型与 align
}
```

**4) Value::PrintAsOperand**（`src/ir/Value.cpp`）：有 context 时先查 Block 标签再查 SSA 名，否则走默认打印。

```cpp
// src/ir/Value.cpp

void Value::PrintAsOperand(std::ostream& os, const IRPrintContext* context) const {
    if (context) {
        const BasicBlock* bb = dynamic_cast<const BasicBlock*>(this);
        if (bb) {
            std::string label;
            if (context->GetBlockLabel(bb, label)) {
                os << "label %" << label;
                return;
            }
        }
        std::string s;
        if (context->GetSSAName(this, s)) {
            os << "%" << s;
            return;
        }
    }
    DefaultPrintAsOperand(os);
}

void Value::DefaultPrintAsOperand(std::ostream& os) const {
    os << "%" << (name_.empty() ? "0" : name_);
}
```

子类（Argument、BasicBlock、ConstantInt、GlobalVar、Function）只重写 `DefaultPrintAsOperand`，不重写两参的 `PrintAsOperand`，这样在无 context 或 map 中无此项时行为与原来一致。

---

### 4.5 涉及文件一览

| 文件 | 变更摘要 |
|------|----------|
| `include/ir/IRPrintContext.h` | 新增：Build / GetSSAName / GetBlockLabel，内部 value_to_name_、block_to_label_。 |
| `src/ir/IRPrintContext.cpp` | 新增：Build 逻辑、InstructionProducesValue、GetBlockBaseName。 |
| `include/ir/Value.h` | PrintAsOperand 增加可选 `context`；新增 DefaultPrintAsOperand。 |
| `src/ir/Value.cpp` | PrintAsOperand 中根据 context 查 map，否则 DefaultPrintAsOperand。 |
| `include/ir/Instruction.h` | Print 增加可选 `context`；所有子类 override 签名同步。 |
| `src/ir/Instruction.cpp` | 所有 Print 实现接 context，用 InstPrintName 与 PrintAsOperand(os, context)。 |
| `include/ir/BasicBlock.h` | Print 增加可选 context；DefaultPrintAsOperand 替代原 PrintAsOperand。 |
| `src/ir/BasicBlock.cpp` | Print 使用 context 输出块标签；指令 Print(os, context)。 |
| `include/ir/Function.h` | DefaultPrintAsOperand 替代原 PrintAsOperand。 |
| `src/ir/Function.cpp` | 有 body 时 Build(ctx)，参数与块打印均传入 ctx。 |
| `include/ir/Argument.h`，`src/ir/Argument.cpp` | DefaultPrintAsOperand 替代原 PrintAsOperand。 |
| `include/ir/Constant.h`，`src/ir/Constant.cpp` | ConstantInt：DefaultPrintAsOperand。 |
| `include/ir/GlobalVar.h`，`src/ir/GlobalVar.cpp` | DefaultPrintAsOperand。 |
| `src/IRGenVisitor.cpp` | CreateEntryBlockAlloca 中 AddInstruction → AddInstructionAtFront。 |

---

## 五、小结与可复用要点

| 要点 | 说明 |
|------|------|
| **SSA 编号来源** | 仅以**打印前**按布局顺序构建的 map 为准；构建阶段分配的 SSA 名不再参与 .ll 输出，避免“创建顺序 ≠ 布局顺序”导致的不递增。 |
| **“是否已分配打印名”** | 由 **IRPrintContext 的 map 是否存在该 Value/Block** 决定，不在 Value 上增加状态。 |
| **块标签** | 保留语义名 + 布局序号（如 for.cond.1、if.then.5），便于阅读且满足 .ll 要求。 |
| **Alloca 位置** | 统一通过 `AddInstructionAtFront` 插入 entry，保证全部 alloca 在 entry 最上方。 |
| **常量/全局量** | ConstantInt、GlobalVar、Function 不进入 SSA map，仍通过 DefaultPrintAsOperand 按原样输出（如 i32 0、@name）。 |

按上述实现后，任意控制流嵌套生成的 .ll 均满足 SSA 编号单调递增，且 alloca 均在 entry 顶部，可通过 `lli` 验证（如 for+continue 用例返回 40）。

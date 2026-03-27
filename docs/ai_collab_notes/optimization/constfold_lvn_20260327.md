# 常量折叠 + 局部值编号（ConstFold + LVN Pass）

**日期**: 2026-03-27
**前置状态**: PhiInst operand 模型升级完成（`FinalizeOperands` 已接入 Mem2Reg），def-use 链现在覆盖所有指令的所有操作数，RAUW 无死角
**关联文档**: [优化阶段规划](optimization_phase_plan_20260310.md)、[PhiInst 升级记录](phi_operand_upgrade_20260327.md)、[课程优化教程 §（4）](../../course_info/optimization_course_guide.md)

---

## 一、背景：现在能做什么了

### 1.1 里程碑：def-use 链完整

在 [PhiInst operand 模型升级](phi_operand_upgrade_20260327.md) 完成之前，我们的 IR 有一个根本性的缺陷：`PhiInst` 的 incoming values 存在独立的 `vector<IncomingPair>` 中，**游离于 def-use 链之外**。这意味着：

- `value->GetUseList()` 看不到 Phi 对该 value 的引用 → DCE 会误判"没人用"
- `RAUW(new_val)` 替换一个值时，Phi 中对该值的引用**不会被更新** → 残留悬垂指针

现在，`Mem2Reg::Run()` 末尾会对每个 `PhiInst` 调用 `FinalizeOperands()`，将所有 incoming values 一次性注册进 `User::operands_`。从此：

- **所有指令的所有操作数**都在 `operands_` 中，通过 `Use` 对象双向挂接在 def-use 链上
- `RAUW` 调用一次，覆盖该值的**全部使用者**——包括 Phi 节点
- `GetUseList().empty()` 可以可靠地判断一个值是否还有使用者

**这是进入优化阶段的里程碑。** 现在可以安全地实现第一个正式的 IR 优化 Pass。

### 1.2 第一炮：为什么选择 ConstFold + LVN

参考 [optimization_phase_plan_20260310.md §三](optimization_phase_plan_20260310.md) 的 ROI 排序：

| 优化 | 前置条件 | 复杂度 | 推荐 |
|------|----------|--------|------|
| **O1 常量折叠 + 代数化简** | ✅ SSA + RAUW | ⭐ 低 | ✅ 必做 |
| **O2 DCE** | ✅ SSA + def-use | ⭐⭐ 低-中 | ✅ 必做 |
| **O6 LVN** | ✅ SSA | ⭐⭐ 中 | ✅ 推荐（可与 O1 合并） |

计划推荐将 O1 和 O6 合并为一个 `ConstFoldLVNPass`，原因：

1. LVN 的主循环就是"逐块遍历所有指令"，与常量折叠完全同路
2. 常量折叠完成后立即将折叠结果送入 LVN 哈希表，二者相互增益（折叠暴露更多公共子表达式，LVN 消除公共子表达式后暴露更多折叠机会）
3. 一个 Pass 完成，减少遍历次数

---

## 二、目标 IR 模式：我们要消除什么

SysY 程序经过 IRGen + Mem2Reg 后，IR 中常见以下冗余模式：

### 2.1 模式 A：纯常量表达式

SysY 允许在初始化中使用复杂常量表达式（`const int N = 10 * 200 + 5`），IRGen 不做折叠，直接生成算术指令：

```llvm
; 原始 IR（Mem2Reg 后）
%v1 = mul i32 10, 200
%v2 = add i32 %v1, 5
```

折叠后应变为直接使用常量 `2005`，`%v1` 和 `%v2` 均可删除（由 DCE 负责）。

### 2.2 模式 B：代数恒等式

IRGen 在生成乘、除、加时不做代数简化，常见残留：

```llvm
%r1 = mul i32 %x, 1      ; → %x
%r2 = add i32 %y, 0      ; → %y
%r3 = mul i32 %z, 0      ; → 0
%r4 = sdiv i32 %w, 1     ; → %w
%r5 = sub i32 %a, 0      ; → %a
%r6 = sub i32 %a, %a     ; → 0
```

这类指令不能由常量折叠消除（操作数不全是常量），但可以通过代数规则直接用 RAUW 替换成更简单的值。

### 2.3 模式 C：块内公共子表达式

课程教程中 GVN/LVN 的经典例子，在 SysY 的数组访问中极为常见：

```llvm
; 循环体中对数组多次同索引访问
%v54 = mul i32 %k, %k     ; k * k
%v55 = add i32 %arr0, %v54
%v62 = mul i32 %k, %k     ; 同一基本块中重复计算 k * k
%v63 = add i32 %arr1, %v62
...（再出现 8 次）
```

LVN 能识别出 `(MUL, %k, %k)` 在同一基本块内首次出现后，后续所有相同表达式直接 RAUW 指向第一次的结果，10 条 `mul` 变成 1 条。

---

## 三、原理精讲

### 3.1 常量折叠（Constant Folding）

**定义**：如果一条 `BinaryInst`（或 `IcmpInst`）的**所有操作数**都是 `ConstantInt`，则在编译期直接计算结果，用结果常量替换该指令的所有使用（RAUW），之后该指令成为"死指令"（无使用者），可由 DCE 删除。

**操作**：

```
before:  %v1 = add i32 3, 5
         %v2 = ... use of %v1 ...

after RAUW(%v1, const_8):
         %v1 = add i32 3, 5   ← 无人使用，等待 DCE
         %v2 = ... use of 8 ...
```

**对应课程教程**：`optimization_course_guide.md §（4）LVN` 中提到的"常量表达式计算（constant-expression evaluation）"。

### 3.2 代数化简（Algebraic Simplification）

**定义**：识别特殊常量操作数带来的化简机会，即使不是纯常量表达式也能简化：

| 模式 | 化简结果 | 说明 |
|------|----------|------|
| `a + 0` | `a` | 加法零元 |
| `a - 0` | `a` | 减法零元 |
| `a * 1` | `a` | 乘法单位元 |
| `a / 1` | `a` | 除法单位元 |
| `a * 0` | `0` | 乘法零律 |
| `0 * a` | `0` | 乘法零律（对称） |
| `0 / a` | `0` | 零除任何非零数为零（a 非 0 时安全） |
| `a - a` | `0` | 自减恒零（需操作数是同一 Value*） |
| `a / a` | `1` | 自除恒一（需操作数是同一 Value*，a 非 0） |
| `0 + a` | `a` | 交换律变形 |
| `1 * a` | `a` | 交换律变形 |

> **安全说明**：`a / a = 1` 和 `0 / a = 0` 仅在 SysY 语义下（无未定义行为的整数语义）安全。SysY 不保证 a 非零，因此 `a / a` 和 `0 / a` **暂不实现**，除非能静态证明 `a != 0`。本 Pass 的保守版本只实现表格中前 5 行和 `a - a`。

### 3.3 局部值编号（LVN，Local Value Numbering）

**定义**：在单个 `BasicBlock` 的指令序列中，为每条"有意义的计算"（BinaryInst、IcmpInst、ZextInst 等）维护一张哈希表：

```
key:   (op_kind, value_id_of_lhs, value_id_of_rhs)
value: first_instruction_that_computed_this
```

遍历到一条指令时：
1. 先尝试常量折叠/代数化简（见 3.1/3.2），若成功则 RAUW 然后继续
2. 否则，计算该指令的 LVN key
3. 若 key 已在表中 → RAUW 成当前 key 对应的首次结果（公共子表达式消除）
4. 若 key 不在表中 → 将 `(key, this_instruction)` 插入表

**关键：如何用 Value 的地址作为 key？**

在 SSA 形式下，每个 `Value*` 指针唯一标识一个定义。因此 LVN key 可以直接用 `(BinaryOp, Value*, Value*)` 的三元组，无需维护"值编号"的整数映射，大幅简化实现。

**交换律处理**：对 `ADD`、`MUL` 这类满足交换律的操作，`(ADD, a, b)` 和 `(ADD, b, a)` 应视为相同。做法：将两个操作数指针**统一排序**（按地址大小），保证相同表达式总是生成相同 key。

**LVN 的生效条件**：只在**同一基本块内**有效。跨块的公共子表达式需要 GVN（暂不实现）。但 SysY 循环体的重复数组计算通常在同一基本块内，LVN 已能捕捉到大部分收益。

---

## 四、Pass 结构设计

### 4.1 类设计

```cpp
// include/pass/ConstFoldLVNPass.h

namespace pass {

class ConstFoldLVNPass : public FunctionPass {
public:
    bool Run(ir::Function& func) override;
    std::string_view GetName() const override { return "ConstFoldLVN"; }

private:
    // 对单个基本块运行 ConstFold + LVN，返回是否修改了 IR
    bool RunOnBlock(ir::BasicBlock& bb);

    // 尝试对一条指令做常量折叠或代数化简
    // 返回 nullptr 表示无法化简；返回一个 Value* 表示该指令应被 RAUW 成该值
    ir::Value* TryFoldOrSimplify(ir::Instruction* inst);

    // 判断是否为 ConstantInt 并提取值（返回 false 表示不是常量）
    static bool IsConstInt(ir::Value* v, int64_t& out_val);

    // 从 Module 的常量池获取或创建 ConstantInt
    ir::ConstantInt* GetOrCreateConstInt(ir::Module& mod, int64_t val);
};

} // namespace pass
```

### 4.2 LVN key 的表示

```cpp
// 在 ConstFoldLVNPass.cpp 内部使用

struct LVNKey {
    ir::BinaryOp op;     // 或者用 int 统一表示 BinaryOp / IcmpPred
    ir::Value* lhs;
    ir::Value* rhs;

    bool operator==(const LVNKey&) const = default;
};

struct LVNKeyHash {
    size_t operator()(const LVNKey& k) const {
        size_t h = std::hash<int>{}(static_cast<int>(k.op));
        h ^= std::hash<void*>{}(k.lhs) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<void*>{}(k.rhs) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

using LVNTable = std::unordered_map<LVNKey, ir::Value*, LVNKeyHash>;
```

### 4.3 主循环逻辑（伪代码）

```cpp
ConstFoldLVNPass::RunOnBlock(BasicBlock& bb):
    LVNTable table
    changed = false

    for inst in bb.instructions (遍历副本，因为可能修改列表):
        // Step 1: 常量折叠 + 代数化简
        replacement = TryFoldOrSimplify(inst)
        if replacement != nullptr:
            inst->ReplaceAllUsesWith(replacement)
            // 注意：这里不删除 inst，删除由 DCE 负责
            changed = true
            continue  // 跳过 LVN（已经替换了）

        // Step 2: LVN（只对 BinaryInst / IcmpInst 有意义）
        if inst 是 BinaryInst 或 IcmpInst:
            key = MakeLVNKey(inst)  // 含交换律规范化
            if table.count(key):
                // 命中：公共子表达式
                inst->ReplaceAllUsesWith(table[key])
                changed = true
            else:
                // 未命中：记入表
                table[key] = inst
                // 同时将折叠后的常量结果也记入表（如果刚才折叠失败但操作数后来变常量，
                // 下一轮 Pass 迭代会重新发现）

    return changed
```

### 4.4 TryFoldOrSimplify 的详细逻辑

```cpp
TryFoldOrSimplify(Instruction* inst):
    if inst 不是 BinaryInst:
        return nullptr  // AllocaInst、BranchInst 等不处理

    lhs = inst->GetLhs()
    rhs = inst->GetRhs()
    op  = inst->GetOp()

    lhs_const = IsConstInt(lhs)  // 尝试提取常量值
    rhs_const = IsConstInt(rhs)

    // --- 纯常量折叠 ---
    if lhs_const && rhs_const:
        result = 根据 op 计算 lhs_val OP rhs_val
        // 注意：sdiv/srem 时检查 rhs_val != 0（SysY 语义下假设无除零）
        return GetOrCreateConstInt(result)

    // --- 代数化简（一侧为常量）---
    if rhs_const:
        switch op:
            ADD: if rhs_val == 0  → return lhs
            SUB: if rhs_val == 0  → return lhs
            MUL: if rhs_val == 0  → return const_0
                 if rhs_val == 1  → return lhs
            DIV: if rhs_val == 1  → return lhs
                 // DIV rhs==0：编译期未定义，不优化
    if lhs_const:
        switch op:
            ADD: if lhs_val == 0  → return rhs
            MUL: if lhs_val == 0  → return const_0
                 if lhs_val == 1  → return rhs

    // --- 同操作数化简（无论是否常量）---
    if lhs == rhs:  // 同一 Value*
        switch op:
            SUB: → return const_0
            // DIV: 暂不化简（未证明 lhs != 0）

    return nullptr  // 无法化简
```

---

## 五、RAUW 的正确使用与指令生命周期

### 5.1 为什么 ConstFoldLVNPass 只做 RAUW、不做删除

初学者容易犯的错误是：发现一条指令可以折叠后，立刻把它从 BasicBlock 中删除。这是**危险**的：

1. 我们正在**迭代** `bb.instructions`（`list<unique_ptr<Instruction>>`），删除当前指令会使迭代器失效（UB）。
2. 删除指令前，必须确保它的操作数引用已经清理（调用 `SetOperand(i, nullptr)`），否则被操作数的 `use_list_` 中会残留悬垂指针。
3. 删除和清理操作应该**集中在 DCE 中完成**——这样逻辑清晰，每个 Pass 职责单一。

**正确模式**：

```
// ConstFoldLVNPass 中：
inst->ReplaceAllUsesWith(new_val);
// inst 现在 use_list_ 为空，但仍然留在 bb 中，等待 DCE 清理
// 指令的 operands_（它使用的操作数）保持不变，直到 DCE 调用 SetOperand(i, nullptr)
```

### 5.2 迭代 BasicBlock 的安全方式

由于 RAUW 可能导致某些指令的操作数变化（但不会增删 `bb.instructions` 中的指令），在 ConstFoldLVNPass 中直接用 range-for 遍历 `bb.instructions` 是**安全的**。指令是 `std::list<std::unique_ptr<Instruction>>` 管理的，遍历时不会失效（除非有删除操作）。

```cpp
// 安全：仅 RAUW，不删除
for (auto& inst_ptr : bb.GetInstructions()) {
    ir::Instruction* inst = inst_ptr.get();
    // ... fold / LVN ...
}
```

### 5.3 RAUW 的传播效应与多轮迭代

单轮 Pass 可能发现不了所有优化机会。例如：

```
轮 1：%v1 = mul 2, 3  → 折叠为 6，RAUW(%v1, 6)
      %v2 = add %v1, 4 → v1 已被替换成 6，但这条指令在 v1 折叠之后才看到
                          ↑ 如果 v2 在遍历中在 v1 之后，当轮就能看到 v2=add 6,4→10
                          ↑ 如果 v2 在遍历中在 v1 之前（不可能，因为 SSA def 先于 use）
```

在 SSA 形式下，值的定义**总是在使用之前**（按基本块内的顺序），所以**单轮遍历**就可以捕捉到同一基本块内所有传播链。

但跨块的情况（如循环 φ 节点）需要多轮。因此在 `main.cpp` 中应以**"直到不再变化"**为停止条件迭代：

```cpp
// main.cpp
bool changed = true;
while (changed) {
    changed = false;
    for (auto& func : module->GetFunctions()) {
        pass::ConstFoldLVNPass cf_lvn;
        changed |= cf_lvn.Run(*func);
    }
}
```

实践中通常 2-3 轮即收敛。

---

## 六、IcmpInst 的折叠

`IcmpInst` 也可以做常量折叠：如果两侧都是 `ConstantInt`，在编译期直接计算比较结果，替换为 `i1` 类型的常量（`true`/`false`，即 `ConstantInt(1)` 或 `ConstantInt(0)`）。

这会暴露控制流优化机会：`BranchInst` 的条件变成了常量后，某个分支永远不会被走，形成**不可达基本块**。这属于 DCE 的扩展（不可达代码消除），暂不在本 Pass 中处理，记录为后续工作项。

```llvm
; 原始 IR（某 const 程序中 cond 已知为 1）
%cond = icmp sgt i32 1, 0    ; 折叠后 → i1 true（常量 1）
br i1 %cond, label %bb2, label %bb3   ; 条件变成常量后，bb3 不可达
```

---

## 七、Module 常量池

常量折叠的结果是一个 `ConstantInt`。我们不能每次折叠都 `new ConstantInt`——这会产生大量重复的常量对象，浪费内存，且两个"值为 8 的常量"其实应该是同一个对象（这样 LVN 的 Value* 比较才能正确工作）。

`ir::Module` 已经有常量池机制（`GetOrCreateConstInt`），Pass 应通过这个接口获取常量：

```cpp
// ConstFoldLVNPass 需要持有 Module 的引用
// 在 Run(ir::Function& func) 中：
ir::Module* mod = func.GetParent();  // Function → Module

// 创建/复用常量：
ir::ConstantInt* c = mod->GetOrCreateConstInt(result_value);
```

> 如果 `Function` 类目前没有 `GetParent()` 方法返回 `Module*`，需要在 `ir::Function` 中补加，或在 `ConstFoldLVNPass::Run` 的签名中额外传入 `Module&`。需确认当前接口。

---

## 八、涉及的改动清单

| 文件 | 改动 |
|------|------|
| `include/pass/ConstFoldLVNPass.h` | **新增**：ConstFoldLVNPass 类声明 |
| `src/pass/ConstFoldLVNPass.cpp` | **新增**：Run / RunOnBlock / TryFoldOrSimplify 实现 |
| `src/main.cpp`（或 `Driver.cpp`）| 在 Mem2Reg 之后、MIPS 生成之前接入 Pass；`while(changed)` 循环迭代 |
| `CMakeLists.txt` | 添加 `src/pass/ConstFoldLVNPass.cpp` 到编译目标 |

不需要改动的文件：
- `ir/Value.h`、`ir/User.h`、`ir/Use.h`：def-use 基础设施不变
- `ir/Instruction.h`：PhiInst/BinaryInst 等接口已满足需求
- `mips/` 目录下所有文件：IR 优化在 MIPS 生成之前完成，后端不感知

---

## 九、Before / After 对照

### 示例 1：常量折叠链

**源 SysY**：
```c
const int N = 10 * 200 + 5;
int x = N - 5;
```

**Mem2Reg 后 IR**：
```llvm
define i32 @main() {
b1:
    %v1 = mul i32 10, 200       ; 10 * 200
    %v2 = add i32 %v1, 5        ; + 5 = 2005
    %v3 = sub i32 %v2, 5        ; - 5 = 2000
    ; ... 使用 %v3 ...
}
```

**ConstFoldLVN 后 IR**（RAUW 之后，DCE 前）：
```llvm
define i32 @main() {
b1:
    %v1 = mul i32 10, 200       ; ← 无人使用，等待 DCE
    %v2 = add i32 %v1, 5        ; ← 无人使用，等待 DCE
    %v3 = sub i32 %v2, 5        ; ← 无人使用，等待 DCE
    ; ... 直接使用常量 2000 ...
}
```

**ConstFoldLVN + DCE 后 IR**：
```llvm
define i32 @main() {
b1:
    ; ... 直接使用常量 2000，三条 mul/add/sub 全部消失 ...
}
```

### 示例 2：LVN 消除循环体中重复计算

**Mem2Reg 后 IR（循环体基本块片段）**：
```llvm
b3:
    %v54 = mul i32 %k, %k
    %v55 = add i32 %arr0, %v54
    store i32 %v55, i32* ...
    %v62 = mul i32 %k, %k       ; ← 与 %v54 完全相同
    %v63 = add i32 %arr1, %v62
    store i32 %v63, i32* ...
    ; ... 再重复 8 次 ...
```

**ConstFoldLVN 后 IR**（第一次出现 `mul %k, %k` 记入 LVN 表，后续命中后 RAUW）：
```llvm
b3:
    %v54 = mul i32 %k, %k       ; ← 唯一保留的 mul
    %v55 = add i32 %arr0, %v54
    store i32 %v55, i32* ...
    ; %v62 → RAUW 为 %v54（LVN 命中）
    %v63 = add i32 %arr1, %v54  ; ← 直接用 %v54
    store i32 %v63, i32* ...
    ; ... 后续 8 个 mul 同理 ...
```

10 条 `mul` 减为 1 条。DCE 后 9 条死 `mul` 全部清除。

### 示例 3：代数化简 `a * 1 = a`

```llvm
; Mem2Reg 后（数组索引 a[i * 1]）
%v15 = mul i32 %arr_load, 1

; ConstFoldLVN 后（代数化简 → RAUW）
; %v15 的所有使用直接改为 %arr_load
; %v15 本身成为死指令，等待 DCE
```

---

## 十、验证策略

### 10.1 正确性验证

1. **编译无 Warning**：Pass 实现后先确保编译通过
2. **全量测试回归**：用 `SysY_Test_2024/` 下所有测试用例，运行前后 `mips.txt` 产生相同执行结果（MARS 4.5 验证）
3. **IR diff 检查**：对几个典型用例对比优化前后的 `llvm_ir.txt`，确认折叠和 LVN 按预期触发
4. **不做折叠的情况**：确认 `sdiv x, 0` 不会被错误地折叠（被除数为 0 时不应折叠）

### 10.2 量化收益

```bash
# 对比 MIPS 指令条数
wc -l mips_before.txt mips_after.txt

# 对比 IR 指令条数
grep -c '=' llvm_ir_before.txt llvm_ir_after.txt
```

期望：常量表达式密集的测试用例（含大量 `const int` 表达式）IR 指令数有显著减少；数组密集访问的测试用例（如循环体）MUL 指令条数大幅减少。

### 10.3 开关控制

在 `main.cpp` 中用布尔开关独立控制，便于回归对比：

```cpp
constexpr bool kEnableConstFoldLVN = true;

if (kEnableConstFoldLVN) {
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& func : module->GetFunctions()) {
            pass::ConstFoldLVNPass cf;
            changed |= cf.Run(*func);
        }
    }
}
```

---

## 十一、后续展望：DCE

ConstFoldLVN Pass 完成后，IR 中会出现大量"死指令"——它们已被 RAUW 替换，`use_list_` 为空，但仍留在 BasicBlock 中。这正是 **O2 DCE（死代码消除）**的输入。

DCE 的工作流程：

1. **标记有用根**：所有有副作用的指令 → `ReturnInst`、`StoreInst`（写内存）、`CallInst`（函数调用可能有 I/O 等副作用）、`BranchInst`（控制流）
2. **沿 def-use 链反向传播**：对每条"有用"指令，将它的所有操作数的定义指令也标记为"有用"（BFS/DFS）
3. **删除未标记指令**：遍历所有基本块，对没有被标记的指令——
   - 先调用 `SetOperand(i, nullptr)` 对每个操作数解挂 use_list_
   - 然后从 BasicBlock 中 `EraseFromParent()`

ConstFoldLVN + DCE 的组合形成**完整的"无用计算消除"流水线**：

```
Mem2Reg → [ConstFoldLVN loop] → DCE → MipsEmitter
```

建议在实现 DCE 时同时处理**不可达基本块**（`BranchInst` 条件为常量 → 某个后继永远不可达），这能为后端的基本块合并优化提供更干净的 IR 输入。

---

*本文档记录 ConstFoldLVNPass 的设计背景、原理与实现方案。实施完成后，在 `optimization_phase_plan_20260310.md` 中更新 O1/O6 的状态，并补充实际的 diff 数据。*

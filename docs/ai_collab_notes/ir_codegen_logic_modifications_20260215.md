# IR 代码生成：逻辑修改与问题解答

**Date**: 2026-02-15  
**Scope**: 针对「Visit 示范、Zext/Trunc 必要性、作用域优雅性、ConstExp 与语义信息复用」所做的逻辑修改说明。不涉及 .h/.cpp 循环依赖与编译报错的修复（该部分见 `ir_circular_dependency_fix_20260215.md`）。

---

## 一、问题回顾

1. **示范实现**：希望有 `VisitNumber`、`VisitBinaryExp`、`VisitAssignStmt` 的示例实现；并询问是否需要 **ZextInst / TruncInst**。
2. **作用域管理**：语义分析阶段已有 `SymbolTable` 与 `ScopeGuard`，但 IR 生成不能直接复用 `ScopeGuard`，这样是否影响实现的优雅性？
3. **ConstExp 与语义信息**：语义分析已做常量折叠，结果存在 `Symbol.const_values` 中，IR 生成是否应利用这些语义信息？

下文按「逻辑修改」与「如何回答上述问题」两部分说明，并尽量给出具体代码位置。

---

## 二、逻辑修改总览

| 修改项 | 位置 | 目的 |
|--------|------|------|
| `ConstantInt` + Module 常量池 | `ir/Constant.h`, `ir/Module.h`, `ir/Module.cpp` | 字面量/常量在 IR 中作为 `Value*` 使用，避免重复创建 |
| `IcmpInst` / `ZextInst` / `TruncInst` | `ir/Instruction.h`, `IRBuilder.h` | 关系运算结果 i1→i32、char 与 int 互转 |
| `VisitNumber` 示范 | `IRGenVisitor.cpp` | 整数常量 → `GetInt32Constant` |
| `VisitBinaryExp` 示范 | `IRGenVisitor.cpp` | 算术用 `CreateBinary`，比较用 `CreateIcmp` + `CreateZext` |
| `VisitAssignStmt` 示范 | `IRGenVisitor.cpp` | `is_lval_mode_` 取地址 + 求值 + `CreateStore` |
| `VisitCompUnit` 遍历方式 | `IRGenVisitor.cpp` | 显式遍历 decls/func_defs/main，避免 `Accept` 递归回 `VisitCompUnit` |

---

## 三、具体逻辑修改与对应代码

### 3.1 整数常量：ConstantInt 与 Module 常量池

**问题**：表达式里的整数（如 `Number`、常量初值）在 IR 里应对应「常量」而不是指令；需要统一创建与复用。

**逻辑**：

- 在 IR 中增加 **`ConstantInt`**（继承 `Constant`），表示 `i32`/`i8` 等整数常量，仅作为操作数使用，不插入 BasicBlock。
- **Module** 持有常量池：对每个不同的 `int64_t` 值至多创建一个 `ConstantInt`，通过 `GetInt32Constant(value)` 返回 `Value*`，供 Visitor 写入 `temp_value_`。

**代码位置**：

- **ConstantInt 定义**：`include/ir/Constant.h`

```cpp
class ConstantInt : public Constant {
public:
    ConstantInt(const std::string& name, Type* type, int64_t value) : Constant(name, type), value_(value) {}
    int64_t GetValue() const { return value_; }
private:
    int64_t value_;
};
```

- **Module 接口与成员**：`include/ir/Module.h`

```cpp
IntegerType* GetI32Type();
IntegerType* GetI8Type();
IntegerType* GetI1Type();
ConstantInt* GetInt32Constant(int64_t value);

private:
    std::vector<std::unique_ptr<IntegerType>> integer_types_;
    std::vector<std::unique_ptr<ConstantInt>> constants_;
    std::unordered_map<int64_t, ConstantInt*> const_i32_cache_;
```

实现逻辑：`GetInt32Constant(value)` 先查 `const_i32_cache_`，若无则 `make_unique<ConstantInt>(..., GetI32Type(), value)`，存入 `constants_` 并写入 cache，返回裸指针。这样同一数值在整份 IR 中只对应一个常量，便于打印与后续优化。

---

### 3.2 关系运算与类型转换：IcmpInst、ZextInst、TruncInst

**问题**：SysY 中关系/相等运算结果为 0/1（int），而 LLVM 的 `icmp` 结果为 i1；另外 char 与 int 混用时需要 i8↔i32 的扩展与截断。因此需要明确：**是否需要 ZextInst / TruncInst？**

**结论**：**需要**。

- **IcmpInst**：关系/相等运算生成 `icmp`，结果为 i1。
- **ZextInst**：在需要「0/1 整数」的语义（如表达式里的 `a < b` 结果参与运算）时，用 `zext i1 to i32` 得到 i32。
- **TruncInst**：把 i32 存回 char 时用 `trunc i32 to i8`。

**逻辑**：

- 在 Instruction 层增加 **`IcmpPred`**（SLT/SGT/SLE/SGE/EQ/NE）和 **`IcmpInst`**；增加 **`ZextInst`**、**`TruncInst`**。
- IRBuilder 增加：`CreateIcmp(result_type, pred, lhs, rhs)`、`CreateZext(value, dest_type)`、`CreateTrunc(value, dest_type)`。其中 `result_type` 一般为 `module->GetI1Type()`。

**代码位置**：

- **指令定义**：`include/ir/Instruction.h`

```cpp
enum class IcmpPred { SLT, SGT, SLE, SGE, EQ, NE };
class IcmpInst : public Instruction { ... };
class ZextInst : public Instruction { ... };
class TruncInst : public Instruction { ... };
```

- **IRBuilder**：`include/IRBuilder.h`

```cpp
Instruction* CreateIcmp(Type* result_type, IcmpPred pred, Value* lhs, Value* rhs);
Instruction* CreateZext(Value* value, Type* dest_type);
Instruction* CreateTrunc(Value* value, Type* dest_type);
```

关系运算示范在 `VisitBinaryExp` 中：先 `CreateIcmp(module_->GetI1Type(), pred, lhs, rhs)`，再 `CreateZext(cmp, module_->GetI32Type())`，将结果写入 `temp_value_`，满足「表达式得到 i32 的 0/1」的语义。

---

### 3.3 示范一：VisitNumber

**逻辑**：整数字面量不生成指令，只取（或创建）一个 i32 常量并作为当前表达式的值。

**代码**：`src/IRGenVisitor.cpp`

```cpp
void IRGenVisitor::VisitNumber(Number& number) {
    temp_value_ = module_->GetInt32Constant(number.int_const);
}
```

`Number` 的 `int_const` 来自词法/语法，直接交给 `GetInt32Constant` 即可；结果通过 `temp_value_` 传给上层（如 `VisitBinaryExp`）。

---

### 3.4 示范二：VisitBinaryExp

**逻辑**：先递归求左、右子表达式的值（`temp_value_`），再按运算符分类：

- **算术**（ADD/SUB/MUL/DIV/MOD）：`CreateBinary(op, lhs, rhs)`，结果类型与操作数一致（如 i32）。
- **关系/相等**（LT/GT/LE/GE/EQ/NE）：`CreateIcmp` 得到 i1，再用 `CreateZext(..., GetI32Type())` 得到 i32 的 0/1，满足 SysY 语义。
- **逻辑与/或**（AND/OR）：需要控制流（短路），当前示范中留空，在实现 Cond 时再扩展。

**代码**：`src/IRGenVisitor.cpp`

```cpp
void IRGenVisitor::VisitBinaryExp(BinaryExp& binary_exp) {
    binary_exp.lhs->Accept(*this);
    ir::Value* lhs = temp_value_;
    binary_exp.rhs->Accept(*this);
    ir::Value* rhs = temp_value_;
    if (!lhs || !rhs || !builder_->GetInsertBlock()) return;
    OpType op = binary_exp.op;
    if (IsArithmeticOp(op)) {
        ir::Instruction* inst = builder_->CreateBinary(op, lhs, rhs);
        temp_value_ = inst ? inst : temp_value_;
        return;
    }
    std::optional<ir::IcmpPred> pred = OpTypeToIcmpPred(op);
    if (pred) {
        ir::Instruction* cmp = builder_->CreateIcmp(module_->GetI1Type(), *pred, lhs, rhs);
        if (cmp) {
            ir::Instruction* zext = builder_->CreateZext(cmp, module_->GetI32Type());
            temp_value_ = zext ? zext : cmp;
        }
        return;
    }
    // AND / OR (short-circuit) require control flow; extend when implementing Cond.
}
```

辅助函数 `OpTypeToIcmpPred`、`IsArithmeticOp` 在同一文件匿名命名空间中实现，将 AST 的 `OpType` 映射到 `IcmpPred` 或判断是否为算术 op。

---

### 3.5 示范三：VisitAssignStmt

**逻辑**：赋值需要「左值的地址」和「右值的值」：

1. 置 **`is_lval_mode_ = true`**，访问 LVal，得到**地址**（alloca 或 GEP 结果）存入 `temp_value_`。
2. 保存该地址到局部变量 `addr`。
3. 置 **`is_lval_mode_ = false`**，访问 Exp，得到**值**到 `temp_value_`。
4. 用 **`CreateStore(val, addr)`** 生成 store。

这样 LVal 在「赋值左值」与「作为值使用」两种语境下的行为由 `is_lval_mode_` 统一区分。

**代码**：`src/IRGenVisitor.cpp`

```cpp
void IRGenVisitor::VisitAssignStmt(AssignStmt& assign_stmt) {
    is_lval_mode_ = true;
    assign_stmt.lval->Accept(*this);
    ir::Value* addr = temp_value_;
    is_lval_mode_ = false;
    assign_stmt.exp->Accept(*this);
    ir::Value* val = temp_value_;
    if (addr && val && builder_->GetInsertBlock()) {
        builder_->CreateStore(val, addr);
    }
}
```

---

### 3.6 VisitCompUnit 的遍历方式（避免递归）

**问题**：若在 `VisitCompUnit` 里直接写 `comp_unit.Accept(*this)`，会再次进入 `VisitCompUnit`，造成无限递归。

**逻辑**：CompUnit 是根节点，只需按文法顺序处理「声明 + 函数定义 + main」；不通过 `Accept` 再进 CompUnit，而是**显式遍历**子节点并对其调用 `Accept`。

**代码**：`src/IRGenVisitor.cpp`

```cpp
void IRGenVisitor::VisitCompUnit(CompUnit& comp_unit) {
    is_global_ = true;
    PushScope();
    for (auto& d : comp_unit.decls) {
        d->Accept(*this);
    }
    for (auto& f : comp_unit.func_defs) {
        f->Accept(*this);
    }
    if (comp_unit.main_func_def) {
        comp_unit.main_func_def->Accept(*this);
    }
    PopScope();
    is_global_ = false;
}
```

这样全局只推一次栈、只弹一次栈，且 `is_global_` 在整段顶层声明与函数定义期间为 true，符合「顶层为全局、函数体内为局部」的划分。

---

## 四、问题一：ZextInst / TruncInst 是否必须？

**结论**：**需要**。

- **ZextInst**：关系/相等运算在 LLVM 中为 `icmp`，结果为 i1；SysY 要求表达式得到 0/1 的 int。因此在「表达式中的比较」处需要 `zext i1 to i32`，对应 `CreateZext(cmp, module_->GetI32Type())`。若不做 zext，类型不一致且语义不符合 SysY。
- **TruncInst**：向 `char` 变量或数组元素写入时，若当前值为 i32（例如算术结果），需要 `trunc i32 to i8` 再 store，否则会破坏「char 为 i8」的约定。

因此 IR 层已增加 `ZextInst`、`TruncInst` 及 IRBuilder 的 `CreateZext`、`CreateTrunc`；示范中在 `VisitBinaryExp` 里对比较结果做了 zext，Trunc 在实现 LVal 写 char 时按需使用即可。

---

## 五、问题二：作用域管理与「不能复用 ScopeGuard」是否影响优雅？

**现状**：

- **语义分析**：使用 `SymbolTable` + `ScopeGuard`（RAII：构造时 PushScope，析构时 PopScope），作用域与 `Symbol` 绑定，用于类型检查与符号表输出。
- **IR 生成**：需要「名字 → IR 的 Value*（alloca/全局/常量）」的映射，且与语义阶段的数据结构不同（语义是 `Symbol`，IR 是 `Value*`），因此 **IR 侧维护自己的作用域链**（如 `IRGenVisitor` 的 `scopes_` + `PushScope`/`PopScope`）。

**为何不能直接复用 ScopeGuard**：`ScopeGuard` 绑定的是 `SymbolTable&`，只做 `PushScope`/`PopScope`，而 IR 的 scope 存的是 `std::map<std::string, ir::Value*>`，和 `SymbolTable` 的 `Scope::map<std::string, Symbol>` 不是同一张表。若强行复用，要么在语义表里塞进 `Value*`（混合阶段），要么仍要维护两套结构，因此 **IR 独立维护一套 scope 是合理设计**。

**对优雅性的影响与可选改进**：

- 当前在 `VisitBlock` 等处需要手写 `PushScope()` … `PopScope()`；若中间有 return 或异常，要保证 Pop 成对，和语义阶段用 ScopeGuard 的体验略差。
- **可选做法**：为 IR 写一个轻量的 **IRScopeGuard**，仅依赖 `IRGenVisitor*`（或抽象成带 `PushScope`/`PopScope` 接口的对象），在构造时 `PushScope()`，析构时 `PopScope()`。这样「进块即建 guard、出块自动弹栈」，逻辑与语义阶段的 ScopeGuard 一致，不影响语义与 SymbolTable，又能保持 IR 作用域的清晰与异常安全。  
  总结：**不复用 ScopeGuard 本身不影响正确性；若要和语义阶段一样优雅，可单独为 IR 增加一个 RAII 的 scope guard，而不必复用语义的 ScopeGuard。**

---

## 六、问题三：ConstExp 与语义分析中的常量折叠（Symbol.const_values）

**结论**：**应尽量利用语义分析的结果**，在「仅需常量值」的场合直接用 `Symbol`，在「需生成 IR 或无法查表」的场合再访问 AST 的 ConstExp。

**语义阶段已提供的信息**（`include/Symbol.h`）：

```cpp
struct Symbol {
    SymbolType type;
    std::string name;
    int scope_id;
    /** For constants: folded value (ConstExp evaluated to int). Empty for non-const. */
    std::vector<int> const_values;
    /** For 1D array: size (evaluated ConstExp). Empty for scalar. */
    std::optional<int> array_size;
    // ...
};
```

- **常量**：`const_values` 中已存常量折叠结果（标量时一般为 `const_values[0]`）。
- **数组**：`array_size` 为已求值的 ConstExp 数组长度。

**IR 生成时的使用策略**：

1. **数组长度（ConstExp 仅用于求 N）**  
   在生成 `alloca [N x i32]` 或 `global [N x i32]` 时，若能从当前符号表查到该标识符的 `Symbol`（例如在 VarDef/ConstDef 中刚完成 Register），应优先用 **`symbol.array_size.value()`**，避免再对 ConstExp 做一遍求值或重复走 AST。
2. **常量初值（ConstInitVal / 常量列表）**  
   若常量已在语义阶段折叠进 `Symbol.const_values`，生成全局常量或局部常量初始 store 时，可直接用 **`const_values`** 构造 IR 常量（如 `GetInt32Constant(const_values[i])`），不必再 Visit ConstExp 的子树。
3. **仍需访问 ConstExp 的情况**  
   - 语义阶段未把该 ConstExp 结果写入 Symbol（例如某些只读上下文）。  
   - 实现上暂时未在 IR 阶段带「当前符号表」或未传 Symbol 指针，为简单起见先对 ConstExp 再求值一次也可接受，但逻辑上可逐步改为「能查 Symbol 就查 Symbol」。

**实现建议**（逻辑层面）：  
在 `VisitConstDef` / `VisitVarDef`（以及处理数组与常量初值的地方）中，若已有对应 `Symbol`（例如在语义分析后保留「AST 节点 → Symbol*」或按名在符号表查），则：

- 数组大小：用 `symbol.array_size`。
- 常量标量/列表：用 `symbol.const_values` 生成 `GetInt32Constant(...)` 或等价 IR。

这样既复用语义阶段的常量折叠，又避免在 IR 中重复实现一套常量求值，并保持与课程要求的「先语义后代码生成」的分阶段设计一致。

---

## 七、小结

| 问题 | 处理方式 |
|------|----------|
| VisitNumber / VisitBinaryExp / VisitAssignStmt 示范 | 实现为：常量用 Module 常量池、二元运算分算术/比较并比较后 zext、赋值用 is_lval_mode_ 取址+求值+Store。 |
| 是否需要 ZextInst/TruncInst | 需要：比较结果 i1→i32 用 Zext；写 char 用 Trunc。已加 IcmpInst/ZextInst/TruncInst 及 IRBuilder 接口。 |
| 作用域不能复用 ScopeGuard 是否影响优雅 | 不影响正确性；IR 独立 scope 链合理。可选：为 IR 单独做 IRScopeGuard（RAII）以与语义阶段风格一致。 |
| ConstExp 是否利用语义信息 | 建议利用：数组大小用 Symbol.array_size，常量初值用 Symbol.const_values；在未带符号表或未绑 Symbol 时再退化为访问 ConstExp。 |
| VisitCompUnit 递归 | 改为显式遍历 `decls`、`func_defs`、`main_func_def` 并分别 `Accept`，避免对 CompUnit 再次 Accept 导致递归。 |

以上均为**逻辑与设计层面**的修改与结论；头文件依赖、析构顺序等实现细节的修复见 `ir_circular_dependency_fix_20260215.md`。

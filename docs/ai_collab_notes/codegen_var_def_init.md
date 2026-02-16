# 变量/常量定义与初始化 — IR 生成说明

本文档说明 SysY 编译器在 **IR 生成阶段** 对 **变量声明/常量声明** 以及 **变量定义/常量定义** 的处理思路，便于初次接触项目的人理解实现。

---

## 1. 文法与 AST 结构（简要）

- **声明**：`Decl` → `ConstDecl` | `VarDecl`
  - `ConstDecl`：`'const' BType ConstDef { ',' ConstDef } ';'`
  - `VarDecl`：`BType VarDef { ',' VarDef } ';'`
- **常量定义**：`ConstDef` → `Ident [ '[' ConstExp ']' ] '=' ConstInitVal`（必有初值）
- **变量定义**：`VarDef` → `Ident [ '[' ConstExp ']' ]` 或 `Ident [ '[' ConstExp ']' ] '=' InitVal`（初值可选）
- **初值**：
  - `ConstInitVal` → `ConstExp` | `'{' [ ConstExp { ',' ConstExp } ] '}'` | StringConst
  - `InitVal` → `Exp` | `'{' [ Exp { ',' Exp } ] '}'` | StringConst

即：**标量** = 无 `[ ConstExp ]`，**一维数组** = 有一个 `[ ConstExp ]`；初值可以是**单个表达式**或**花括号列表**。

AST 中：
- `ConstDef` / `VarDef` 有 `ident`、`array_size`（`optional<unique_ptr<ConstExp>>`）、以及 `const_init_val` / `init_val`。
- `ConstInitVal` / `InitVal` 用 `std::variant<SingleExp, ExpList, StringVal>` 表示三种形式。

---

## 2. 全局 vs 局部

| 作用域 | 判定方式 | 存储方式 | 初值要求 |
|--------|----------|----------|----------|
| **全局** | `is_global_ == true`（在 CompUnit 下遍历 Decl 时置位） | `module_->CreateGlobalVar` | **必须**是编译期常量（`ir::Constant`），**禁止**生成 Store 等运行时指令 |
| **局部** | `is_global_ == false`（在 Block 内遍历 Decl 时） | `CreateEntryBlockAlloca` 在函数入口块分配 | 初值通过 **Store**（标量）或 **GEP + Store**（数组）在运行时写入 |

因此：
- 全局：只做“创建带常量初值的全局变量/常量”，不做任何指令插入。
- 局部：先 Alloca，再根据是否有初值生成 Store 或一列 GEP+Store。

---

## 3. 常量表达式求值：`GetConstIntVal(Exp*)`

全局初值必须是 **常量**，而 AST 的 `Exp` 在 IR 阶段并不携带“已求值结果”，因此需要在本阶段**即时计算**整型初值。

- **作用**：对给定的 `Exp*` 做**编译期整型求值**，只支持可静态求值的子集。
- **支持**：
  - `Number` → 直接返回 `int_const`
  - `ConstExp` → 对其 `inner` 递归
  - `UnaryExp`：`+` 返回操作数结果，`-` 返回相反数
  - `BinaryExp`：`+` `-` `*` `/` `%`（除/模遇 0 返回 0，避免未定义行为）
- **不支持**（如 LVal、函数调用等）：返回 `0`，不抛错。

这样在生成**全局**标量/数组初值时，可以统一用 `GetConstIntVal` 得到每个元素的整数值，再转成 `ConstantInt` / `ConstantArray`。

---

## 4. 为何不依赖 `VisitInitVal` / `VisitConstInitVal` 回传初值？

`temp_value_` 只能携带**一个** `Value*`，无法表示**一维数组的多个元素**。若用 Visitor 回传初值，要么要改接口（例如传出一组 `Value*`），要么要在 Def 里再遍历一遍 AST。

当前做法：**在 `VisitVarDef` / `VisitConstDef` 里直接解析 `init_val` / `const_init_val` 的 variant**：

- 用 `std::get_if<SingleExp>` 判断是否为**单个表达式**；
- 用 `std::get_if<ExpList>` 判断是否为**花括号列表**。

这样在 Def 内一次遍历即可完成“类型判断 + 求值/生成指令”，逻辑集中、易读。
因此 `VisitInitVal` / `VisitConstInitVal` 仅保留空实现（或简单断言），真正逻辑都在 Def 中内联处理。

---

## 5. 实现结构概览

### 5.1 入口：Decl → Def

- `VisitConstDecl` / `VisitVarDecl`：设置 `current_decl_btype_`（当前声明的 BType），然后对每个 `ConstDef` / `VarDef` 调用 `Accept`。
- `VisitConstDef` / `VisitVarDef`：根据 `is_global_` 分支，再调用下面的“发射”函数。

### 5.2 类型与维度

- **元素类型**：由 `current_decl_btype_` 得到 `GetElemType()` → `i32` 或 `i8`。
- **标量 vs 一维数组**：看 `array_size.has_value() && array_size->get()` 是否为真。
- **数组长度**：对 `array_size->get()`（`ConstExp*`）用 `EvalArraySizeFromConstExp`（内部用 `GetConstIntVal(inner.get())`）得到 `n`，并保证 `n >= 1`。

### 5.3 全局：`EmitGlobalConstDef` / `EmitGlobalVarDef`

- **标量**：
  - 从 ConstInitVal/InitVal 的 SingleExp 用 `GetConstIntVal` 得到整型初值；
  - 用 `BuildConstScalarInit(val)` 得到 `ConstantInt*`（i32 或 i8）；
  - `CreateGlobalVar(ident, elem_type, init, is_constant)`，并 `RegisterVariable`。
- **数组**：
  - 用 `GetArrayType(elem_type, n)` 得到 `[N x T]`；
  - 从 ExpList 解析每个元素并 `GetConstIntVal`（或 VarDef 无初值时填 0），得到 `std::vector<int>`；
  - 用 `BuildConstArrayInit(arr_ty, values)` 得到 `ConstantArray*`；
  - `CreateGlobalVar(ident, GetPointerType(arr_ty), init, ...)`，并 `RegisterVariable`。

**禁止**在全局分支中生成任何 Store / GEP 等指令。

### 5.4 局部：`EmitLocalConstDef` / `EmitLocalVarDef`

- **标量**：
  - `CreateEntryBlockAlloca(elem_type, ident)`；
  - `RegisterVariable(ident, alloca)`；
  - 若有初值：ConstDef 用 `GetConstIntVal` + `BuildConstScalarInit` 再 `Store`；VarDef 用 `exp->Accept(*this)` 得到 `temp_value_` 再 `Store(temp_value_, alloca)`。
- **数组**：
  - `CreateEntryBlockAlloca([N x T], ident)`；
  - `RegisterVariable(ident, alloca)`；
  - 若有初值：对列表中每个元素，ConstDef 用 `GetConstIntVal` 得到值后 `BuildConstScalarInit`；VarDef 用 `Accept` 得到 `temp_value_`；然后对下标 `i` 做 `GEP(alloca, 0, i)`，再 `Store(value, gep)`。

这样**标量 = 一次 Store**，**数组 = 多组 GEP + Store**，符合“局部初值在运行时写入”的约束。

---

## 6. 辅助函数一览

| 函数 | 作用 |
|------|------|
| `GetElemType()` | 根据 `current_decl_btype_` 返回 `i32` 或 `i8` 的 `Type*` |
| `EvalArraySizeFromConstExp(ConstExp*)` | 对 ConstExp 求值得到数组长度，并保证 ≥ 1 |
| `BuildConstScalarInit(int val)` | 构造 i32/i8 的 `ConstantInt*`（用于全局或局部常量初值） |
| `BuildConstArrayInit(ArrayType*, vector<int>)` | 构造 `ConstantArray*`（仅用于全局数组初值） |
| `EmitGlobalConstDef` / `EmitLocalConstDef` | 常量定义的全局/局部分支 |
| `EmitGlobalVarDef` / `EmitLocalVarDef` | 变量定义的全局/局部分支 |

`VisitConstDef` / `VisitVarDef` 只负责：取 `GetElemType()`，然后根据 `is_global_` 调用对应的 `Emit*`，保持入口简洁。

---

## 7. 与后续阶段的关系

- **IR 打印**：若后端要输出 `.ll`，需在打印 GlobalVar 时根据 `GetInitializer()` 是否为 null 输出 `zeroinitializer` 或对应常量/常量数组。
- **MIPS/其他后端**：当前在内存中已形成完整的 Value 图（GlobalVar、Alloca、Store、GEP 等），后续可直接基于此图做寄存器分配与指令选择。

以上即为“变量/常量定义与初始化”在 IR 生成中的实现思路与模块划分；若你需要在某一步（例如全局字符串、多维数组）扩展，可在此结构上按同一规则增加分支或辅助函数。

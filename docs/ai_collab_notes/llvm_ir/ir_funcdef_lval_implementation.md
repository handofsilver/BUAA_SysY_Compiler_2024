# IR 代码生成：VisitFuncDef、VisitLVal 与相关实现说明

本文档面向未读过源码的读者，说明「函数定义」「左值访问」及与之相关的 Entry Block Alloca、作用域、GEP 区分等实现思路，便于理解与后续维护。

---

## 1. 与文法的一致性

### 1.1 文法摘录

- **函数定义**：`FuncDef → FuncType Ident '(' [FuncFParams] ')' Block`
- **函数类型**：`FuncType → 'void' | 'int' | 'char'`
- **形参**：`FuncFParam → BType Ident ['[' ']']`，其中 `BType → 'int' | 'char'`

即：返回类型可以是 void / int / char；形参可以是 int/char 的标量或数组（`Ident` 后可选 `[]`）。

### 1.2 类型映射（与文法一一对应）

| 文法 | IR 类型 |
|------|--------|
| 返回 void | `void` |
| 返回 int | `i32` |
| 返回 char | `i8` |
| 形参 int 标量 | `i32` |
| 形参 char 标量 | `i8` |
| 形参 int 数组 | `i32*`（传首地址） |
| 形参 char 数组 | `i8*`（传首地址） |

实现中通过辅助函数 `BTypeToReturnType`、`BTypeToParamType`、`BTypeToAllocaType` 将 `BType` 与「是否数组」映射到上述 IR 类型，保证与文法一致，**未忽略 char 类型**。

---

## 2. 整体流程概览

- **VisitFuncDef**：创建函数骨架（返回类型 + 形参类型）、建 entry 块、为标量形参在 entry 内做 alloca + store、访问函数体、必要时补 `ret void`。
- **VisitLVal**：根据符号表取基地址；无下标时按「要地址」还是「要值」做 load 或直接传地址；有下标时按「真实数组」与「指针形参」两种类型生成不同的 GEP，再按同样规则处理地址/值。

下面分块说明各部分的「为什么这样设计」和「怎么做」。

---

## 3. 函数定义（VisitFuncDef）实现思路

### 3.1 目标

为每个 `FuncDef` 生成一个 `define`：正确的返回类型、正确的形参类型与名字、entry 块内集中做形参的「落地」（标量 alloca + store，数组直接用形参指针），然后生成函数体，并在 void 且未终止时补 `ret void`。

### 3.2 步骤拆解

1. **确定返回类型与形参类型**  
   用 `FuncType`（void/int/char）得到 `return_type`；对每个 `FuncFParam` 用其 `BType` 与 `is_array` 得到 IR 形参类型（i32/i8 或 i32*/i8*）。这样与文法完全一致，包括 char。

2. **创建函数与参数 Value**  
   调用 `Module::CreateFunction(ident, return_type, param_types)`，内部会创建 `FunctionType`、`Function` 以及每个形参对应的 `Argument`（Value），并加入 Module。随后用 AST 中的形参名字设置每个 `Argument` 的 name。

3. **进入函数作用域**  
   使用 **IRScopeGuard**：在 VisitFuncDef 开头创建 `IRScopeGuard scope_guard(*this)`，构造时 `PushScope()`，析构时 `PopScope()`。这样无论正常 return 还是异常离开，作用域都会正确弹出，**无需手写 PopScope**，也避免漏弹。

4. **创建 entry 块并设插入点**  
   `CreateBasicBlock("entry")` 并 `SetInsertPoint(entry)`。后续所有「在 entry 里插指令」都发生在这个插入点之后，因此 alloca 会集中在 entry 块前部。

5. **形参处理（关键）**  
   - **数组形参**（`int a[]` / `char s[]`）：不在栈上再分配，形参本身已是 `i32*` / `i8*`。直接把该 `Argument` 的 Value 登记到符号表，后续对 `a[i]` / `s[i]` 的访问会走「指针形参」的 GEP 路径。  
   - **标量形参**（`int a` / `char c`）：在 entry 块内为该形参分配一槽（alloca），把传入的实参值 store 进去，再把 **alloca 得到的地址** 登记到符号表。这样函数体内对形参名的使用统一为「对某地址 load/store」，与局部变量一致。

6. **访问函数体**  
   `func_def.block->Accept(*this)`。Block 内部会再通过 IRScopeGuard 管理自己的作用域。

7. **void 返回与终止检查**  
   若返回类型为 void 且当前块尚未以 `ret`/`br` 结束（`!IsBlockTerminated()`），则补一条 `ret void`，满足「void 函数每条路径都要有 return」的语义。

### 3.3 为何不需要 PrependInstruction

Entry 块的 alloca 只需要「都出现在 entry 块里、且在函数体其他指令之前」。实现上：

- 在 VisitFuncDef 里，**先**创建 entry 并设为插入点，**再**按顺序处理每个形参（每处理一个标量形参就调一次 `CreateEntryBlockAlloca`）。
- `CreateEntryBlockAlloca` 是在 **当前函数的 entry 块** 上 `AddInstruction`（在块尾追加一条 alloca），并不移动 builder 的插入点。
- 因为此时还没有为函数体生成任何指令，所以这些 alloca 自然都排在 entry 块的前面，顺序与形参顺序一致。

因此不需要「在块首插入」的 `PrependInstruction`，**只用 `AddInstruction` 即可**。若将来在块中部为局部变量插入 alloca，仍可调用同一套逻辑，alloca 会接在现有 entry 指令之后，仍满足「入口块内集中分配」的约定。当前实现中已移除 `PrependInstruction`，与 `AddInstruction` 的职责区分是：前者是「在块首插入」，后者是「在块尾追加」；这里只用到后者。

---

## 4. Entry Block Alloca 辅助方法（CreateEntryBlockAlloca）

### 4.1 作用

在**当前函数**的 **entry 块** 末尾追加一条 alloca，分配给定类型的一格栈槽，返回该 alloca 指令（其类型为「指向该类型的指针」）。用于标量形参和（若将来实现局部变量）局部变量，保证分配都发生在 entry，便于分析和优化。

### 4.2 实现要点

- 不移动 builder 的插入点：只对 `current_function_->GetBlocks().front()` 调用 `AddInstruction`，把新 alloca 挂到 entry 块。
- 传入的 `type` 是「要分配的类型」（如 i32、i8）；内部用 `GetPointerType(type)` 作为 alloca 指令的**结果类型**，这样后续 `CreateStore(arg_val, alloca_inst)` 类型正确。

---

## 5. 左值访问（VisitLVal）实现思路

### 5.1 语义与两种使用方式

- **文法**：`LVal → Ident | Ident '[' Exp ']'`  
  即「变量」或「数组元素」。

- **两种使用方式**（由 `is_lval_mode_` 区分）：  
  - **要地址**（赋值左侧、getint/getchar 左侧等）：只求地址，不 load，把地址放进 `temp_value_`，由上层做 store。  
  - **要值**（表达式中的变量/数组元素）：先求地址，再 load，把值放进 `temp_value_`。

### 5.2 步骤拆解

1. **查符号表**  
   用 `lval.ident` 在作用域链中查找，得到基地址 `value`（可能是 alloca、全局变量、或形参指针）。

2. **无下标**  
   - 若 `is_lval_mode_`：直接 `temp_value_ = value`（把地址交给上层）。  
   - 否则：对 `value` 做一次 load，`temp_value_` 为 load 结果。

3. **有下标**  
   - 先访问下标表达式，得到 `index_val`（在 `temp_value_`）。  
   - 用 `dynamic_cast<PointerType*>(value->GetType())` 得到基地址的指针类型，再取 `pointee`。  
   - **区分两种基地址类型**：  
     - **真实数组**：`pointee` 为 `ArrayType`（如 `[N x i32]` / `[N x i8]`）。在 LLVM 中这类指针用「先选元素 0，再按下标选元素」的方式做 GEP，即 `GEP(base, 0, index)`，得到元素地址。元素类型为 `ArrayType::GetElementType()`（i32 或 i8），故 GEP 结果类型为 `GetPointerType(elem_ty)`（i32* 或 i8*）。  
     - **指针形参**：`pointee` 为标量类型（如 i32、i8），即「直接指向第一个元素」的指针。用 `GEP(base, index)` 即可，结果类型同样为 `GetPointerType(pointee)`（i32* 或 i8*）。  
   - 这样 **int 数组与 char 数组** 都能得到正确的元素类型（i32* 或 i8*），与文法一致。  
   - 最后根据 `is_lval_mode_`：要地址则 `temp_value_ = gep`；要值则对 `gep` 再 load，结果写入 `temp_value_`。

### 5.3 为何要区分「真实数组」与「指针形参」

- **真实数组**（局部或全局）：类型为 `[N x T]*`，必须用两个索引的 GEP（先 0 再 index）才能正确计算元素地址。  
- **指针形参**：类型已是 `T*`，只需一个索引的 GEP(base, index)。  

用 `dynamic_cast` 区分 `ArrayType` 与标量类型，可以统一处理 int/char、局部/全局/形参，并保证生成的 GEP 与 LLVM 规范一致。

---

## 6. 作用域管理：IRScopeGuard 与 PushScope/PopScope

### 6.1 设计选择

- **PushScope() / PopScope()**：由调用方成对调用，若中间有 return 或异常，容易漏掉 PopScope，导致作用域栈错乱。  
- **IRScopeGuard**：构造时调用 `PushScope()`，析构时（包括正常离开、return、异常）自动调用 `PopScope()`，与语义阶段的 ScopeGuard 思路一致，**异常安全且不易出错**。

因此实现中**统一使用 IRScopeGuard** 管理「进入某作用域」的边界，保留 `PushScope`/`PopScope` 为 IRScopeGuard 内部使用，不在 VisitFuncDef、VisitBlock、VisitForStmt 等处手写 `PopScope()`。

### 6.2 使用位置

- **Translate**：最外层一个 IRScopeGuard，为整个编译单元推一层作用域。  
- **VisitFuncDef / VisitMainFuncDef**：进入函数时一个 IRScopeGuard，覆盖整个函数体。  
- **VisitBlock**：进入块时一个 IRScopeGuard，覆盖该块内所有 BlockItem。  
- **VisitForStmt**：进入 for 时一个 IRScopeGuard，覆盖 init/cond/body/step 的变量作用域。

这样每个「语法上的作用域」都对应一个 guard，无需记忆在哪里 PopScope。

---

## 7. 相关数据结构与模块

- **Argument**：表示函数形参的 Value，由 Function 持有；CreateFunction 时按形参类型创建并加入 Function。  
- **Module::CreateFunction**：根据名字、返回类型、形参类型创建 FunctionType 与 Function，并为每个形参创建 Argument，保证类型与文法一致（含 char 与数组指针）。  
- **ArrayType**：用于表示 `[N x T]`，在 VisitLVal 中通过 `dynamic_cast<ArrayType*>(pointee)` 区分「真实数组」与「指针形参」，并正确取元素类型（i32 或 i8）以生成 GEP 结果类型。

---

## 8. 小结

- **文法**：返回类型与形参类型严格按 FuncType / BType 与是否数组映射到 void、i32、i8、i32*、i8*，**不忽略 char**。  
- **Entry alloca**：通过「先建 entry、再按顺序为标量形参 AddInstruction(alloca)」实现，无需 PrependInstruction；CreateEntryBlockAlloca 只负责在 entry 块尾追加一条 alloca。  
- **作用域**：统一用 IRScopeGuard，避免手写 PopScope 带来的错误与不一致。  
- **VisitLVal**：无下标时按 is_lval_mode_ 给地址或 load；有下标时用 pointee 类型区分真实数组（GEP(base,0,index)）与指针形参（GEP(base,index)），并统一用元素类型得到正确的 i32* / i8*，与 int/char 语义一致。

按上述思路，未读源码的人也可以理解「函数定义与左值如何从文法映射到 IR」以及「各步为何这样实现」。

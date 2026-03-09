# LLVM IR 阶段代码理解路线图

本文档在正式撰写《代码生成（LLVM IR）设计文档》之前，用于**系统性拆解与理解**本阶段代码。因本阶段采用大量 vibe coding 生成代码，可通过以下维度和阅读顺序逐步建立对代码的掌控感，并自然形成设计文档的章节结构。

---

## 一、理解维度总览

| 维度 | 关注问题 | 建议阅读顺序 | 对应设计文档章节 |
|------|----------|--------------|------------------|
| **1. 入口与数据流** | 谁调 IR？输入输出是什么？ | 最先 | 概述、输入/输出 |
| **2. 内存与所有权** | 谁拥有 Module/Function/Instruction？谁只是引用？ | 早 | 总体架构、关键数据结构 |
| **3. IR 类层次** | Value / User / Instruction 是什么？有哪些指令类？ | 早 | IR 中间表示结构 |
| **4. 类型与常量** | 类型从哪来？常量池如何工作？ | 中 | 类型系统、常量 |
| **5. Visitor 与分发** | 哪个 AST 节点由哪个 Visit 处理？ | 中 | 遍历策略、与文法对应 |
| **6. 插入点与 Builder** | 指令插到哪？SSA 名如何分配？ | 中 | IRBuilder、插入点 |
| **7. 声明与作用域** | 全局/局部/形参如何生成 alloca/global？ | 中 | 声明生成、IRDeclEmitter |
| **8. 表达式求值** | 表达式如何得到 Value*？左值/右值如何区分？ | 中 | 表达式翻译 |
| **9. 控制流** | if/for/break/continue 如何变成块与 br？ | 后 | 控制流、短路求值 |
| **10. 输出** | IR 如何打印成 .ll 文本？ | 后 | 打印与输出 |

下面按上述顺序展开每一维度的「要回答的问题」和「建议看的代码位置」。

---

## 二、各维度拆解

### 1. 入口与数据流

**要回答的问题：**

- 主控（Driver）在何时、如何调用 IR 生成？
- 输入是什么（AST？符号表？）？输出是什么（`ir::Module`？文件？）？

**建议看的代码：**

- `include/Driver.h`：`CompilerResult` 中的 `module`，`RunCompiler` 的语义。
- `src/Driver.cpp`：在语义分析之后如何调用 `IRGenVisitor::Translate(comp_unit)`，以及 `result.module` 如何被用于写 llvm_ir.txt。
- `src/irgen/IRGenVisitor.cpp`：`IRGenVisitor::Translate(CompUnit&)`（构造 visitor → `comp_unit.Accept(*this)` → `return std::move(module_)`）。

**小结：**
输入 = 语义分析后的 AST 根节点 `CompUnit`（符号表已构建，AST 可读）；输出 = `std::unique_ptr<ir::Module>`，主控将其打印到 llvm_ir.txt。**不在此阶段写文件**，只构建内存中的 IR。

---

### 2. 内存与所有权

**要回答的问题：**

- `Module`、`Function`、`BasicBlock`、`Instruction`、`GlobalVar`、`Constant` 分别由谁持有？
- 哪些地方用 `unique_ptr`，哪些地方用裸指针 `Value*`？为什么？

**建议看的代码：**

- `include/ir/Module.h`：`vector<unique_ptr<GlobalVar>>`、`vector<unique_ptr<Function>>`，以及常量池的 ownership。
- `include/ir/Function.h`：对 BasicBlock 的 ownership。
- `include/ir/BasicBlock.h` / `BasicBlock.cpp`：对 Instruction 的 ownership（`AddInstruction` / `AddInstructionAtFront`）。
- `include/ir/Value.h`、`include/ir/User.h`：`Use` 与 `use_list_` 只记录引用关系，不拥有 Value。
- `include/irgen/IRGenVisitor.h`：`module_`、`builder_` 为 `unique_ptr`；`ctx_` 里的是非拥有指针（如 `current_function`、`builder`）。

**所有权规则（简要）：**

- **Module** 拥有：GlobalVar、Function、FunctionType、常量池（ConstantInt/ConstantArray 等）。
- **Function** 拥有：BasicBlock、Argument。
- **BasicBlock** 拥有：Instruction。
- **引用关系**：Instruction 的 operands、BranchInst 的目标块、IRBuilder 的 insert_point_ 等一律用裸指针，避免循环引用与 shared_ptr 泛滥。

设计文档中建议画一张「所有权链」图：Module → Function → BasicBlock → Instruction，以及 GlobalVar/Constant 挂在 Module 下。

---

### 3. IR 类层次

**要回答的问题：**

- `Value`、`User`、`Instruction` 的继承关系？
- 有哪些具体指令类（AllocaInst、LoadInst、StoreInst、BinaryInst、BranchInst、CallInst、ReturnInst、GetElementPtrInst、IcmpInst、ZextInst、TruncInst）？各自对应 LLVM IR 的哪条指令？

**建议看的代码：**

- `include/ir/Value.h`：基类，name、type、use_list_，`PrintAsOperand`、`ReplaceAllUsesWith`。
- `include/ir/User.h`：继承 Value，增加 `operands_`（vector of Use）。
- `include/ir/Instruction.h`：继承 User，增加 `parent_`（BasicBlock*）；所有具体指令类的声明。
- `include/ir/BasicBlock.h`：BasicBlock 继承 Value（可作为 br 的目标）。
- `include/ir/Constant.h`、`include/ir/GlobalVar.h`、`include/ir/Argument.h`：Constant/GlobalVar/Argument 也是 Value（或 User），但不属于 Instruction。

**与课程指令的对应：**
可结合 `docs/ai_collab_notes/llvm_ir_instructions_summary.md` 做一张表：SysY 文法/AST 概念 → 本项目的 IR 类 → LLVM IR 文本形式。

---

### 4. 类型与常量

**要回答的问题：**

- 类型（i32、i8、i1、ptr、array、function）从哪里来？是否单例？
- 常量（ConstantInt、ConstantArray）由谁创建、谁持有？Module 的常量池如何复用？

**建议看的代码：**

- `include/ir/TypeManager.h`、`src/ir/TypeManager.cpp`：`GetI32Type()`、`GetI8Type()`、`GetI1Type()`、`GetPointerType()`、`GetArrayType()` 等；TypeManager 单例。
- `include/ir/Type.h`：IntegerType、PointerType、ArrayType、FunctionType、VoidType 等。
- `include/ir/Module.h`：`GetInt32Constant`、`GetInt8Constant`、`CreateConstantArray`；内部 `constants_`、`const_i32_cache_` 等。
- `include/ir/Constant.h`、`src/ir/Constant.cpp`：ConstantInt、ConstantArray 等。

设计文档中可单独小节：类型系统（TypeManager + Type 子类）、常量池（Module 内缓存与创建接口）。

---

### 5. Visitor 与分发

**要回答的问题：**

- 从 `CompUnit` 开始，各 AST 节点分别由哪个 `VisitXxx` 处理？
- Visitor 的实现分布在哪些 .cpp 文件？（便于按「声明 / 语句 / 表达式」分块阅读）

**建议看的代码：**

- `include/irgen/IRGenVisitor.h`：所有 `VisitXxx` 的声明，以及 `temp_value_`、`is_lval_mode_`、`break_targets_` 等状态。
- `src/irgen/IRGenVisitor.cpp`：VisitCompUnit、VisitConstDecl、VisitVarDecl、VisitConstDef、VisitVarDef、VisitFuncDef、VisitMainFuncDef、VisitFuncFParam 等（编译单元、声明、函数入口）。
- `src/irgen/IRGenVisitorStmt.cpp`：VisitBlock、VisitAssignStmt、VisitExpStmt、VisitIfStmt、VisitForStmt、VisitBreakStmt、VisitContinueStmt、VisitReturnStmt、VisitGetintStmt、VisitGetcharStmt、VisitPrintfStmt。
- `src/irgen/IRGenVisitorExpr.cpp`：VisitLVal、VisitNumber、VisitCharacter、VisitBinaryExp、VisitUnaryExp、VisitFuncCall、VisitFuncRParams、VisitConstExp。

**建议：**
做一张「AST 节点 → VisitXxx → 所在文件」表，并标注哪些节点会「设置 `temp_value_`」、哪些会「创建 BasicBlock / 修改插入点」。这样读某个语法现象时能快速定位到文件和函数。

---

### 6. 插入点与 Builder

**要回答的问题：**

- 当前指令插入到哪个 BasicBlock？谁在何时调用 `SetInsertPoint`？
- SSA 名字（%0, %1, ...）如何分配？是否与 BasicBlock 或 Function 绑定？

**建议看的代码：**

- `include/ir/IRBuilder.h`：`SetInsertPoint`、`GetInsertBlock`、`GetNextSSAName`、`ResetSSACounter`；各 `Create*` 方法（CreateAlloca、CreateLoad、CreateStore、CreateBinary、CreateBr、CreateCondBr、CreateRet、CreateCall、CreateGEP、CreateIcmp、CreateZext、CreateTrunc）。
- `include/irgen/SSANameAllocator.h`：SSA 名字的生成与重置（按函数重置，使形参为 %0,%1,...，局部为后续编号）。
- 在 IRGenVisitor 中搜索 `SetInsertPoint`、`CreateBasicBlock`：理解进入函数时设 entry、进入 if/for 时切换到 then/body/cond/end 等块。

设计文档中可画「插入点状态机」：进入函数 → entry；进入 if → then/else/merge；进入 for → cond/body/step/end；break/continue 只改 br 目标，不改变当前插入点（在目标块里不再插指令时，插入点会留在上一块）。

---

### 7. 声明与作用域

**要回答的问题：**

- 全局常量/全局变量与局部常量/局部变量在 IR 上的区别？（global/constant vs alloca）
- 形参如何变成 alloca + 入口 store？局部变量/常量的 alloca 放在哪（entry 块首）？
- 名字到 Value* 的映射存在哪里？（IRGenContext 的 variable 注册）

**建议看的代码：**

- `include/irgen/IRGenContext.h`：`current_function`、`builder`、`module`、类型管理器引用，以及变量注册（如 `RegisterVariable`、查找接口）。
- `include/irgen/IRDeclEmitter.h`、`src/irgen/IRDeclEmitter.cpp`：`EmitGlobal`、`EmitLocalAlloca`、`EmitGlobalStringLiteral`、数组初始化 `BuildConstArrayInit` 等；与 IRGenContext 的配合。
- `src/irgen/IRGenVisitor.cpp`：`EmitGlobalConstDef`、`EmitLocalConstDef`、`EmitGlobalVarDef`、`EmitLocalVarDef`；何时 `is_global_ == true`，何时调用 `decl_emitter_`。
- `IRDeclEmitter::CreateEntryBlockAlloca`：alloca 插在 entry 块**最前面**（`AddInstructionAtFront`），保证同一函数内所有局部/形参的 alloca 在入口集中。

设计文档中可区分：模块级声明（CompUnit 下 Decl）→ global/constant；函数内声明（Block 内 Decl）→ alloca；形参 → entry 内 alloca + 入口 store。

---

### 8. 表达式求值

**要回答的问题：**

- 表达式的结果如何传递？`temp_value_` 的读写约定是什么？
- 左值（LVal）在「赋值目标」与「作为值使用」时有何不同？（is_lval_mode_、地址 vs load）
- 二元运算、单目运算、函数调用的操作数如何求值？类型提升（PromoteToI32）与转换（ConvertToTargetType）在何处做？

**建议看的代码：**

- `src/irgen/IRGenVisitorExpr.cpp`：VisitLVal（根据 is_lval_mode_ 决定写回地址还是 load 后设 temp_value_）、VisitNumber/VisitCharacter、VisitBinaryExp、VisitUnaryExp、VisitFuncCall、VisitFuncRParams、VisitConstExp。
- `src/irgen/IRGenVisitor.cpp`：PromoteToI32、ConvertToTargetType、CoerceToI1；EmitShortCircuitAND、EmitShortCircuitOR（短路与/或如何生成多个块并合并结果到 temp_value_）。
- 赋值语句（VisitAssignStmt）：如何设 is_lval_mode_，让 LVal 返回地址再 store。

设计文档中可写：表达式求值约定（子节点 Accept 后从 temp_value_ 取结果）、左值/右值模式、类型提升与转换规则（参考 type_conversion.md 若有）。

---

### 9. 控制流

**要回答的问题：**

- if（无 else / 有 else）如何生成 cond / then / else / merge 块？条件如何变为 i1？块之间如何用 br 连接？
- for 的 cond、body、step、end 四块如何创建与连接？缺省条件/缺省步进时的语义？
- break/continue 如何找到目标块？（break_targets_、continue_targets_ 栈）
- 短路求值（&& / ||）如何生成多个块和 phi？（当前实现若用 alloca + store 合并结果，可说明「未用 phi，用内存槽」）

**建议看的代码：**

- `src/irgen/IRGenVisitorStmt.cpp`：VisitIfStmt、VisitForStmt、VisitBreakStmt、VisitContinueStmt。
- `src/irgen/IRGenVisitor.cpp`：CreateBasicBlock、IsBlockTerminated、CoerceToI1、EmitShortCircuitAND、EmitShortCircuitOR。

设计文档中建议画 if/for 的块图（带 br 的箭头），并说明 break_targets_/continue_targets_ 的压栈与弹栈时机。

---

### 10. 输出

**要回答的问题：**

- Module 的 Print 如何被调用？内部如何遍历 globals、functions？
- 每条 Instruction 的 Print 如何生成一行 LLVM IR 文本？BasicBlock 的 label 与 SSA 名字如何确定？

**建议看的代码：**

- `include/ir/Module.h`：`void Print(std::ostream& os) const;`
- `src/ir/Module.cpp`：Print 实现（先全局变量、再 declare、再 define；函数内块与指令顺序）。
- `include/ir/IRPrintContext.h`、`src/ir/IRPrintContext.cpp`：打印时 SSA 名与块 label 的分配（若与构建时不同，可在这里做统一编号）。
- 各 Instruction 子类的 `Print` 实现（如 `Instruction.cpp` 或对应 cpp）：如何输出 `add i32 %0, %1` 等。

设计文档中可简要描述：输出阶段是单遍遍历 Module → Function → BasicBlock → Instruction，各层 Print 委托子节点并依赖 IRPrintContext 提供名字/标签。

---

## 三、建议的阅读顺序（按时间）

1. **第 1 步（约 30 分钟）**：入口与数据流 + 内存与所有权。弄清「谁调谁、谁拥有谁」。
2. **第 2 步（约 30 分钟）**：IR 类层次 + 类型与常量。弄清「有哪些 IR 节点、类型和常量从哪来」。
3. **第 3 步（约 45 分钟）**：Visitor 与分发 + 插入点与 Builder。弄清「AST 到 Visit 的映射、指令插到哪」。
4. **第 4 步（约 45 分钟）**：声明与作用域 + 表达式求值。选一条路径跟到底，例如「全局变量声明」或「赋值语句中的 LVal + Exp」。
5. **第 5 步（约 30 分钟）**：控制流。选一个 if 和一个 for，从 Visit 到 CreateBasicBlock/CreateCondBr/CreateBr 跟一遍。
6. **第 6 步（约 20 分钟）**：输出。从 Module::Print 到一条 Instruction::Print 走一遍。

---

## 四、与设计文档章节的对应

正式写《代码生成（LLVM IR）设计文档》时，可沿用上述维度作为章节骨架，例如：

1. **概述**：模块职责、输入/输出、核心策略（对应维度 1）。
2. **总体架构**：模块划分、所有权与数据流图（维度 2、3）。
3. **IR 中间表示结构**：类层次、指令种类、与 LLVM IR 文本的对应（维度 3）。
4. **类型系统与常量**：TypeManager、Type 子类、Module 常量池（维度 4）。
5. **遍历策略**：Visitor 分发、文件划分、AST 节点与 Visit 的对应表（维度 5）。
6. **插入点与 IRBuilder**：SetInsertPoint、SSA 名、Create* 封装（维度 6）。
7. **声明与变量**：全局/局部/形参、IRDeclEmitter、变量注册（维度 7）。
8. **表达式翻译**：temp_value_、左值/右值、类型提升与转换、短路求值（维度 8）。
9. **控制流**：if/for/break/continue、块结构、br 连接（维度 9）。
10. **IR 输出**：Module::Print、IRPrintContext、指令打印（维度 10）。

每个章节内可再按「要回答的问题 → 关键代码位置 → 小结/表/图」组织，便于日后维护和新人上手。

---

## 五、小结

- 本阶段代码量大、生成逻辑分散在多个 Visitor 与 IR 类中，**按维度拆解**比一次性通读更易建立清晰心智模型。
- **先弄清入口、所有权和 IR 层次**，再按「声明 → 表达式 → 语句 → 控制流 → 输出」的顺序逐个击破，最后用「AST 节点 → IR」对应表与块图把整条链路串起来。
- 这份路线图既是指南，也可直接作为《代码生成（LLVM IR）设计文档》的提纲，按章节填充即可得到完整设计文档。

# 重构：Visitor 文件合并与辅助函数清理

**日期**：2026-02-28
**分支**：`llvm_ir`
**前置**：`IRDeclEmitter` 抽取已完成（见 `refactor_extract_IRDeclEmitter_20260228.md`）

---

## 1. 动机

`IRDeclEmitter` 抽取后，`IRGenVisitorDecl.cpp`（67 行）和 `IRGenVisitorFunc.cpp`（72 行）各自只剩 2-3 个极短的分发/定义函数。机械地拆分成 5 个文件反而增加了阅读成本（"用物理拆分掩盖逻辑高耦合"的遗留问题）。同时，`GetPointeeType` 作为一个不访问任何类成员的纯工具函数，不应该出现在类的私有方法列表中。

## 2. 改动内容

### 2.1 `GetPointeeType` 从类成员变为自由内联函数

**问题**：`IRGenVisitor::GetPointeeType(ir::Value*)` 是一个纯函数——只做 `dynamic_cast<PointerType*>` + null 保护，不访问任何 `this` 成员。它不属于 Visitor 的类接口。

**方案**：从类定义中移除，改为 `IRGenVisitor.h` 底部的全局 `inline` 自由函数。所有 `.cpp` 文件（包括 Stmt/Expr）都 include 此头文件，因此调用点 `GetPointeeType(ptr)` 无需任何修改。

**注意**：这与 `PointerType::GetPointeeType()` 并非重复——后者是类型类的成员方法，前者是对 `Value*` 做 null 安全的 pointee 提取（先 `dynamic_cast` 再调用），是高频使用的便利封装。

### 2.2 文件合并：5 → 3

| 操作 | 结果 |
|------|------|
| `IRGenVisitorDecl.cpp` 内容合并入 `IRGenVisitor.cpp` | 已删除 |
| `IRGenVisitorFunc.cpp` 内容合并入 `IRGenVisitor.cpp` | 已删除 |

合并后的文件结构：

| 文件 | 职责 | 约行数 |
|------|------|--------|
| `IRGenVisitor.cpp` | 核心（构造/控制流/类型转换/声明生成）+ CompUnit/Decl 分发 + Func 定义 | ~480 |
| `IRGenVisitorStmt.cpp` | 语句（Block/Assign/If/For/Return/Printf/IO） | ~275 |
| `IRGenVisitorExpr.cpp` | 表达式（LVal/Number/Binary/Unary/FuncCall） | ~209 |

合并后添加了 `#include "irgen/TypeMapping.h"`（原 Func.cpp 所需）。

## 3. 文件清单

**删除文件**：
- `src/IRGenVisitorDecl.cpp`
- `src/IRGenVisitorFunc.cpp`

**修改文件**：
- `include/IRGenVisitor.h` — 移除 `GetPointeeType` 成员声明；在类定义后添加同名全局 inline 函数
- `src/IRGenVisitor.cpp` — 移除 `GetPointeeType` 成员实现；合并 Decl/Func 内容；添加 `TypeMapping.h` include；更新文件头注释

## 4. 目录结构现状与改进建议

当前 `include/` 根目录混杂了前端文件（Lexer/Parser/AST）和代码生成文件（IRGenVisitor/IRGenContext/IRBuilder）。理想结构应为：

```
include/
├── frontend/          ← Lexer.h, Parser.h, AST.h, Token.h, ...
├── ir/                ← Value.h, Instruction.h, Module.h, IRBuilder.h, ...
├── irgen/             ← IRGenVisitor.h, IRGenContext.h, IRDeclEmitter.h, ...
└── semantic/          ← SemanticAnalyzer.h, Symbol.h, SymbolTable.h
```

**当前不做此变更**——涉及 20+ 文件的 include 路径修改，高风险低收益。记录在此作为 MIPS 阶段前的可选整理目标。

## 5. 关于后续模块化的结论

经过 `IRDeclEmitter` 抽取 + 文件合并后，`IRGenVisitor` 的职责已经足够清晰：

- **核心辅助**（~150 行）：BasicBlock 创建、短路求值、类型转换
- **声明生成**（~140 行）：全局/局部、const/var 的 AST 遍历 + Emitter 委托
- **CompUnit/Decl/Func 分发**（~90 行）：纯粹的 Accept 分发
- **Stmt**（~275 行，独立文件）：控制流语句，已经足够简洁
- **Expr**（~209 行，独立文件）：表达式求值，结构清晰

### 不再计划抽取 `IRControlFlowBuilder`

原 Gemini 路线图的第二步建议抽取控制流生成器。经评估，**收益不足**：
1. `VisitIfStmt`（25 行）和 `VisitForStmt`（45 行）已经结构清晰
2. 控制流与 AST 遍历天然交织（`SetInsertPoint` 之间穿插 `Accept`），抽离需要 lambda 回调，反增复杂度
3. 课程编译器应把精力留给 MIPS 后端

**IR 生成阶段的架构重构到此完成，可以转入 MIPS 后端开发。**

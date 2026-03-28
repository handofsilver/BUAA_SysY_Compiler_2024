<div align="center">

# BUAA Compiler - SysY (C++ Refactor)

[简体中文](README.md) | [English](README_en.md)

</div>

> 北航计算机学院编译原理课程实验 - SysY 语言编译器（C++17 重构版）

本仓库记录了该编译器从 Java 版本迁移至 C++ 版本的重构过程。项目按实验阶段管理分支，当前已完成**全部阶段**：**词法分析**、**语法分析**、**语义分析**、**中间代码生成（含 Mem2Reg 优化）**、**MIPS 目标代码生成**与**代码优化**（IR 级常量折叠/DCE + MIPS 级窥孔优化/图染色寄存器分配）。主控读取 `testfile.txt`，经完整编译流水线后输出 `llvm_ir.txt`（LLVM IR）和 `mips.txt`（MIPS 汇编，可在 MARS 4.5 上运行），或 `error.txt`（有错误时）。

------

## 📖 项目背景

本项目是北京航空航天大学计算机学院编译原理课程实验（2024秋季）的 **C++ 重构版本**。原 Java 实现版本目前仅在本地作为逻辑参考。

**重构契机**：

2026年初，在梳理考研复试的项目经历时，重新审视了本科阶段的工程实践。编译原理课设作为本科期间对**代码掌控力**与**系统架构思维**最具挑战性的训练，其最终形态若仅停留于“勉强运行”，实为核心竞争力的缺失与遗憾。

鉴于完善项目不可避免需要复盘原有逻辑，为提高有限时间内的**投入产出比 (ROI)**，决定摒弃在原 Java 代码上修补的方案，转而采用 C++ 重构。此举既能补全架构设计，又能顺势完成对 C++ 语言特性的掌握——着眼于长远的技术储备，这无疑是一笔**高回报的时间投资**。

- **预期净投入时间**：2周。

### 🎯 重构目标与意义

1. **系统完整性补全**

   原 Java 版本因彼时时间仓促，代码生成仅止步于 LLVM IR，且存在大量因赶工导致的架构耦合。本次重构旨在彻底完成 **MIPS 汇编生成**及**代码优化**（IR 级 + MIPS 级），并重构不合理的模块交互逻辑，打造一个架构清晰的完整编译器。

2. **Modern C++ 深度实践**

   本项目将作为 C++ 技能树的实战演练场。重点聚焦于面向对象设计 (Object-Oriented Programming, OOP) 的优雅实现，以及智能指针 (Smart Pointers)、STL 高级特性与右值引用的实际应用，确保代码风格符合现代工程标准。

3. **工程能力复健**

   在考研长周期的理论复习后，通过为期两周的高强度重构冲刺，快速恢复代码手感，让工程思维与解决复杂问题的能力回归基准线。

------

## 🚧 进度与分支管理

本 README 结构将随开发进度动态更新。

| **阶段**        | **分支**   | **状态** | **说明**                                                                 |
| --------------- | ---------- | -------- | ------------------------------------------------------------------------ |
| **词法分析**    | `lexer`    | ✅ 已完成 | Token 识别与错误处理，输出 `output.txt` / `error.txt`。                    |
| **语法分析**    | `parser`   | ✅ 已完成 | 递归下降 + AST，输出 `parser.txt` / `error.txt`。                         |
| **语义/符号表** | `analyzer` | ✅ 已完成 | 符号表、作用域（RAII）、Visitor 遍历；输出 `symbol.txt` / `error.txt`。   |
| **中间代码**    | `llvm_ir`  | ✅ 已完成 | 基于内存的 IR 树形结构（Value/User）、IRBuilder 模式生成，输出 `llvm_ir.txt`。|
| **IR 优化**     | `mem2reg`  | ✅ 已完成 | **Mem2Reg Pass**：CFG 构建、Cooper 支配树、支配边界、φ 节点插入与 SSA 重命名；输出完全 SSA 形式的 `llvm_ir.txt`。|
| **目标代码**    | `mips`     | ✅ 已完成 | **MIPS 后端**：全栈分配、指令选择、调用约定、Phi 下降；模块化架构，输出 `mips.txt`。|
| **代码优化**    | `optimize` | ✅ 已完成 | **IR 优化**：常量折叠+LVN、死代码消除。**MIPS 优化**：乘除强度削减、冗余跳转消除、窥孔优化、图染色寄存器分配（Chaitin-Briggs）。|

------

## 📁 项目结构

当前主流程为 **Lexer → Parser → SemanticAnalyzer → IRGenVisitor → Mem2Reg → ConstFoldLVN → DCE → MipsEmitter（含图染色寄存器分配）**；无错误时写完全 SSA 形式的 `llvm_ir.txt` 与优化后的 MIPS 汇编 `mips.txt`，有错误时合并之前阶段错误写 `error.txt`。

```Plaintext
.
├── CMakeLists.txt
├── src/
│   ├── main.cpp              # 入口：读 testfile.txt，完整编译，写 llvm_ir.txt + mips.txt / error.txt
│   ├── Driver.cpp             # 主控流程
│   ├── Lexer.cpp
│   ├── Parser.cpp            # 递归下降 + AST 构造
│   ├── AST.cpp
│   ├── TokenType.cpp
│   ├── SemanticAnalyzer.cpp  # 语义分析 Visitor：符号表、作用域、错误检查
│   ├── SymbolTable.cpp       # 作用域栈、Lookup/Register
│   ├── Symbol.cpp
│   ├── ScopeGuard.cpp
│   ├── ir/                   # IR 基础数据结构
│   │   ├── BasicBlock.cpp
│   │   ├── Constant.cpp
│   │   ├── Function.cpp
│   │   ├── Instruction.cpp
│   │   ├── Module.cpp
│   │   ├── Type.cpp
│   │   ├── User.cpp
│   │   ├── Value.cpp
│   │   ├── Argument.cpp
│   │   ├── GlobalVar.cpp
│   │   ├── TypeManager.cpp
│   │   └── IRPrintContext.cpp
│   ├── irgen/                # IR 生成与转换
│   │   ├── IRDeclEmitter.cpp
│   │   ├── IRGenContext.cpp
│   │   ├── IRGenVisitor.cpp
│   │   ├── IRGenVisitorExpr.cpp
│   │   ├── IRGenVisitorStmt.cpp
│   │   ├── IRScopeGuard.cpp
│   │   ├── TypeMapping.cpp
│   │   └── ConstExpEvaluator.cpp
│   ├── pass/                 # IR 优化 Pass
│   │   ├── CFGBuilder.cpp
│   │   ├── DomTree.cpp
│   │   ├── Mem2Reg.cpp
│   │   ├── ConstFoldLVN.cpp  # 常量折叠 + 局部值编号
│   │   └── DCE.cpp           # 死代码消除
│   └── mips/                 # MIPS 后端代码生成
│       ├── MipsEmitter.cpp   # 顶层驱动：.data / .text 段
│       ├── FunctionEmitter.cpp # 每函数编排：prologue + body + 寄存器分配接入
│       ├── InstructionEmitter.cpp # 指令选择：IR → MIPS 序列（含虚拟寄存器发射）
│       ├── StackFrame.cpp    # 栈帧布局与 Value 偏移计算
│       ├── AsmWriter.cpp     # MIPS 汇编输出格式化
│       ├── LivenessAnalysis.cpp # 活跃变量分析 + 干涉图构建
│       ├── RegAlloc.cpp      # 图染色寄存器分配（Chaitin-Briggs）+ 缓冲区重写
│       └── MipsCommon.cpp    # 标签生成等共享工具
├── include/
│   ├── Lexer.h
│   ├── Token.h
│   ├── TokenType.h
│   ├── Parser.h
│   ├── AST.h                 # AST 节点与 Accept(Visitor)
│   ├── ASTVisitor.h          # Visitor 接口
│   ├── Driver.h
│   ├── SemanticAnalyzer.h    # 语义分析 Visitor 实现
│   ├── SymbolTable.h
│   ├── Symbol.h
│   ├── ScopeGuard.h
│   ├── ir/                   # IR 定义头文件
│   │   ├── Value.h
│   │   ├── User.h
│   │   ├── Use.h
│   │   ├── Type.h
│   │   ├── TypeManager.h
│   │   ├── Constant.h
│   │   ├── Instruction.h
│   │   ├── BasicBlock.h
│   │   ├── Function.h
│   │   ├── Argument.h
│   │   ├── GlobalVar.h
│   │   ├── Module.h
│   │   ├── IRBuilder.h
│   │   └── IRPrintContext.h
│   ├── irgen/                # IR 生成相关头文件
│   │   ├── IRGenVisitor.h
│   │   ├── IRGenContext.h
│   │   ├── IRDeclEmitter.h
│   │   ├── IRScopeGuard.h
│   │   ├── TypeMapping.h
│   │   ├── ConstExpEvaluator.h
│   │   └── SSANameAllocator.h
│   ├── pass/                 # IR 优化 Pass 头文件
│   │   ├── Pass.h
│   │   ├── CFGBuilder.h
│   │   ├── DomTree.h
│   │   ├── Mem2Reg.h
│   │   ├── ConstFoldLVN.h
│   │   └── DCE.h
│   └── mips/                 # MIPS 后端头文件
│       ├── MipsEmitter.h     # 顶层驱动
│       ├── FunctionEmitter.h # 每函数编排
│       ├── InstructionEmitter.h # 指令选择
│       ├── StackFrame.h      # 栈帧布局
│       ├── AsmWriter.h       # 汇编输出助手
│       ├── MipsInst.h        # 结构化 MIPS 指令表示
│       ├── LivenessAnalysis.h # 活跃分析 + 干涉图
│       ├── RegAlloc.h        # 图染色寄存器分配器
│       ├── MipsCommon.h      # 共享工具与常量
│       ├── MipsOptions.h     # 编译选项 / 优化开关
│       └── ValueLocation.h   # Value 位置抽象（栈/寄存器）
└── docs/
    ├── ai_collab_notes/      # AI协作记录
    ├── course_info/          # 实验要求与课程规范
    │   ├── requirement_1_lexer.md
    │   ├── requirement_2_parser.md
    │   ├── requirement_3_analyzer.md
    │   ├── requirement_4_codegen_simple.md
    │   ├── requirement_5_codegen.md
    │   ├── 2024_SysY_grammar.md
    │   ├── 2024_SysY_detailed.md
    │   ├── llvm_course_guide.md
    │   └── optimization_course_guide.md
    └── design_documents/     # 设计文档
        ├── lexer.md
        ├── parser.md
        ├── semantic_analyzer.md
        ├── llvm_ir.md
        ├── mem2reg.md
        └── mips_backend.md
```

------

## 🔍 词法分析 (Lexer)

### 1. 功能概述

基于课程文法定义，实现对 SysY 源文件的词法解析。

- **设计模式**：采用手写状态机（State Machine）逻辑，而非使用 Flex/Lex 等生成工具，以增强对底层逻辑的掌控。
- **C++ 特性实践**：尝试使用 `std::fstream` 流式处理、`enum class` 类型安全枚举以及 STL 容器优化查找性能。

### 2. 输入与输出规范（Lexer 阶段）

若仅使用词法分析，程序读取 `testfile.txt`，可生成：

| **场景**     | **输入文件**   | **输出文件** | **输出内容格式**                 |
| ------------ | -------------- | ------------ | -------------------------------- |
| **正常情况** | `testfile.txt` | `output.txt` | `单词类别码 单词值` （每行一项） |
| **包含错误** | `testfile.txt` | `error.txt`  | `行号 错误类别码` （每行一项）   |

> **注意**：**单词类别码**详见 [第一次实验要求文档](docs/course_info/requirement_1_lexer.md)。词法阶段主要处理 **a 类错误**（非法字符/格式错误）；存在词法错误时仍会继续解析以暴露更多错误。

### 3. 相关文件

词法分析相关核心文件：`include/Token.h`、`include/TokenType.h`、`include/Lexer.h`，`src/Lexer.cpp`、`src/TokenType.cpp`。完整目录见「项目结构」。

------

## 🌲 语法分析 (Parser)

### 1. 功能概述

在词法分析基础上，对 SysY 源程序进行递归下降语法分析，并构建抽象语法树 (AST)。

- **分析算法**：**递归下降分析法 (Recursive Descent)**，为文法中非终结符实现解析子程序，配合 **Cur / Lookahead / Lookahead2** 做产生式选择，**无回溯**。
- **与 Lexer 的交互**：拉取式 (Pull Model)，Parser 通过 `Advance()` 驱动 Lexer 消费 Token；Lexer 提供 `PeekNext()` / `PeekNext2()` 支持 1/2-token 超前查看。
- **AST 设计**：解析与存储分离；表达式统一为 **Exp** 体系（LVal、Number、BinaryExp、UnaryExp、FuncCall 等），二元运算用 **BinaryExp(lhs, rhs, op)** 左结合；详见 [Parser 设计文档](docs/design_documents/parser.md)。

### 2. 输入与输出规范（Parser 阶段）

程序读取 `testfile.txt`，运行 **Lexer + Parser**；若启用 Parser 输出则生成 `parser.txt`（正确时）或参与合并写 `error.txt`（词法 a 类、语法 i/j/k 类等）。详见 [第二次实验要求](docs/course_info/requirement_2_parser.md)。

------

## 📋 语义分析 (Semantic Analyzer)

### 1. 功能概述

在 AST 上做一遍 **Visitor 遍历**，维护栈式符号表与作用域，在声明处注册符号、在引用处查找并做语义检查；同时完成常量折叠以支持 ConstExp/数组维度求值。

- **作用域**：栈式符号表 + **ScopeGuard (RAII)**，进入 Block/FuncDef 时 PushScope，离开时自动 PopScope；函数形参与函数体共用一层作用域（VisitBlockContents 不二次压栈），见设计文档「函数作用域扁平化」。
- **错误**：收集型，不抛异常；支持课程要求的 b/c/d/e/f/g/h/l/m 等语义错误码，其中 **g（缺少 return）** 按课程简化规则仅检查函数体最后一条是否为 return，详见 [2024_SysY_detailed.md](docs/course_info/2024_SysY_detailed.md) 与 [语义分析设计文档](docs/design_documents/semantic_analyzer.md)。

### 2. 输入与输出规范（语义分析阶段）

| **场景**     | **输入**       | **输出文件**  | **输出内容格式**                                      |
| ------------ | -------------- | ------------- | ----------------------------------------------------- |
| **正确源程序** | `testfile.txt` | `symbol.txt`  | `作用域序号 标识符 类型名`（如 `1 year ConstInt`）     |
| **存在错误** | `testfile.txt` | `error.txt`   | `行号 错误类别码`（词法+语法+语义合并，按行号排序）   |

- 规范详见 [第三次实验要求](docs/course_info/requirement_3_analyzer.md)。`main` 不纳入符号表；符号输出可由主控开关控制（便于后续完整编译器关闭 symbol.txt）。

------

## ⚙️ 中间代码生成 (LLVM IR)

> **⚠️ LLVM 版本说明**：本项目**不遵循**课程 [第五次实验要求](docs/course_info/requirement_5_codegen.md) 中「LLVM 评测机使用 12.0.0 版本」的约束，生成的 IR 直接采用 **LLVM 20** 标准，以便使用现代工具链（如 `lli`、`opt`）验证与后续优化。

### 1. 功能概述

在抽象语法树 (AST) 和符号表的基础上，通过一遍 **Visitor 遍历**，将源程序转换为基于内存的 **LLVM IR** 对象结构（Module, Function, BasicBlock, Instruction 等），并最终将该内存结构序列化为文本格式的 LLVM IR。

- **架构设计**：采用与原生 LLVM 类似的 `Value -> User -> Instruction` 类继承体系。内存所有权明确：`Module` 拥有全局变量和函数，`Function` 拥有基本块，`BasicBlock` 拥有指令。采用 `std::unique_ptr` 管理树形拥有的生命周期，采用裸指针管理引用（操作数）。
- **生成模式**：
  - **IRGenVisitor** 继承 `ASTVisitor` 驱动 AST 遍历。
  - **IRBuilder** 充当工厂类，负责在当前基本块末尾创建并插入指令。
  - **IRDeclEmitter** 封装繁琐的符号声明逻辑。
  - **短路求值与控制流**：为 `&&` 和 `||` 实现了精确的短路控制流生成。采用局部变量 (Alloca/Load/Store) 机制代替 Phi 节点来处理短路求值结果及变量赋值，由 **Mem2Reg Pass** 在后续优化阶段消除。

### 2. 输入与输出规范（IR 生成阶段）

| **场景**     | **输入**       | **输出文件**  | **输出内容格式**                                      |
| ------------ | -------------- | ------------- | ----------------------------------------------------- |
| **正确源程序** | `testfile.txt` | `llvm_ir.txt` | 纯文本格式的 LLVM IR 代码，包含 `@main` 等函数定义与内部指令。 |
| **存在错误** | `testfile.txt` | `error.txt`   | `行号 错误类别码`（词法+语法+语义合并，无中间代码生成）。 |

- 规范详见 [第四次与第五次实验要求](docs/course_info/requirement_4_codegen_simple.md)。
- 本项目保证生成的 LLVM IR 可以使用 `lli` (LLVM Interpreter) 解释执行，具有完整的标准 C 语义。

------

## 🎯 MIPS 目标代码生成 (MIPS Backend)

### 1. 功能概述

在 Mem2Reg 产出的完全 SSA 形式 IR 基础上，遍历 `ir::Module`，将每条 IR 指令翻译为等价的 MIPS 汇编序列，写入 `mips.txt`，可在 MARS 4.5 上正确执行。

- **图染色寄存器分配**：实现完整的 Chaitin-Briggs 算法（Build → Simplify → Coalesce → Freeze → Spill → Select），通过活跃变量分析与干涉图构建，将虚拟寄���器分配到 18 个物理寄存器（`$t0`-`$t9` + `$s0`-`$s7`）中。George 准则合并消除冗余 MOVE 指令，callee-saved 寄存器自动保存/恢复。
- **模块���架构**：经 AI 协作重构，拆分为 10 个单一职责模块——StackFrame（栈帧布局）、InstructionEmitter（指令选择）、AsmWriter（输出格式化）、LivenessAnalysis（活跃分析）、RegAlloc（寄存器分配）等，职责清晰、依赖无环。
- **调用约定**：参数 0–3 通过 `$a0`–`$a3`，参数 4+ 由调用者在栈上传递；返回值 `$v0`；`$ra` 由被调函数保存/恢复。
- **Phi 下降**：在前驱块跳转前通过拓扑排序发射 move，解决并行复制的写覆盖问题。

### 2. 输入与输出规范（MIPS 阶段）

| **场景**     | **输入**       | **输出文件**  | **输出内容格式**                                      |
| ------------ | -------------- | ------------- | ----------------------------------------------------- |
| **正确源程序** | `testfile.txt` | `mips.txt`    | MIPS 汇编文本（.data + .text），可直接在 MARS 4.5 中 Run。 |
| **存在错误** | `testfile.txt` | `error.txt`   | `行号 错误类别码`（词法+语法+语义合并，无代码生成）。 |

- 规范详见 [第五次实验要求](docs/course_info/requirement_5_codegen.md)。
- 生成的 MIPS 汇编通过了 `SysY_Test_2024/` 下的全部测试样例验证。

------

## 🛠️ 构建与运行

### 环境要求

- **CMake**: ≥ 3.10
- **Compiler**: 支持 **C++17** 标准 (GCC/Clang/MSVC)

### 编译步骤



```Bash
# 在项目根目录下
mkdir build && cd build
cmake ..
cmake --build .
```

### 运行方式

将 `testfile.txt` 放在可执行文件所在目录（或配置 IDE 工作目录）。程序执行完整编译流水线 **Lexer → Parser → SemanticAnalyzer → IRGenVisitor → Mem2Reg → ConstFoldLVN → DCE → MipsEmitter（含寄存器分配）**，根据是否存在错误生成：

- **无错误**：`llvm_ir.txt`（完全 SSA 形式的 LLVM IR）、`mips.txt`（优化后的 MIPS 汇编，可在 MARS 4.5 中运行）。
- **有错误**：`error.txt`（行号 + 错误码，词法/语法/语义合并并按行号排序）。

```Bash
# Linux / macOS
./build/Compiler

# Windows
.\build\Compiler.exe
```

------

## 🧪 自建评测方案 (待完善)

> 由于课程官方评测平台已不可用，本项目将建立一套本地化的验证机制以确保重构正确性。

### 计划方案

1. **基准对比 (Baseline)**：
   - 利用原 Java 项目生成标准输出（词法 `output.txt`、语法 `parser.txt`、错误 `error.txt`）。
   - 编写 Python 脚本 (`diff_test.py`) 自动比对 C++ 版本与 Java 版本的输出差异。
2. **单元测试**：
   - 词法：边缘 Case（注释嵌套、跨行字符串、特殊符号粘连等）。
   - 语法：缺失分号/括号、产生式歧义等用例。

*(注：具体的评测脚本和测试用例集计划在后续 commit 中补充)*

------

## 📄 参考资料

### 实验要求

- [第一次实验：词法分析](docs/course_info/requirement_1_lexer.md)
- [第二次实验：语法分析](docs/course_info/requirement_2_parser.md)
- [第三次实验：语义分析](docs/course_info/requirement_3_analyzer.md)
- [第四次实验：代码生成（简单）](docs/course_info/requirement_4_codegen_simple.md)
- [第五次实验：代码生成](docs/course_info/requirement_5_codegen.md)

### 课程规范与文法

- [SysY 文法](docs/course_info/2024_SysY_grammar.md)、[SysY 详细定义](docs/course_info/2024_SysY_detailed.md)
- [LLVM 课程指导](docs/course_info/llvm_course_guide.md)
- [代码优化课程教程](docs/course_info/optimization_course_guide.md)

### 设计文档

- [词法分析设计文档](docs/design_documents/lexer.md)
- [语法分析设计文档](docs/design_documents/parser.md)
- [语义分析设计文档](docs/design_documents/semantic_analyzer.md)
- [LLVM IR 设计文档](docs/design_documents/llvm_ir.md)
- [Mem2Reg 优化设计文档](docs/design_documents/mem2reg.md)
- [MIPS 后端设计文档](docs/design_documents/mips_backend.md)
- [代码优化设计文档](docs/design_documents/optimization.md)
- [图染色寄存器分配设计文档](docs/design_documents/register_allocation.md)

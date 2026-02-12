<div align="center">

# BUAA Compiler - SysY (C++ Refactor)

[简体中文](README.md) | [English](README_en.md)

</div>

> 北航计算机学院编译原理课程实验 - SysY 语言编译器（C++17 重构版）

本仓库记录了该编译器从 Java 版本迁移至 C++ 版本的重构过程。项目按实验阶段管理分支，当前处于 **`parser`** 分支，已完成词法分析与语法分析阶段。

------

## 📖 项目背景

本项目是北京航空航天大学计算机学院编译原理课程实验（2024秋季）的 **C++ 重构版本**。原 Java 实现版本目前仅在本地作为逻辑参考。

**重构契机**：

2026年初，在梳理考研复试的项目经历时，重新审视了本科阶段的工程实践。编译原理课设作为本科期间对**代码掌控力**与**系统架构思维**最具挑战性的训练，其最终形态若仅停留于“勉强运行”，实为核心竞争力的缺失与遗憾。

鉴于完善项目不可避免需要复盘原有逻辑，为提高有限时间内的**投入产出比 (ROI)**，决定摒弃在原 Java 代码上修补的方案，转而采用 C++ 重构。此举既能补全架构设计，又能顺势完成对 C++ 语言特性的掌握——着眼于长远的技术储备，这无疑是一笔**高回报的时间投资**。

- **预期净投入时间**：2周。

### 🎯 重构目标与意义

1. **系统完整性补全**

   原 Java 版本因彼时时间仓促，代码生成仅止步于 LLVM IR，且存在大量因赶工导致的架构耦合。本次重构旨在彻底完成 **MIPS 汇编生成**及**后端优化**，并重构不合理的模块交互逻辑，打造一个架构清晰的完整编译器。

2. **Modern C++ 深度实践**
   
   本项目将作为 C++ 技能树的实战演练场。重点聚焦于面向对象设计 (Object-Oriented Programming, OOP) 的优雅实现，以及智能指针 (Smart Pointers)、STL 高级特性与右值引用的实际应用，确保代码风格符合现代工程标准。

3. **工程能力复健**

   在考研长周期的理论复习后，通过为期两周的高强度重构冲刺，快速恢复代码手感，让工程思维与解决复杂问题的能力回归基准线。

------

## 🚧 进度与分支管理

本 README 结构将随开发进度动态更新。

| **阶段**        | **分支**   | **状态** | **说明**                                           |
| --------------- | ---------- | -------- | -------------------------------------------------- |
| **词法分析**    | `lexer`    | ✅ 已完成 | 实现了基本的 Token 识别与错误处理。                |
| **语法分析**    | `parser`   | ✅ 已完成 | 当前分支。递归下降 + AST，输出语法成分与错误码。   |
| **语义/符号表** | `semantic` | ⏳ 待开发 | 作用域管理与类型检查。                             |
| **中间代码**    | `ir`       | ⏳ 待开发 | LLVM IR 生成。                                     |
| **目标代码**    | `backend`  | ⏳ 待开发 | **本次重构核心目标**：MIPS 生成 + 寄存器分配优化。 |

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

### 3. 项目结构 (Lexer)

词法分析相关核心文件：`include/Token.h`、`include/TokenType.h`、`include/Lexer.h`，`src/Lexer.cpp`、`src/TokenType.cpp`。完整目录见下方「项目结构 (Parser 分支)」。

------

## 🌲 语法分析 (Parser)

### 1. 功能概述

在词法分析基础上，对 SysY 源程序进行递归下降语法分析，并构建抽象语法树 (AST)。

- **分析算法**：**递归下降分析法 (Recursive Descent)**，为文法中非终结符实现解析子程序，配合 **Cur / Lookahead / Lookahead2** 做产生式选择，**无回溯**。
- **与 Lexer 的交互**：拉取式 (Pull Model)，Parser 通过 `Advance()` 驱动 Lexer 消费 Token；Lexer 提供 `PeekNext()` / `PeekNext2()` 支持 1/2-token 超前查看。
- **AST 设计**：解析与存储分离；表达式统一为 **Exp** 体系（LVal、Number、BinaryExp、UnaryExp、FuncCall 等），二元运算用 **BinaryExp(lhs, rhs, op)** 左结合；详见 [Parser 设计文档](docs/design_documents/parser.md)。

### 2. 输入与输出规范（Parser 分支）

程序默认读取工作目录下的 `testfile.txt`，运行 **Lexer + Parser**，根据是否存在错误生成不同文件：

| **场景**     | **输入文件**   | **输出文件** | **输出内容格式**                                                                 |
| ------------ | -------------- | ------------ | -------------------------------------------------------------------------------- |
| **正确源程序** | `testfile.txt` | `parser.txt` | 按读入顺序：每行 `单词类别码 单词值` 或单独一行 `<语法成分名>`（如 `<Stmt>`）     |
| **存在错误** | `testfile.txt` | `error.txt`  | `行号 错误类别码`（每行一项，按行号排序；合并词法 a 类与语法 i/j/k 类错误）        |

> **注意**：
>
> - 输出规范详见 [第二次实验要求文档](docs/course_info/requirement_2_parser.md)。
> - 语法错误码 **i**（缺分号）、**j**（缺右小括号）、**k**（缺右中括号）等由 Parser 检测；即使有错误也会完成整轮词法+语法分析并输出全部错误。

### 3. 项目结构 (Parser 分支)

```Plaintext
.
├── CMakeLists.txt          # C++17 标准构建配置
├── src/
│   ├── main.cpp            # 程序入口：读 testfile.txt，驱动 Lexer + Parser，写 parser.txt / error.txt
│   ├── Lexer.cpp           # 词法分析核心实现
│   ├── parser.cpp          # 语法分析核心实现（递归下降 + AST 构造）
│   └── TokenType.cpp       # Token 字符串转换工具
├── include/
│   ├── Lexer.h             # Lexer 类定义
│   ├── Parser.h            # Parser 类与解析接口
│   ├── AST.h               # 抽象语法树节点定义（CompUnit、Decl、Stmt、Exp 等）
│   ├── Token.h             # Token 结构体与相关类型
│   └── TokenType.h         # 单词类别码 (enum class)
└── docs/
    ├── course_info/        # 课程原始文档与要求
    │   ├── 2024_SysY_grammar.md
    │   ├── 2024_SysY_detailed.md
    │   ├── requirement_1_lexer.md
    │   ├── requirement_2_parser.md
    │   ├── requirement_3_semantics.md
    │   ├── requirement_4_codegen_simple.md
    │   └── requirement_5_codegen.md
    └── design_documents/   # 设计说明
        ├── lexer.md
        └── parser.md
```

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

确保 `testfile.txt` 位于可执行文件同一目录（或根据 IDE 工作目录配置调整）。程序将根据是否存在词法/语法错误，生成 `parser.txt`（正确时）或 `error.txt`（有错误时）：

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

- [词法分析实验要求](docs/course_info/requirement_1_lexer.md)
- [语法分析实验要求](docs/course_info/requirement_2_parser.md)
- [SysY 文法与说明](docs/course_info/2024_SysY_grammar.md)、[SysY 详细定义](docs/course_info/2024_SysY_detailed.md)
- [词法分析设计文档](docs/design_documents/lexer.md)、[语法分析设计文档](docs/design_documents/parser.md)
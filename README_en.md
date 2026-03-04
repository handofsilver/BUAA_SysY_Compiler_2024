<div align="center">

# BUAA Compiler - SysY (C++ Refactor)

[简体中文](README.md) | [English](README_en.md)

</div>

> BUAA School of Computer Science and Engineering - Compiler Principles Course Project - SysY Language Compiler (C++17 Refactored Version)

This repository documents the refactoring of a SysY compiler from Java to C++. Development is organized by stages. **Lexical analysis**, **syntax analysis**, **semantic analysis**, and **intermediate code generation (including Mem2Reg optimization)** are all complete. The driver reads `testfile.txt`, runs the full frontend pipeline followed by the Mem2Reg SSA promotion pass, and writes a fully-SSA-form `llvm_ir.txt` (no errors) or `error.txt` (errors present).

------

## 📖 Project Background

This project is a **C++ refactoring** of the Compiler Principles course assignment (Autumn 2024) at the School of Computer Science and Engineering, Beihang University (BUAA). The original Java implementation now serves primarily as a local logic reference.

**Motivation for Refactoring**:

In early 2026, while preparing my portfolio for postgraduate interviews, I reviewed my undergraduate engineering projects. As the most rigorous training in **code control** and **system architecture** during my undergraduate studies, leaving the compiler project in a "barely functional" state felt like a missed opportunity and a regret.

Given that improving the project inevitably required reviewing the original logic, I decided to maximize **Return on Investment (ROI)** by refactoring it in C++ rather than patching the existing Java code. This approach allows me to finalize the architectural design while mastering modern C++ features—a **high-yield investment** for long-term technical growth.

- **Estimated Time Investment**: 2 weeks.

### 🎯 Refactoring Goals and Significance

1.  **Architectural Completeness**

    The original Java version halted at LLVM IR generation due to time constraints and suffered from tight coupling. This refactoring aims to implement **MIPS assembly generation** and **backend optimization**, decoupling modules to build a robust and well-architected compiler.

2.  **Deep Dive into Modern C++**

    This project serves as a training ground for advanced C++. It focuses on elegant Object-Oriented Programming (OOP) implementation and the practical application of Smart Pointers, STL, and Rvalue References, ensuring the code meets modern engineering standards.

3.  **Engineering Skills Restoration**

    Following a long period of theoretical review for entrance exams, this two-week intensive refactoring sprint aims to restore coding fluency and sharpen engineering intuition and complex problem-solving skills.

------

## 🚧 Progress and Branch Management

This README will be dynamically updated to reflect development progress.

| **Stage** | **Branch** | **Status** | **Description** |
| :--- | :--- | :--- | :--- |
| **Lexical Analysis** | `lexer` | ✅ Completed | Token recognition and error handling; outputs `output.txt` / `error.txt`. |
| **Syntax Analysis** | `parser` | ✅ Completed | Recursive descent + AST; outputs `parser.txt` / `error.txt`. |
| **Semantics / Symbol Table** | `analyzer` | ✅ Completed | Symbol table, scopes (RAII), Visitor traversal; outputs `symbol.txt` / `error.txt`. |
| **Intermediate Code** | `llvm_ir` | ✅ Completed | In-memory IR structure (Value/User), IRBuilder generation, outputs `llvm_ir.txt`. |
| **IR Optimization** | `mem2reg` | ✅ Completed | **Mem2Reg Pass**: CFG construction, Cooper dominator tree, dominance frontier, φ-node insertion and SSA renaming; outputs fully-SSA-form `llvm_ir.txt`. |
| **Target Code** | `mips` | ⏳ Pending | **Core goal**: MIPS generation + register allocation optimization. |

------

## 📁 Project Structure

The main pipeline is **Lexer → Parser → SemanticAnalyzer → IRGenVisitor → Mem2RegPass**. Final output: **no errors** → fully-SSA-form `llvm_ir.txt`; **any errors** → merged `error.txt` from all previous stages.

```Plaintext
.
├── CMakeLists.txt
├── src/
│   ├── main.cpp              # Entry: read testfile.txt, execute to IR generation, write llvm_ir.txt / error.txt
│   ├── Driver.cpp            # Main driver
│   ├── Lexer.cpp
│   ├── Parser.cpp            # Recursive descent + AST construction
│   ├── AST.cpp
│   ├── TokenType.cpp
│   ├── SemanticAnalyzer.cpp  # Semantic Visitor: symbol table, scopes, errors
│   ├── SymbolTable.cpp       # Scope stack, Lookup/Register
│   ├── Symbol.cpp
│   ├── ScopeGuard.cpp
│   ├── ir/                   # Core IR data structures
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
│   ├── irgen/                # IR generation and translation
│   │   ├── IRDeclEmitter.cpp
│   │   ├── IRGenContext.cpp
│   │   ├── IRGenVisitor.cpp
│   │   ├── IRGenVisitorExpr.cpp
│   │   ├── IRGenVisitorStmt.cpp
│   │   ├── IRScopeGuard.cpp
│   │   ├── TypeMapping.cpp
│   │   └── ConstExpEvaluator.cpp
│   └── pass/                 # IR optimization passes
│       ├── CFGBuilder.cpp
│       ├── DomTree.cpp
│       └── Mem2Reg.cpp
├── include/
│   ├── Lexer.h
│   ├── Token.h
│   ├── TokenType.h
│   ├── Parser.h
│   ├── AST.h                 # AST nodes and Accept(Visitor)
│   ├── ASTVisitor.h          # Visitor interface
│   ├── Driver.h
│   ├── SemanticAnalyzer.h    # Semantic analysis Visitor implementation
│   ├── SymbolTable.h
│   ├── Symbol.h
│   ├── ScopeGuard.h
│   ├── ir/                   # IR headers
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
│   ├── irgen/                # IR generation headers
│   │   ├── IRGenVisitor.h
│   │   ├── IRGenContext.h
│   │   ├── IRDeclEmitter.h
│   │   ├── IRScopeGuard.h
│   │   ├── TypeMapping.h
│   │   ├── ConstExpEvaluator.h
│   │   └── SSANameAllocator.h
│   └── pass/                 # IR optimization pass headers
│       ├── Pass.h
│       ├── CFGBuilder.h
│       ├── DomTree.h
│       └── Mem2Reg.h
└── docs/
    ├── ai_collab_notes/      # AI collaboration notes
    ├── course_info/          # Experiment requirements and course specs
    │   ├── requirement_1_lexer.md
    │   ├── requirement_2_parser.md
    │   ├── requirement_3_analyzer.md
    │   ├── requirement_4_codegen_simple.md
    │   ├── requirement_5_codegen.md
    │   ├── 2024_SysY_grammar.md
    │   ├── 2024_SysY_detailed.md
    │   └── llvm_course_guide.md
    └── design_documents/     # Design documents
        ├── lexer.md
        ├── parser.md
        ├── semantic_analyzer.md
        ├── llvm_ir.md
        └── mem2reg.md
```

------

## 🔍 Lexical Analysis (Lexer)

### 1. Overview

Implement lexical analysis for SysY source files based on the course grammar definition.

- **Implementation Strategy**: Utilize a hand-written State Machine instead of generator tools like Flex/Lex to maximize control over low-level logic.
- **C++ Practice**: Leverage `std::fstream` for stream processing, `enum class` for type safety, and STL containers for optimized lookup performance.

### 2. I/O Specification (Lexer stage)

When using lexical analysis only, the program reads `testfile.txt` and can produce:

| **Scenario** | **Input File** | **Output File** | **Output Format** |
| :--- | :--- | :--- | :--- |
| **Normal** | `testfile.txt` | `output.txt` | `TokenCode TokenValue` (one per line) |
| **Error** | `testfile.txt` | `error.txt` | `LineNumber ErrorCode` (one per line) |

> **Note**: **Token codes** are defined in the [Experiment 1 Requirements](docs/course_info/requirement_1_lexer.md). The lexer handles **Type A errors** (illegal characters/format errors) and continues parsing to expose more errors when present.

### 3. Related Files

Core lexer files: `include/Token.h`, `include/TokenType.h`, `include/Lexer.h`, `src/Lexer.cpp`, `src/TokenType.cpp`. The full directory tree is given in the **Project Structure** section above.

------

## 🌲 Syntax Analysis (Parser)

### 1. Overview

Building on the lexer, the parser performs recursive-descent syntax analysis and constructs an Abstract Syntax Tree (AST).

- **Algorithm**: **Recursive descent** with a parsing routine per non-terminal; **Cur / Lookahead / Lookahead2** are used for production selection with **no backtracking**.
- **Lexer interaction**: **Pull model**—the parser drives the lexer via `Advance()`; the lexer provides `PeekNext()` / `PeekNext2()` for 1- and 2-token lookahead.
- **AST design**: Parsing is decoupled from tree storage; expressions are unified under **Exp** (LVal, Number, BinaryExp, UnaryExp, FuncCall, etc.), with binary operations as left-associative **BinaryExp(lhs, rhs, op)**. See the [Parser design document](docs/design_documents/parser.md) for details.

### 2. I/O Specification (Parser stage)

When running **Lexer + Parser**, the program reads `testfile.txt`; if parser output is enabled it produces `parser.txt` (on success) or contributes to a merged `error.txt` (lexical type-a, syntax i/j/k, etc.). See [Experiment 2 Requirements](docs/course_info/requirement_2_parser.md).

------

## Semantic Analysis (Semantic Analyzer)

### 1. Overview

A single **Visitor** pass over the AST maintains a stack-based symbol table and scopes: declarations are registered, uses are looked up and checked, and constant folding is performed for ConstExp and array dimensions.

- **Scopes**: Stack-based table with **ScopeGuard (RAII)**; entering a Block or function body does a PushScope, leaving does PopScope. Function parameters and body share one scope (see “function scope flattening” in the design doc).
- **Errors**: Collected without throwing; supports course error codes b/c/d/e/f/g/h/l/m. Error **g** (missing return) follows the course’s simplified rule: only the last statement of the function body is checked for a return; see [2024_SysY_detailed.md](docs/course_info/2024_SysY_detailed.md) and the [semantic analysis design doc](docs/design_documents/semantic_analyzer.md).

### 2. I/O Specification (semantic stage)

| **Scenario** | **Input** | **Output file** | **Output format** |
| :--- | :--- | :--- | :--- |
| **Correct source** | `testfile.txt` | `symbol.txt` | `ScopeId Identifier TypeName` (e.g. `1 year ConstInt`) |
| **Errors present** | `testfile.txt` | `error.txt` | `LineNumber ErrorCode` (lex + parse + semantic merged, sorted by line) |

See [Experiment 3 Requirements](docs/course_info/requirement_3_analyzer.md). The name `main` is not entered in the symbol table; symbol output can be turned off in the driver for use as a full compiler later.

------

## ⚙️ Intermediate Code Generation (LLVM IR)

> **⚠️ LLVM version**：This project **does not follow** the course [Experiment 5 requirement](docs/course_info/requirement_5_codegen.md) that the evaluation environment uses LLVM 12.0.0. Generated IR conforms to **LLVM 20** so that modern toolchains (e.g. `lli`, `opt`) can be used for verification and future optimizations.

### 1. Overview

Building upon the Abstract Syntax Tree (AST) and the symbol table, a single **Visitor pass** converts the source program into an in-memory **LLVM IR** object structure (Module, Function, BasicBlock, Instruction, etc.), which is then serialized into text-format LLVM IR.

- **Architecture Design**: Employs a simplified `Value -> User -> Instruction` class hierarchy inspired by native LLVM. Clear memory ownership: `Module` owns global variables and functions, `Function` owns basic blocks, and `BasicBlock` owns instructions using `std::unique_ptr`. Raw pointers are used for references (operands).
- **Generation Patterns**:
  - **IRGenVisitor** extends `ASTVisitor` to drive the AST traversal.
  - **IRBuilder** acts as a factory class, creating and inserting instructions at the current basic block.
  - **IRDeclEmitter** encapsulates verbose symbol declaration logic.
  - **Short-circuit Evaluation & Control Flow**: Implements precise short-circuiting for `&&` and `||`. Local variable memory slots (Alloca/Load/Store) are used instead of Phi nodes to handle evaluation results and assignments, which are eliminated by the **Mem2Reg Pass** in the subsequent optimization stage.

### 2. I/O Specification (IR stage)

| **Scenario** | **Input** | **Output file** | **Output format** |
| :--- | :--- | :--- | :--- |
| **Correct source** | `testfile.txt` | `llvm_ir.txt` | Plain text LLVM IR code containing function definitions like `@main` and internal instructions. |
| **Errors present** | `testfile.txt` | `error.txt` | `LineNumber ErrorCode` (lex + parse + semantic merged; no IR is generated). |

- See the [Experiment 4 & 5 Requirements](docs/course_info/requirement_4_codegen_simple.md).
- The generated LLVM IR is strictly compliant and can be executed using `lli` (LLVM Interpreter) to verify standard C semantics.

------

## 🛠️ Build and Run

### Requirements

- **CMake**: ≥ 3.10
- **Compiler**: Supports **C++17** standard (GCC/Clang/MSVC)

### Build Steps

```Bash
# In the project root directory
mkdir build && cd build
cmake ..
cmake --build .
```

### Usage

Place `testfile.txt` in the executable’s working directory (or set the IDE run configuration accordingly). The program runs **Lexer → Parser → SemanticAnalyzer** and produces:

- **No errors**: `symbol.txt` (scope id, identifier, type name). If parser output is enabled, `parser.txt` is also written.
- **Any errors**: `error.txt` (line number + error code; lex, parse, and semantic errors merged and sorted by line).

```Bash
# Linux / macOS
./build/Compiler

# Windows
.\build\Compiler.exe
```

------

## 🧪 Verification Strategy (Work in Progress)

> Since the official course evaluation platform is no longer available, this project establishes a localized verification mechanism to ensure refactoring correctness.

### Planned Approach

1.  **Baseline Comparison**:
    - Use the original Java project to generate reference output (lexer `output.txt`, parser `parser.txt`, errors `error.txt`).
    - Write a Python script (`diff_test.py`) to automatically compare C++ and Java output.
2.  **Unit Testing**:
    - Lexer: edge cases (nested comments, multi-line strings, adjacent operators).
    - Parser: missing semicolons/brackets, production ambiguities.

*(Scripts and test case sets are planned to be added in subsequent commits)*

------

## 📄 References

### Experiment Requirements

- [Experiment 1: Lexical Analysis](docs/course_info/requirement_1_lexer.md)
- [Experiment 2: Syntax Analysis](docs/course_info/requirement_2_parser.md)
- [Experiment 3: Semantic Analysis](docs/course_info/requirement_3_analyzer.md)
- [Experiment 4: Code Generation (Simple)](docs/course_info/requirement_4_codegen_simple.md)
- [Experiment 5: Code Generation](docs/course_info/requirement_5_codegen.md)

### Course Specs and Grammar

- [SysY Grammar](docs/course_info/2024_SysY_grammar.md), [SysY detailed definition](docs/course_info/2024_SysY_detailed.md)
- [LLVM course guide](docs/course_info/llvm_course_guide.md)

### Design Documents

- [Lexer design](docs/design_documents/lexer.md)
- [Parser design](docs/design_documents/parser.md)
- [Semantic analyzer design](docs/design_documents/semantic_analyzer.md)
- [LLVM IR design](docs/design_documents/llvm_ir.md)
- [Mem2Reg optimization design](docs/design_documents/mem2reg.md)

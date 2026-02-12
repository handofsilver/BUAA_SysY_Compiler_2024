<div align="center">

# BUAA Compiler - SysY (C++ Refactor)

[简体中文](README.md) | [English](README_en.md)

</div>

> BUAA School of Computer Science and Engineering - Compiler Principles Course Project - SysY Language Compiler (C++17 Refactored Version)

This repository documents the refactoring of a SysY compiler from Java to C++. Project branches are managed according to development stages. Currently, the project is on the **`parser`** branch, with both lexical and syntax analysis stages completed.

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
| **Lexical Analysis** | `lexer` | ✅ Completed | Basic Token recognition and error handling. |
| **Syntax Analysis** | `parser` | ✅ Completed | Current branch. Recursive descent + AST; outputs syntax elements and error codes. |
| **Semantics/Symbol Table** | `semantic` | ⏳ Pending | Scope management and type checking. |
| **Intermediate Code** | `ir` | ⏳ Pending | LLVM IR generation. |
| **Target Code** | `backend` | ⏳ Pending | **Core Goal**: MIPS generation + register allocation optimization. |

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

### 3. Project Structure (Lexer)

Core lexer files: `include/Token.h`, `include/TokenType.h`, `include/Lexer.h`, `src/Lexer.cpp`, `src/TokenType.cpp`. The full directory tree is given under **Project Structure (Parser branch)** below.

------

## 🌲 Syntax Analysis (Parser)

### 1. Overview

Building on the lexer, the parser performs recursive-descent syntax analysis and constructs an Abstract Syntax Tree (AST).

- **Algorithm**: **Recursive descent** with a parsing routine per non-terminal; **Cur / Lookahead / Lookahead2** are used for production selection with **no backtracking**.
- **Lexer interaction**: **Pull model**—the parser drives the lexer via `Advance()`; the lexer provides `PeekNext()` / `PeekNext2()` for 1- and 2-token lookahead.
- **AST design**: Parsing is decoupled from tree storage; expressions are unified under **Exp** (LVal, Number, BinaryExp, UnaryExp, FuncCall, etc.), with binary operations as left-associative **BinaryExp(lhs, rhs, op)**. See the [Parser design document](docs/design_documents/parser.md) for details.

### 2. I/O Specification (Parser branch)

The program reads `testfile.txt` from the working directory, runs **Lexer + Parser**, and writes output as follows:

| **Scenario** | **Input File** | **Output File** | **Output Format** |
| :--- | :--- | :--- | :--- |
| **Correct source** | `testfile.txt` | `parser.txt` | In read order: each line `TokenCode TokenValue` or a single line `<SyntaxElement>` (e.g. `<Stmt>`) |
| **Errors present** | `testfile.txt` | `error.txt` | `LineNumber ErrorCode` (one per line, sorted by line; merges lexical Type A and syntax i/j/k errors) |

> **Note**:
>
> - See the [Experiment 2 Requirements](docs/course_info/requirement_2_parser.md) for the full output specification.
> - Syntax error codes **i** (missing semicolon), **j** (missing `)`), **k** (missing `]`) are reported by the parser; a full lexer + parser pass is always completed and all errors are output.

### 3. Project Structure (Parser branch)

```Plaintext
.
├── CMakeLists.txt          # C++17 standard build configuration
├── src/
│   ├── main.cpp            # Entry: read testfile.txt, run Lexer + Parser, write parser.txt / error.txt
│   ├── Lexer.cpp           # Lexical analysis core
│   ├── parser.cpp          # Syntax analysis core (recursive descent + AST construction)
│   └── TokenType.cpp       # Token string conversion
├── include/
│   ├── Lexer.h             # Lexer class
│   ├── Parser.h            # Parser class and parsing interface
│   ├── AST.h               # AST node definitions (CompUnit, Decl, Stmt, Exp, etc.)
│   ├── Token.h             # Token structure and related types
│   └── TokenType.h         # Token type codes (enum class)
└── docs/
    ├── course_info/        # Course materials and requirements
    │   ├── 2024_SysY_grammar.md
    │   ├── 2024_SysY_detailed.md
    │   ├── requirement_1_lexer.md
    │   ├── requirement_2_parser.md
    │   ├── requirement_3_semantics.md
    │   ├── requirement_4_codegen_simple.md
    │   └── requirement_5_codegen.md
    └── design_documents/   # Design notes
        ├── lexer.md
        └── parser.md
```

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

Ensure `testfile.txt` is in the same directory as the executable (or adjust according to your IDE's working directory). The program writes `parser.txt` when there are no errors, or `error.txt` when lexical/syntax errors exist:

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

- [Experiment 1 Requirements (Lexer)](docs/course_info/requirement_1_lexer.md)
- [Experiment 2 Requirements (Parser)](docs/course_info/requirement_2_parser.md)
- [SysY Grammar](docs/course_info/2024_SysY_grammar.md), [SysY Detailed Definition](docs/course_info/2024_SysY_detailed.md)
- [Lexer Design](docs/design_documents/lexer.md), [Parser Design](docs/design_documents/parser.md)

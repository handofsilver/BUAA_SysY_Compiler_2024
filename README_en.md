<div align="center">

# BUAA Compiler - SysY (C++ Refactor)

[简体中文](README.md) | [English](README_en.md)

</div>

> BUAA School of Computer Science and Engineering - Compiler Principles Course Project - SysY Language Compiler (C++17 Refactored Version)

This repository documents the refactoring of a SysY compiler from Java to C++. Project branches are managed according to development stages. Currently, the project is on the **`lexer`** branch, with the lexical analysis stage completed.

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
| **Lexical Analysis** | `lexer` | ✅ Completed | Current branch. Implemented basic Token recognition and error handling. |
| **Syntax Analysis** | `parser` | ⏳ Pending | Recursive descent parser implementation. |
| **Semantics/Symbol Table** | `semantic` | ⏳ Pending | Scope management and type checking. |
| **Intermediate Code** | `ir` | ⏳ Pending | LLVM IR generation. |
| **Target Code** | `backend` | ⏳ Pending | **Core Goal**: MIPS generation + register allocation optimization. |

------

## 🔍 Lexical Analysis (Lexer)

### 1. Overview

Implement lexical analysis for SysY source files based on the course grammar definition.

- **Implementation Strategy**: Utilize a hand-written State Machine instead of generator tools like Flex/Lex to maximize control over low-level logic.
- **C++ Practice**: Leverage `std::fstream` for stream processing, `enum class` for type safety, and STL containers for optimized lookup performance.

### 2. I/O Specification

The program reads `testfile.txt` from the working directory by default and generates the following outputs:

| **Scenario** | **Input File** | **Output File** | **Output Format** |
| :--- | :--- | :--- | :--- |
| **Normal** | `testfile.txt` | `output.txt` | `TokenCode TokenValue` (one per line) |
| **Error** | `testfile.txt` | `error.txt` | `LineNumber ErrorCode` (one per line) |

> **Note**:
>
> - **Token Codes**: Refer to the [Experiment 1 Requirements](docs/requirement_1_lexer.md).
> - **Error Handling**: Currently handles **Type A errors** (illegal characters/format errors). The lexer attempts to recover and parse subsequent content to expose potential latent errors.

### 3. Project Structure (Lexer)

```Plaintext
.
├── CMakeLists.txt          # C++17 standard build configuration
├── src/
│   ├── main.cpp            # Program entry: I/O stream management and main control logic
│   ├── Lexer.cpp           # Core implementation of lexical analysis (getsym)
│   └── TokenType.cpp       # Token string conversion tool
├── include/
│   ├── Lexer.h             # Lexer class definition
│   ├── Token.h             # Token structure and related types
│   └── TokenType.h         # Token type codes (enum class)
└── docs/                   # Archive of original course documents
    ├── grammar.md
    ├── requirement_1_lexer.md
    ├── requirement_2_parser.md
    ├── requirement_3_symbol.md
    ├── requirement_4_codegen_simple.md
    └── requirement_5_codegen.md
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

Ensure `testfile.txt` is in the same directory as the executable (or adjust according to your IDE's working directory settings):

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
    - Use the original Java project to generate reference output.
    - Write a Python script (`diff_test.py`) to automatically compare C++ and Java versions.
2.  **Unit Testing**:
    - Create a dedicated test suite for edge cases (nested comments, multi-line strings, adjacent operators).

*(Documentation and test cases are planned to be added in subsequent commits)*

------

## 📄 References

- [Experiment 1 Requirements](docs/requirement_1_lexer.md)
- [SysY Language Grammar](docs/grammar.md)

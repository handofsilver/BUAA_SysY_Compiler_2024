# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

A complete SysY-to-MIPS compiler written in Modern C++17, built for the BUAA (Beijing University of Aeronautics & Astronautics) Compiler Principles course. Currently in the optimization phase (`optimize` branch).

## Build

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

The executable is named `Compiler` and must be run from the directory containing `testfile.txt` (i.e., the project root):

```bash
./build/Compiler
```

There is no automated test framework. Testing is manual: place a SysY source in `testfile.txt`, run the compiler, inspect output files (`lexer.txt`, `parser.txt`, `symbol.txt`, `llvm_ir.txt`, `mips.txt`, `error.txt`). MIPS output can be validated with MARS 4.5 (`Mars.jar` at project root); IR output with `lli`.

## Compilation Pipeline

```
testfile.txt
  → Lexer           → (lexer.txt)
  → Parser          → (parser.txt)
  → SemanticAnalyzer → (symbol.txt)
  → IRGenVisitor    → IR Module
  → Mem2Reg pass    → SSA form
  → ConstFoldLVN pass → constant folding + LVN
  → DCE pass        → dead code elimination
  → (llvm_ir.txt)
  → MipsEmitter     → (mips.txt)
```

All phase-enable flags are `const bool` constants at the top of `src/main.cpp`. Toggling them is the standard way to enable/disable a phase or optimization.

## Architecture

### Frontend (`include/` top-level + `src/`)
- **Lexer / Parser**: Hand-written recursive-descent parser with 1–2 token lookahead, no backtracking. Produces an AST.
- **AST**: `include/AST.h` — node hierarchy rooted at `ASTNode`. Uses double-dispatch: each node has `Accept(ASTVisitor&)`.
- **ASTVisitor**: `include/ASTVisitor.h` — abstract base. Both `SemanticAnalyzer` and `IRGenVisitor` implement it.
- **SemanticAnalyzer**: scoped symbol table (`SymbolTable`), RAII scope guards (`ScopeGuard`). Collects errors without throwing.
- **Driver**: `RunCompiler()` in `src/Driver.cpp` — sequences Lexer → Parser → Semantic → IRGen and returns a `CompilerResult` containing the IR `Module`.

### IR (`include/ir/`, `src/ir/`)
Modeled closely after LLVM's IR:
- **Value → User → Instruction** hierarchy with use-list / def-use chains (`Use.h`).
- `Module` owns `Function`s and `GlobalVar`s; `Function` owns `BasicBlock`s; `BasicBlock` owns `Instruction`s.
- All IR values use `std::shared_ptr`; operand references are `Use` objects maintaining the def-use chain.
- `IRBuilder` provides factory methods for creating instructions inside a `BasicBlock`.

### IR Generation (`include/irgen/`, `src/irgen/`)
- `IRGenVisitor` — main visitor, split across `IRGenVisitorExpr.cpp` and `IRGenVisitorStmt.cpp`.
- `IRGenContext` — holds the active `IRBuilder` and current block; shared across sub-emitters.
- `IRDeclEmitter` — handles variable/function declarations.
- `ConstExpEvaluator` — compile-time constant folding for initializers and array sizes.
- All local variables are initially `alloca`-based; Mem2Reg converts them to SSA.

### Optimization Passes (`include/pass/`, `src/pass/`)
- Base class: `Pass.h`. Each pass has a `Run(Function&)` method.
- **Mem2Reg**: builds CFG (`CFGBuilder`), constructs dominator tree (`DomTree`), computes dominance frontier, inserts `phi` nodes, renames variables to SSA.
- **ConstFoldLVN**: constant folding + local value numbering per basic block. Run to fixpoint in `main.cpp`.
- **DCE**: dead code elimination. Separates operand detachment from instruction erasure to avoid use-after-free.

### MIPS Backend (`include/mips/`, `src/mips/`)
- **MipsEmitter**: top-level driver; emits `.data` and `.text` sections.
- **FunctionEmitter**: per-function prologue/epilogue, delegates instruction selection.
- **InstructionEmitter**: IR instruction → MIPS instruction selection.
- **StackFrame**: tracks stack layout; every SSA value gets a spill slot (full-stack-allocation strategy; register allocation is planned but not yet implemented).
- **MipsInst / MipsOpcode** (`include/mips/MipsInst.h`): structured representation of MIPS instructions. All `EmitXxx` methods construct a `MipsInst`; peephole operates on structured fields; serialization to text happens once in `AsmWriter::Serialize()`.
- **AsmWriter**: formats MIPS assembly text. Internal buffer is `vector<MipsInst>`. Exposes `GetBuffer()` for register allocation passes to read/rewrite instructions.
- **MipsOptions** (`include/mips/MipsOptions.h`): struct of optimization flags passed through from `main.cpp`. Add new backend flags here.
- **ValueLocation** (`include/mips/ValueLocation.h`): abstraction over register vs. stack slot; prepared for register allocator.

## Documentation Style

Design documents live in `docs/design_documents/`. When writing or updating them, follow these requirements:

- **Code + prose organic combination**: never dump large code blocks without context, and never write dry text-only descriptions. Interleave code snippets with explanatory text so each illuminates the other. Code blocks should replace verbose natural-language descriptions where the code itself is more direct.
- **Code navigation**: each major section should include a "代码导读" (code reading guide) subsection showing the file → function reading order as a tree, so readers know exactly where to start and what each piece does.
- **Complete call chains**: show the full path from entry point (e.g., `main.cpp`) down to the implementation function, not just isolated snippets. Readers should be able to follow the data/control flow end-to-end.
- **Mermaid diagrams**: use mermaid flowcharts for complex algorithms, decision trees, and multi-step processes to make them visually intuitive.
- **Sufficient detail**: the document should be detailed enough that someone who has **not read the source code** can understand the complete design. Err on the side of more detail, not less.
- **"Why" alongside "what"**: when showing a design decision or implementation detail, explain the reasoning behind it (especially non-obvious invariants, safety concerns, or tradeoffs).

## Key Conventions

- **Header-only declarations**: all classes declared in `include/`; implementations in matching `src/` files.
- **No exceptions in pipeline**: errors are collected into `result.errors` and returned; the pipeline checks `has_errors` and short-circuits.
- **Use-list invariant**: when erasing an instruction, detach all its operands first (call `dropAllOperands()` or equivalent) before removing it from the basic block. DCE pass demonstrates the correct pattern.
- **`PhiInst` is a `User`**: phi nodes participate in the def-use chain just like other instructions; treat them the same in optimization passes.
- **MIPS target**: MARS 4.5 simulator. Syscall conventions follow MARS 4.5 (not Linux MIPS).

# MIPS 后端模块化重构进度记录

**日期**: 2026-03-10
**参考计划**: `mips_backend_modular_refactor_plan_20260310.md`
**当前状态**: **全部步骤已完成**

---

## 一、已完成的所有步骤

### S1 ✅ 提取 MipsCommon

- 新增 `include/mips/MipsCommon.h` 和 `src/mips/MipsCommon.cpp`
- `GlobalLabel()`、`BlockLabel()`、`kIndent` 常量统一到此模块，消除两处重复定义
- 新增 `IsLibraryFunction()` 集中判断库函数
- `MipsEmitter.cpp` 和 `FunctionEmitter.cpp` 均改为 `#include "mips/MipsCommon.h"`

### S2 ✅ 提取 AsmWriter

- 新增 `include/mips/AsmWriter.h` 和 `src/mips/AsmWriter.cpp`
- 所有 MIPS 文本输出统一经由 `AsmWriter` 发射，消除散落的 `os_ <<`
- 提供语义化接口：`EmitInsn`, `EmitLi`, `EmitLa`, `EmitSwSp`, `EmitLwSp`, `EmitAddiu`, `EmitSyscall`, `EmitLabel`, `EmitDirective` 等
- `MipsEmitter` 和 `FunctionEmitter` 均改为持有 `AsmWriter&`

### S3 ✅ 提取 StackFrame

- 新增 `include/mips/StackFrame.h` 和 `src/mips/StackFrame.cpp`
- `BuildStackFrame()` 的全部逻辑封装在 `StackFrame::Build()` 中
- `FunctionEmitter` 持有 `StackFrame frame_` 成员，通过 `GetOffset()` / `GetFrameSize()` 访问

### S4 ✅ 提取 InstructionEmitter

- 新增 `include/mips/InstructionEmitter.h` 和 `src/mips/InstructionEmitter.cpp`
- 全部指令选择逻辑（`EmitBinaryInst`、`EmitLoadInst`、…、`EmitCallInst`、`EmitLibraryCall`）从 `FunctionEmitter` 搬入
- `LoadValueToReg` 和 `EmitPhiMovesForEdge`（原 `EmitPhiMovesBeforeBranch`）一并迁入
- `GepElementSizeBytes` 从 `FunctionEmitter.cpp` 匿名命名空间移入 `InstructionEmitter.cpp` 匿名命名空间
- 对外只暴露 `Emit(inst, block)` 和 `EmitPhiMovesForEdge(pred, branch)` 两个公有方法
- Epilogue（`lw $ra` / `addiu $sp` / `jr $ra`）随 ret 指令就地在 `EmitEpilogue()` 私有方法中发射

### S5 ✅ FunctionEmitter 完全瘦身

- `FunctionEmitter.h`：43 行 → 38 行；只暴露 `Emit()` 一个公有方法，无 `EmitXxxInst` 声明
- `FunctionEmitter.cpp`：406 行 → 51 行；只剩 `Emit()` / `EmitPrologue()` / `EmitBody()` 三段编排逻辑
- 内部持有 `StackFrame frame_` 和 `InstructionEmitter inst_emitter_`，完全委托

### S6 ✅ 新增 MipsOptions

- 新增 `include/mips/MipsOptions.h`（header-only）
- `MipsEmitter` 构造函数接收 `const MipsOptions& options = {}`，透传给 `FunctionEmitter`
- 包含 `emit_comments`、`enable_reg_alloc`、`enable_peephole`、`enable_mul_div_opt` 四个开关，默认全关
- `main.cpp` 已构造 `MipsOptions` 并传入 `MipsEmitter`

### S7 ✅ 新增 ValueLocation（存根）

- 新增 `include/mips/ValueLocation.h`（header-only）
- `StackFrame::GetLocation(val)` 返回 `ValueLocation`（初版全为 `STACK`）
- `InstructionEmitter::LoadValueToReg` 对一般栈槽改为基于 `ValueLocation::kind` 分支处理
- 当前全走 `STACK` 分支，行为与重构前完全一致；`REGISTER` 分支为寄存器分配预留

### S8 ✅ 代码清理

- `EmitBody` 中的 `if` 链已改为 `if-else-if` 链（在 S4 实施时同步完成）
- `GepElementSizeBytes` 已从 `FunctionEmitter.cpp` 迁移至 `InstructionEmitter.cpp`
- `MipsEmitter.cpp` 全面改用 `AsmWriter`，不再直接写 `os_ <<`
- 所有注释统一为英文，删除中文注释

---

## 二、最终文件结构

```
include/mips/
    MipsCommon.h           # 共享常量与标签生成（GlobalLabel, BlockLabel, IsLibraryFunction）
    MipsOptions.h          # 编译选项 / 优化开关（header-only）
    ValueLocation.h        # Value 位置抽象，STACK / REGISTER（header-only）
    AsmWriter.h            # MIPS 汇编输出助手
    StackFrame.h           # 栈帧布局计算（含 GetLocation() → ValueLocation）
    InstructionEmitter.h   # 指令选择，对外只暴露 Emit() / EmitPhiMovesForEdge()
    FunctionEmitter.h      # 瘦身编排者：Emit() 为唯一公有接口
    MipsEmitter.h          # 顶层驱动，持有 AsmWriter，接收 MipsOptions

src/mips/
    MipsCommon.cpp
    AsmWriter.cpp
    StackFrame.cpp
    InstructionEmitter.cpp  # 核心指令选择逻辑（~270 行）
    FunctionEmitter.cpp     # 编排逻辑（~50 行）
    MipsEmitter.cpp         # 顶层驱动（~120 行）
```

---

## 三、模块依赖关系（已实现）

```
ValueLocation (no deps)
MipsOptions   (no deps)
MipsCommon    (no deps)
    │
    ├──> AsmWriter        (ostream)
    │
    ├──> StackFrame       (ir::Function, ValueLocation)
    │
    └──> InstructionEmitter
             │  (AsmWriter, StackFrame, MipsCommon, ir::*)
             │
          FunctionEmitter
             │  (AsmWriter, StackFrame, InstructionEmitter, MipsOptions)
             │
          MipsEmitter
                (AsmWriter, MipsOptions, ir::Module)
```

---

## 四、优化预留接入点

| 优化 | 接入位置 | 触发条件 |
|------|---------|---------|
| 寄存器分配 | `StackFrame::GetLocation()` 改为返回 `REGISTER` | `options_.enable_reg_alloc` |
| 窥孔优化 | `AsmWriter` 内部增加缓冲 + Flush 后处理 | `options_.enable_peephole` |
| 乘除强度削减 | `InstructionEmitter::EmitBinaryInst` | `options_.enable_mul_div_opt` |

---

## 五、验证结果

- 编译通过，无警告
- 对当前 `testfile.txt` 生成的 `mips.txt` 与重构前完全一致（`diff` 为空）

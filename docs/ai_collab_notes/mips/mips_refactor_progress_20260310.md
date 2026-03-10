# MIPS 后端模块化重构进度记录

**日期**: 2026-03-10
**参考计划**: `mips_backend_modular_refactor_plan_20260310.md`

---

## 一、已完成的步骤

### S1 ✅ 提取 MipsCommon

- 新增 `include/mips/MipsCommon.h` 和 `src/mips/MipsCommon.cpp`
- `GlobalLabel()`、`BlockLabel()`、`kIndent` 常量从两个文件的匿名命名空间合并到此模块
- 新增 `IsLibraryFunction()` 集中判断库函数
- `MipsEmitter.cpp` 和 `FunctionEmitter.cpp` 均已改为 `#include "mips/MipsCommon.h"`，不再各自重复定义

### S3 ✅ 提取 StackFrame

- 新增 `include/mips/StackFrame.h` 和 `src/mips/StackFrame.cpp`
- `BuildStackFrame()` 的全部逻辑已搬入 `StackFrame::Build()`
- `FunctionEmitter` 改为持有 `StackFrame frame_` 成员
- `frame_.GetOffset(val)` / `frame_.GetFrameSize()` 已替换原来的直接访问

### S6 ✅ 新增 MipsOptions

- 新增 `include/mips/MipsOptions.h`（header-only）
- `MipsEmitter` 构造函数已接收 `const MipsOptions& options = {}`
- 包含 `emit_comments`、`enable_reg_alloc`、`enable_peephole`、`enable_mul_div_opt` 四个开关

### S8（部分）✅ if → if-else-if 链

- `FunctionEmitter::EmitInstruction()` 中的分发逻辑已从 `if` 链改为 `if-else-if` 链

### 接口收紧（计划 S5 的前置）✅

- `FunctionEmitter.h` 中所有 `EmitXxxInst` 方法已改为 `private`，对外只暴露 `Emit()`

---

## 二、尚未完成的步骤

### S2 ❌ 提取 AsmWriter

- 当前状态：`FunctionEmitter` 和 `MipsEmitter` 仍直接使用裸 `os_ <<` 散落输出
- 需要：新增 `include/mips/AsmWriter.h` + `src/mips/AsmWriter.cpp`，将所有 MIPS 文本输出经由 `AsmWriter` 封装
- 这是窥孔优化（缓冲 + 后处理）的关键基础设施

### S4 ❌ 提取 InstructionEmitter

- 当前状态：所有 `EmitBinaryInst` / `EmitLoadInst` / … / `LoadValueToReg` / `EmitPhiMovesBeforeBranch` 仍在 `FunctionEmitter.cpp` 中
- 需要：新增 `include/mips/InstructionEmitter.h` + `src/mips/InstructionEmitter.cpp`，将指令选择逻辑完整搬出

### S5 ❌ FunctionEmitter 完全瘦身

- 当前状态：`FunctionEmitter.h` 虽然接口已收紧（方法均为 private），但 `.cpp` 仍约 400 行
- 需要：在 S4 完成后，`FunctionEmitter.cpp` 应瘦身至约 60 行，只剩 `Emit()` / `EmitPrologue()` / `EmitBody()` / `EmitEpilogue()` 的编排逻辑

### S7 ❌ 新增 ValueLocation（存根）

- 当前状态：尚未创建 `ValueLocation.h`
- 需要：新增 `include/mips/ValueLocation.h`（header-only），`StackFrame::GetLocation()` 返回 `ValueLocation`（初版全为 `kStack`），`LoadValueToReg` 改为基于 `ValueLocation` 分支

### S8（剩余）❌ 代码清理

- `GepElementSizeBytes()` 目前还在 `FunctionEmitter.cpp` 的匿名命名空间，应移入 `InstructionEmitter` 私有方法（等 S4 完成后顺带）
- `MipsEmitter.cpp` 还在直接使用 `os_ <<`，等 S2 完成后迁移

---

## 三、模块完成状态速览

| 模块 | 计划步骤 | 状态 |
|------|---------|------|
| MipsCommon | S1 | ✅ 完成 |
| MipsOptions | S6 | ✅ 完成 |
| StackFrame | S3 | ✅ 完成 |
| AsmWriter | S2 | ❌ 未开始 |
| InstructionEmitter | S4 | ❌ 未开始 |
| FunctionEmitter 瘦身 | S5 | ❌ 未开始（待 S4） |
| ValueLocation | S7 | ❌ 未开始 |
| if-else-if 清理 | S8 | ✅ 完成 |
| GepElementSizeBytes 迁移 | S8 | ❌ 待 S4 |

---

## 四、建议的下一步顺序

1. **S2**：先做 `AsmWriter`，因为后续 S4/S5 都要用它
2. **S4**：提取 `InstructionEmitter`，顺带迁移 `GepElementSizeBytes`
3. **S5**：FunctionEmitter 完全瘦身（S4 完成后很自然地推进）
4. **S7**：加入 `ValueLocation` 存根，完成寄存器分配的预留接口
5. **S8 剩余**：整理 `MipsEmitter.cpp` 中的裸 `os_` 输出

每步完成后 diff 验证生成的 mips.txt 与重构前完全一致。

# MIPS 后端模块化重构方案

**日期**: 2026-03-10  
**阶段**: MIPS 初版完成 → 优化前的代码整理  
**目标**: 在不改变语义的前提下，将现有 MIPS 后端代码重构为清晰的模块结构，并为后续优化（寄存器分配、窥孔优化等）预留友好接口  
**前置状态**: 初版 MIPS 代码生成已通过全部测试样例

---

## 一、现状分析

### 1.1 当前文件结构

```
include/mips/
    MipsEmitter.h          # 21 行，接口简洁
    FunctionEmitter.h      # 43 行，所有方法公有暴露

src/mips/
    MipsEmitter.cpp        # 121 行，.data/.text 段生成
    FunctionEmitter.cpp    # 436 行，所有逻辑集中
```

### 1.2 具体问题清单

#### P1：代码重复

| 重复项 | MipsEmitter.cpp | FunctionEmitter.cpp |
|--------|----------------|---------------------|
| `k_indent` 常量 | ✅ 第 11 行 | ✅ 第 13 行 |
| `GlobalLabel()` 函数 | ✅ 第 13–18 行 | ✅ 第 35–40 行 |

两个文件各自在匿名命名空间中定义了完全相同的辅助函数和常量，违反 DRY 原则。

#### P2：FunctionEmitter 职责过重（God Class 倾向）

`FunctionEmitter` 一个类承担了 **5 项不同职责**：

1. **栈帧计算** — `BuildStackFrame()`：扫描 IR 指令，分配 `value_offset_`，计算 `frame_size_`
2. **函数序言/尾声** — `EmitPrologue()` / `EmitEpilogue()`：\$sp 调整、\$ra 保存/恢复、参数入栈
3. **指令选择** — 12 个 `EmitXxxInst()` 方法：将每种 IR 指令映射为 MIPS 序列
4. **调用约定** — `EmitCallInst()` / `EmitLibraryFunctionCall()`：参数传递、syscall、返回值处理
5. **Phi 下降** — `EmitPhiMovesBeforeBranch()`：依赖拓扑排序 + 移动发射

当前仅 436 行尚可管理，但后续加入寄存器分配后会急剧膨胀。更关键的是，不同职责之间的边界不清晰，增加了维护和测试的难度。

#### P3：EmitBody 中的 if 链而非 if-else-if

```cpp
// 当前代码：每条指令被所有 dynamic_cast 试一遍
if (auto* bin = ...) { EmitBinaryInst(bin); }
if (auto* ret = ...) { EmitReturnInst(ret); }  // 不是 else if！
if (auto* load = ...) { EmitLoadInst(load); }
// ...
```

虽然语义正确（一条指令只会匹配一个类型），但每条指令要执行 ~12 次 `dynamic_cast`，既浪费又不符合"互斥分支"的语义意图。

#### P4：FunctionEmitter 接口暴露过多

头文件中所有 `EmitXxxInst` 方法都是 `public`，但它们只在 `EmitBody()` 内部调用，不应暴露给外部（`MipsEmitter` 只调用 `BuildStackFrame` / `EmitPrologue` / `EmitBody`）。

#### P5：缺少优化接口预留

- 没有 MIPS 层面的 Pass 基础设施
- 没有统一的编译选项 / 优化开关配置
- `value_offset_` 的"全栈分配"策略硬编码，无法被寄存器分配器替换
- 没有"Value 位置"的抽象——目前写死为栈偏移，后续需要区分"在寄存器"还是"在栈上"

#### P6：GEP 元素大小计算散落

`GepElementSizeBytes()` 在 FunctionEmitter.cpp 的匿名命名空间中，是一个纯 IR 层面的类型查询，不应与 MIPS 发射逻辑耦合。

---

## 二、重构目标与原则

### 2.1 目标

1. **消除代码重复**：公共工具提取到共享模块
2. **职责分离**：将 FunctionEmitter 拆分为若干单一职责模块
3. **收紧接口**：只暴露外部需要的方法
4. **预留优化插槽**：为寄存器分配、窥孔优化等提供清晰的接入点
5. **可开关优化**：通过统一配置结构控制各优化 Pass 的启用/禁用

### 2.2 原则

- **语义不变**：重构前后生成的 MIPS 汇编文本完全一致（可 diff 验证）
- **增量推进**：每一步重构后编译 + 回归测试，不做大爆炸式改动
- **命名规范统一**：所有新模块沿用现有 `mips::` 命名空间、`PascalCase` 公有方法、`snake_case_` 私有成员

---

## 三、重构后的模块设计

### 3.1 模块总览

```
include/mips/
    MipsCommon.h           # [新] 共享常量与标签生成
    MipsOptions.h          # [新] 编译选项 / 优化开关
    StackFrame.h           # [新] 栈帧布局计算
    ValueLocation.h        # [新] Value 位置抽象（栈/寄存器）
    AsmWriter.h            # [新] MIPS 汇编输出助手
    InstructionEmitter.h   # [新] 指令选择（从 FunctionEmitter 拆出）
    FunctionEmitter.h      # [改] 瘦身：编排者角色
    MipsEmitter.h          # [改] 接收 MipsOptions

src/mips/
    MipsCommon.cpp
    StackFrame.cpp
    AsmWriter.cpp
    InstructionEmitter.cpp
    FunctionEmitter.cpp    # 大幅瘦身
    MipsEmitter.cpp        # 小改
```

### 3.2 模块详细设计

---

#### 模块 A：MipsCommon — 共享工具

**职责**：提供标签生成、常量定义等跨模块共用的工具函数。

```cpp
// include/mips/MipsCommon.h
#pragma once
#include <string>

namespace mips {

inline constexpr const char* kIndent = "    ";

std::string GlobalLabel(const std::string& ir_name);
std::string BlockLabel(const std::string& func_name, const std::string& block_name);
std::string FuncLabel(const std::string& func_name);

bool IsLibraryFunction(const std::string& name);

} // namespace mips
```

**要点**：
- `GlobalLabel` / `BlockLabel` 从两个文件的匿名命名空间中提取合并
- `IsLibraryFunction` 集中判断（目前散布在 EmitCallInst 的字符串比较中）
- 纯头文件 + 小 cpp，无状态

---

#### 模块 B：MipsOptions — 编译选项与优化开关

**职责**：用一个结构体集中管理所有 MIPS 后端的编译选项和优化开关。

```cpp
// include/mips/MipsOptions.h
#pragma once

namespace mips {

struct MipsOptions {
    // 基本选项
    bool emit_comments = false;   // 在 MIPS 输出中插入注释（调试用）

    // 优化开关（后续逐步添加）
    bool enable_reg_alloc  = false;  // 寄存器分配（图着色）
    bool enable_peephole   = false;  // 窥孔优化
    bool enable_mul_div_opt = false; // 乘除优化（强度削减）
    bool enable_const_prop = false;  // MIPS 级常量传播
};

} // namespace mips
```

**要点**：
- 在 `main.cpp` 中构造 `MipsOptions`，传入 `MipsEmitter`
- 每个优化 Pass 检查对应开关，决定是否执行
- 后续新增优化只需在此结构体加一个 `bool`
- 未来可以从命令行参数、配置文件或宏控制

---

#### 模块 C：StackFrame — 栈帧布局

**职责**：封装从 `BuildStackFrame()` 提取出的栈帧计算逻辑，管理 Value 到栈偏移的映射。

```cpp
// include/mips/StackFrame.h
#pragma once
#include "ir/Function.h"
#include "ir/Instruction.h"
#include <unordered_map>

namespace mips {

class StackFrame {
public:
    explicit StackFrame(const ir::Function& func);

    void Build();

    int GetFrameSize() const { return frame_size_; }
    int GetOffset(const ir::Value* val) const;
    bool HasSlot(const ir::Value* val) const;

private:
    const ir::Function& func_;
    int frame_size_ = 0;
    std::unordered_map<const ir::Value*, int> value_offset_;

    void AllocateAllocas();
    void AllocateInstructionSlots();
    void AllocateArgumentSlots();
};

} // namespace mips
```

**要点**：
- 独立于输出流（不依赖 `std::ostream`）
- 纯计算逻辑，可单独测试
- `GetOffset` 替代原来的 `value_offset_[val]` 直接访问
- 后续寄存器分配器可以继承或组合 `StackFrame`，只对分配到寄存器的 Value 不分配栈槽

---

#### 模块 D：ValueLocation — Value 位置抽象（为优化预留）

**职责**：抽象一个 IR Value 在 MIPS 层面的"位置"——当前只有栈偏移，未来扩展为寄存器。

```cpp
// include/mips/ValueLocation.h
#pragma once
#include <string>

namespace mips {

struct ValueLocation {
    enum Kind { kStack, kRegister };

    Kind kind;
    int stack_offset;        // kind == kStack 时有效
    std::string reg_name;    // kind == kRegister 时有效（如 "$s0"）

    static ValueLocation OnStack(int offset) {
        return {kStack, offset, {}};
    }
    static ValueLocation InRegister(const std::string& reg) {
        return {kRegister, 0, reg};
    }
};

} // namespace mips
```

**要点**：
- 初版中所有 Value 都是 `kStack`，行为与现在完全一致
- 当寄存器分配器上线后，部分 Value 变为 `kRegister`
- `LoadValueToReg` 和 `StoreRegToValue` 根据 `ValueLocation::kind` 分支处理
- 这是连接"全栈分配"和"寄存器分配"的关键桥梁

---

#### 模块 E：AsmWriter — MIPS 汇编输出助手

**职责**：封装 `std::ostream&` 的写操作，提供语义化的发射接口。

```cpp
// include/mips/AsmWriter.h
#pragma once
#include <ostream>
#include <string>

namespace mips {

class AsmWriter {
public:
    explicit AsmWriter(std::ostream& os) : os_(os) {}

    // 标签
    void EmitLabel(const std::string& label);

    // R/I/J 型指令
    void EmitR(const std::string& op, const std::string& rd,
               const std::string& rs, const std::string& rt);
    void EmitI(const std::string& op, const std::string& rt,
               const std::string& rs, int imm);
    void EmitI(const std::string& op, const std::string& rt,
               int offset, const std::string& base);  // lw/sw 格式
    void EmitJ(const std::string& op, const std::string& label);

    // 伪指令
    void EmitLi(const std::string& reg, int64_t value);
    void EmitLa(const std::string& reg, const std::string& label);
    void EmitMove(const std::string& dst, const std::string& src);
    void EmitSyscall();

    // 指示符
    void EmitDirective(const std::string& directive);  // .data, .text 等
    void EmitWord(int64_t value);
    void EmitWordList(const std::vector<int64_t>& values);
    void EmitAsciiz(const std::string& str);
    void EmitSpace(int bytes);

    // 注释（受 MipsOptions::emit_comments 控制）
    void EmitComment(const std::string& comment);

    // 空行
    void EmitBlankLine();

    // 原始写入（过渡期兼容，逐步消除）
    std::ostream& Raw() { return os_; }

private:
    std::ostream& os_;
};

} // namespace mips
```

**要点**：
- 所有 MIPS 文本输出都经过 `AsmWriter`，杜绝散落的 `os_ <<`
- 格式化（对齐、缩进）集中管理，保证输出风格统一
- 后续窥孔优化可以在 `AsmWriter` 层面缓冲指令，先收集后优化再输出
- `Raw()` 方法用于过渡期，最终应消除

---

#### 模块 F：InstructionEmitter — 指令选择

**职责**：纯指令选择逻辑——给定一条 IR 指令，生成对应的 MIPS 指令序列。

```cpp
// include/mips/InstructionEmitter.h
#pragma once
#include "ir/Instruction.h"
#include "mips/AsmWriter.h"
#include "mips/StackFrame.h"

namespace mips {

class InstructionEmitter {
public:
    InstructionEmitter(AsmWriter& writer, const StackFrame& frame,
                       const ir::Function& func);

    void Emit(const ir::Instruction* inst);

    void EmitPhiMovesForEdge(const ir::BasicBlock* pred,
                             const ir::BasicBlock* succ);

private:
    AsmWriter& writer_;
    const StackFrame& frame_;
    const ir::Function& func_;

    void EmitBinaryInst(const ir::BinaryInst* inst);
    void EmitLoadInst(const ir::LoadInst* inst);
    void EmitStoreInst(const ir::StoreInst* inst);
    void EmitBranchInst(const ir::BranchInst* inst,
                        const ir::BasicBlock* current_block);
    void EmitReturnInst(const ir::ReturnInst* inst);
    void EmitGetElementPtrInst(const ir::GetElementPtrInst* inst);
    void EmitIcmpInst(const ir::IcmpInst* inst);
    void EmitZextInst(const ir::ZextInst* inst);
    void EmitTruncInst(const ir::TruncInst* inst);
    void EmitCallInst(const ir::CallInst* inst);
    void EmitLibraryCall(const ir::CallInst* inst);

    // Value 到寄存器的加载（判断 ConstantInt / GlobalVar / Alloca / 栈槽）
    void LoadValueToReg(const ir::Value* val, const std::string& reg);
};

} // namespace mips
```

**要点**：
- 所有 `EmitXxxInst` 方法变为 `private`
- 对外只暴露 `Emit(const ir::Instruction*)` 和 `EmitPhiMovesForEdge`
- 不再持有 `os_`，而是通过 `AsmWriter` 发射
- 不再持有 `value_offset_`，而是通过 `StackFrame` 查询
- `EmitBranchInst` 接收当前块指针，内部处理 phi moves + 跳转

---

#### 模块 G：FunctionEmitter — 瘦身后的编排者

**职责**：组装以上模块，按 prologue → body → (epilogue via return) 的顺序驱动一个函数的代码生成。

```cpp
// include/mips/FunctionEmitter.h（重构后）
#pragma once
#include "ir/Function.h"
#include "mips/AsmWriter.h"
#include "mips/MipsOptions.h"
#include "mips/StackFrame.h"
#include "mips/InstructionEmitter.h"

namespace mips {

class FunctionEmitter {
public:
    FunctionEmitter(AsmWriter& writer, const ir::Function& func,
                    const MipsOptions& options);
    void Emit();

private:
    AsmWriter& writer_;
    const ir::Function& func_;
    const MipsOptions& options_;

    StackFrame frame_;
    InstructionEmitter inst_emitter_;

    void EmitPrologue();
    void EmitBody();
    void EmitEpilogue();
};

} // namespace mips
```

**要点**：
- 从 43 行 / 436 行 瘦身为 ~25 行头文件 + ~60 行实现
- 不再直接持有 `value_offset_`、`frame_size_` —— 委托给 `StackFrame`
- 不再直接持有 12 个 `EmitXxxInst` —— 委托给 `InstructionEmitter`
- `Emit()` 是唯一公有方法：`frame_.Build()` → `EmitPrologue()` → `EmitBody()` → done

---

#### 模块 H：MipsEmitter — 顶层驱动（小改）

```cpp
// include/mips/MipsEmitter.h（重构后）
#pragma once
#include "ir/Module.h"
#include "mips/AsmWriter.h"
#include "mips/MipsOptions.h"

namespace mips {

class MipsEmitter {
public:
    MipsEmitter(std::ostream& os, const ir::Module& module,
                const MipsOptions& options = {});
    void Emit();

private:
    AsmWriter writer_;
    const ir::Module& module_;
    const MipsOptions& options_;

    void EmitDataSegment();
    void EmitTextSegment();
};

} // namespace mips
```

**要点**：
- 接收 `MipsOptions`，默认值保持现有行为（全部优化关闭）
- 持有 `AsmWriter` 而非裸 `ostream`
- `EmitDataSegment` 使用 `AsmWriter` 的指示符方法

---

### 3.3 模块依赖关系

```
MipsOptions ───────────────────────────────┐
    │                                      │
MipsCommon (无依赖，纯工具)                  │
    │                                      │
    ├──> AsmWriter (依赖 ostream)           │
    │       │                              │
    ├──> StackFrame (依赖 ir::Function)     │
    │       │                              │
    ├──> ValueLocation (无依赖，纯数据)      │
    │       │                              │
    └──> InstructionEmitter                │
            │  (依赖 AsmWriter, StackFrame, │
            │   MipsCommon, ir::*)          │
            │                              │
         FunctionEmitter                   │
            │  (组合 StackFrame,            │
            │   InstructionEmitter,         │
            │   AsmWriter)                 │
            │                              │
         MipsEmitter ──────────────────────┘
            (驱动 FunctionEmitter,
             持有 AsmWriter, MipsOptions)
```

依赖方向清晰、无环。每个模块可以独立编译和测试。

---

## 四、优化预留设计

### 4.1 优化插槽位置

后续优化按作用层级分为两类：

#### IR 层优化（在 MIPS 生成之前）

在 `main.cpp` 中 Mem2Reg 之后、MipsEmitter 之前插入：

```
Mem2Reg → [常量折叠] → [死代码消除] → [函数内联] → MipsEmitter
```

每个优化实现为 `pass::FunctionPass` 子类，复用现有 `Pass.h` 基础设施。

#### MIPS 层优化（在代码生成过程中或之后）

| 优化 | 插入位置 | 接口 |
|------|---------|------|
| **寄存器分配** | 替换 `StackFrame` 的分配策略 | `StackFrame` → `RegAllocFrame`（子类或策略模式） |
| **窥孔优化** | `AsmWriter` 内部缓冲 + 后处理 | `AsmWriter::Flush()` 前执行窥孔 Pass |
| **乘除优化** | `InstructionEmitter::EmitBinaryInst` 内 | 检查 `options_.enable_mul_div_opt` |
| **常量传播** | `InstructionEmitter::LoadValueToReg` 内 | 检查操作数是否可折叠为立即数 |

### 4.2 寄存器分配预留（关键设计）

当前 `LoadValueToReg` 总是从栈上 `lw`。引入 `ValueLocation` 后：

```cpp
void InstructionEmitter::LoadValueToReg(const ir::Value* val,
                                         const std::string& reg) {
    if (auto* ci = dynamic_cast<const ir::ConstantInt*>(val)) {
        writer_.EmitLi(reg, ci->GetValue());
        return;
    }
    // ... GlobalVar, AllocaInst 等特殊情况

    ValueLocation loc = frame_.GetLocation(val);
    if (loc.kind == ValueLocation::kRegister) {
        if (loc.reg_name != reg) {
            writer_.EmitMove(reg, loc.reg_name);
        }
    } else {
        writer_.EmitI("lw", reg, loc.stack_offset, "$sp");
    }
}
```

初版中 `ValueLocation` 全部是 `kStack`，行为与现在完全一致。当寄存器分配器启用后，部分 Value 的 `ValueLocation` 变为 `kRegister`，自动生成 `move` 而非 `lw`。

### 4.3 窥孔优化预留

`AsmWriter` 可以扩展为支持缓冲模式：

```cpp
class AsmWriter {
public:
    void SetBufferMode(bool enabled);  // 开启后指令暂存不写
    void Flush();                       // 执行窥孔优化后输出
    // ...
};
```

窥孔规则示例：
- `sw $t0, X($sp)` 紧接 `lw $t0, X($sp)` → 删除 load（冗余加载消除）
- `li $t0, 0` + `addu $t1, $t2, $t0` → `move $t1, $t2`
- `move $t0, $t0` → 删除

### 4.4 优化开关使用方式

在 `main.cpp` 中：

```cpp
mips::MipsOptions mips_opts;
mips_opts.enable_reg_alloc  = kEnableRegAlloc;
mips_opts.enable_peephole   = kEnablePeephole;
mips_opts.enable_mul_div_opt = kEnableMulDivOpt;
mips_opts.emit_comments     = kEmitMipsComments;

mips::MipsEmitter emitter(mips_out, *result.module, mips_opts);
emitter.Emit();
```

所有优化可以通过 `const bool kEnableXxx = true/false;` 随时开关，与现有 `kEnableMem2Reg` 风格一致。

---

## 五、实施路线图

### 步骤概览

| # | 步骤 | 改动范围 | 风险 | 验证方式 |
|---|------|---------|------|---------|
| S1 | 提取 MipsCommon | 新增文件 + 两处 include 替换 | 极低 | diff mips.txt |
| S2 | 提取 AsmWriter（渐进式） | 新增文件 + FunctionEmitter/MipsEmitter 逐步迁移 | 低 | diff mips.txt |
| S3 | 提取 StackFrame | 新增文件 + FunctionEmitter 委托 | 低 | diff mips.txt |
| S4 | 提取 InstructionEmitter | 新增文件 + FunctionEmitter 委托 | 中 | diff mips.txt |
| S5 | FunctionEmitter 瘦身 + 接口收紧 | 修改头文件 + 实现 | 低 | 编译 + diff |
| S6 | 新增 MipsOptions + 接入 main.cpp | 新增文件 + MipsEmitter 改签名 | 低 | 编译 |
| S7 | 新增 ValueLocation（存根） | 新增文件，StackFrame 返回类型升级 | 低 | diff mips.txt |
| S8 | if → if-else-if + 其他代码清理 | InstructionEmitter 内 | 极低 | diff mips.txt |

### 步骤详细说明

#### S1：提取 MipsCommon

1. 创建 `include/mips/MipsCommon.h` 和 `src/mips/MipsCommon.cpp`
2. 将 `GlobalLabel()`、`BlockLabel()`、`k_indent` 常量搬入
3. 新增 `IsLibraryFunction()` 和 `FuncLabel()`
4. `MipsEmitter.cpp` 和 `FunctionEmitter.cpp` 删除各自匿名命名空间中的重复定义，改为 `#include "mips/MipsCommon.h"`
5. 更新 CMakeLists.txt（如果用 GLOB 则自动包含）

**验证**：重新编译，对同一 testfile.txt 生成的 mips.txt 做 diff，确认完全一致。

#### S2：提取 AsmWriter

1. 创建 `include/mips/AsmWriter.h` 和 `src/mips/AsmWriter.cpp`
2. 先实现最常用的方法（`EmitLabel`, `EmitLi`, `EmitR`, `EmitI` 等）
3. **渐进迁移**：先在 FunctionEmitter 中创建 `AsmWriter` 实例并逐方法替换 `os_ <<`，每替换一批就 diff 验证
4. MipsEmitter 的 `.data` 段输出同理迁移

**验证**：每批替换后 diff mips.txt。

#### S3：提取 StackFrame

1. 创建 `include/mips/StackFrame.h` 和 `src/mips/StackFrame.cpp`
2. 将 `BuildStackFrame()` 的全部逻辑搬入 `StackFrame::Build()`
3. FunctionEmitter 改为持有 `StackFrame frame_` 成员
4. 所有 `value_offset_[xxx]` 替换为 `frame_.GetOffset(xxx)`
5. 所有 `frame_size_` 替换为 `frame_.GetFrameSize()`

**验证**：diff mips.txt。

#### S4：提取 InstructionEmitter

1. 创建 `include/mips/InstructionEmitter.h` 和 `src/mips/InstructionEmitter.cpp`
2. 将所有 `EmitXxxInst` 方法、`LoadValueToReg`、`EmitPhiMovesBeforeBranch` 搬入
3. `InstructionEmitter` 构造时接收 `AsmWriter&` 和 `const StackFrame&`
4. FunctionEmitter::EmitBody() 简化为遍历 BB → 遍历 Instruction → 调用 `inst_emitter_.Emit(inst)`

**验证**：diff mips.txt。

#### S5：FunctionEmitter 瘦身

1. 删除 FunctionEmitter.h 中所有 `EmitXxxInst` 的声明
2. `Emit()` 成为唯一公有方法
3. 私有方法只剩 `EmitPrologue()` / `EmitBody()` / `EmitEpilogue()`

**验证**：编译通过 + diff mips.txt。

#### S6：新增 MipsOptions

1. 创建 `include/mips/MipsOptions.h`
2. `MipsEmitter` 构造函数新增 `const MipsOptions& options = {}` 参数
3. `main.cpp` 中新增优化开关常量，构造 `MipsOptions` 传入
4. 各模块通过 const 引用接收 `MipsOptions`，初版不使用任何开关

**验证**：编译通过，行为不变。

#### S7：新增 ValueLocation

1. 创建 `include/mips/ValueLocation.h`
2. `StackFrame::GetLocation()` 返回 `ValueLocation`（初版全部为 `kStack`）
3. `InstructionEmitter::LoadValueToReg` 改为基于 `ValueLocation` 分支

**验证**：diff mips.txt（初版全是 kStack，行为不变）。

#### S8：代码清理

1. EmitBody 中的 `if` 链改为 `if-else if` 链
2. `GepElementSizeBytes` 移入 `MipsCommon` 或 `InstructionEmitter` 的私有方法
3. 移除不必要的 `#include`，整理头文件依赖
4. 统一注释风格

**验证**：编译 + diff mips.txt。

---

## 六、重构前后对比

### 6.1 代码行数预估

| 模块 | 头文件 | 实现文件 | 说明 |
|------|--------|---------|------|
| MipsCommon | ~25 | ~30 | 纯工具 |
| MipsOptions | ~20 | 0 (header-only) | 配置结构 |
| ValueLocation | ~25 | 0 (header-only) | 数据结构 |
| AsmWriter | ~45 | ~80 | 输出封装 |
| StackFrame | ~30 | ~80 | 栈帧计算 |
| InstructionEmitter | ~40 | ~280 | 指令选择（核心） |
| FunctionEmitter | ~25 | ~50 | 编排者 |
| MipsEmitter | ~20 | ~90 | 顶层驱动 |
| **合计** | **~230** | **~610** | |

重构前：头文件 64 行，实现 557 行，共 621 行。  
重构后：头文件 ~230 行，实现 ~610 行，共 ~840 行。

行数增长约 35%，主要来自模块边界（头文件声明、构造函数参数传递）和新增的 `AsmWriter` / `ValueLocation` / `MipsOptions` 基础设施。这些增长换来的是：

- 每个文件的职责单一、可独立理解
- 优化插入点明确，不需要在大文件中定位
- 后续加入寄存器分配等优化时，只改动局部模块

### 6.2 调用流程对比

**重构前**：

```
main.cpp
 └─ MipsEmitter::Emit()
     ├─ EmitDataSegment()        # 直接 os_ << ...
     └─ EmitTextSegment()
         └─ FunctionEmitter      # 手动调用 Build + Prologue + Body
             ├─ BuildStackFrame()
             ├─ EmitPrologue()
             └─ EmitBody()
                 ├─ EmitBinaryInst()
                 ├─ EmitLoadInst()
                 ├─ ...（12 个 if 分支）
                 └─ EmitPhiMovesBeforeBranch()
```

**重构后**：

```
main.cpp
 └─ MipsEmitter::Emit(options)
     ├─ EmitDataSegment()        # 通过 AsmWriter
     └─ EmitTextSegment()
         └─ FunctionEmitter::Emit()
             ├─ StackFrame::Build()
             ├─ EmitPrologue()           # 通过 AsmWriter
             └─ EmitBody()
                 └─ InstructionEmitter::Emit(inst)
                     ├─ EmitBinaryInst()     # 通过 AsmWriter + StackFrame
                     ├─ EmitLoadInst()
                     ├─ ...（if-else-if 链）
                     └─ EmitPhiMovesForEdge()
```

---

## 七、回归测试策略

### 7.1 diff 验证

每个步骤完成后，对**所有已有测试用例**执行以下流程：

```bash
# 备份重构前的 mips.txt
cp mips.txt mips_before.txt

# 重新编译运行
cd build && cmake --build . && cd ..
./build/Compiler

# 比对
diff mips_before.txt mips.txt
```

diff 结果必须为空（即输出完全一致）。

### 7.2 MARS 执行验证

对核心样例（fib_rec、fib_iter、char 数组、多参数函数调用等）在 MARS 4.5 中执行，确认输出不变。

### 7.3 自动化建议

后续可编写一个简单的 shell 脚本遍历 `SysY_Test_2024/` 下的所有测试用例，自动比对输出。

---

## 八、风险与缓解

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| 重构引入隐蔽 bug | 生成的 MIPS 错误 | 每步 diff + MARS 验证 |
| 模块边界传参过多 | 代码冗余感增加 | 控制模块数量，不过度拆分 |
| AsmWriter 输出格式与原版不一致 | diff 失败 | AsmWriter 的格式化严格对齐原版（空格数、换行位置） |
| ValueLocation 引入后 LoadValueToReg 分支增多 | 初版复杂度增加 | 初版 ValueLocation 全为 kStack，新分支实际不执行 |

---

## 九、总结

本次重构的核心是 **"职责分离 + 优化预留"**：

1. **MipsCommon**：消除重复，统一工具
2. **StackFrame**：栈帧逻辑独立，为寄存器分配留接口
3. **AsmWriter**：输出集中管理，为窥孔优化留缓冲区
4. **InstructionEmitter**：指令选择内聚，接口收紧
5. **FunctionEmitter**：从 God Class 变为轻量编排者
6. **MipsOptions**：统一优化开关，一处控制全局
7. **ValueLocation**：Value 位置抽象，连接全栈到寄存器分配的桥梁

每一步都可以单独完成和验证，不存在"必须一次性全改"的风险。重构完成后，后端代码将具备清晰的层次结构，后续任何优化都可以在对应模块中自然接入。

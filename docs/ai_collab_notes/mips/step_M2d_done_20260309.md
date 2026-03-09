# M2d 全局 int 数组 — 完成说明

## 一、实现概要

M2d 在 M2a–c（标量全局 + load/store）基础上，支持**全局 int 数组**的 .data 发射与通过 GEP 的 load/store，并用统一 API 区分标量与数组全局。

---

## 二、新增/修改内容

### 1. GlobalVar 判断“是否为数组”的 API（建议保留）

**问题**：原先在 MipsEmitter 里用 `global->GetType()->GetPointeeType()` 判断数组，但 `Type` 基类没有 `GetPointeeType()`，且类型判断分散在多处。

**做法**：在 `GlobalVar` 上增加两个接口，把“是否数组”和“取数组类型”集中在一处：

- **`bool GlobalVar::IsArray() const`**
  当且仅当该全局的类型为“指针且 pointee 为 `ArrayType`”时返回 true。

- **`ArrayType* GlobalVar::GetArrayType() const`**
  若为数组全局则返回 pointee 的 `ArrayType*`，否则返回 nullptr。

**实现位置**：
- 声明：`include/ir/GlobalVar.h`（含 `class ArrayType;` 前向声明）
- 实现：`src/ir/GlobalVar.cpp`（`PointerType*` / `ArrayType*` 的 `dynamic_cast`，与现有 `Print` 逻辑一致）

这样后端只需 `global->IsArray()` 和 `global->GetArrayType()`，不必再在 MipsEmitter 里写类型层次判断，也便于以后扩展（如只对数组做特殊处理）。

---

### 2. .data 段：标量 vs 数组分支（MipsEmitter）

**文件**：`src/mips/MipsEmitter.cpp` — `EmitDataSegment()`。

- **标量全局（`!global->IsArray()`）**
  保持 M2a 行为：
  `global_<name>: .word <初值或 0>`

- **数组全局（`global->IsArray()`）**
  - 用 `global->GetArrayType()` 得到 `ArrayType*`，取 `GetNumElements()` 得到 N。
  - 若有初值：`GetInitializer()` 为 `ConstantArray*`，遍历 `GetElements()`，对每个元素用 `ConstantInt*` 取值（否则 0），输出
    `global_<name>: .word v0, v1, ...`
    （MARS 支持一行多个 `.word` 初值。）
  - 若无初值：输出
    `global_<name>: .space 4*N`
    表示 N 个 int 的零初始化。

**依赖**：`#include "ir/Type.h"`（使用 `ArrayType::GetNumElements()`）、`ir/Constant.h`（`ConstantArray`）、`ir/GlobalVar.h`（新 API）。

---

### 3. GEP 发射：双下标与单下标（FunctionEmitter）

**文件**：`src/mips/FunctionEmitter.cpp` — `EmitGetElementPtrInst()`。

- **栈帧与派发**
  - `BuildStackFrame()` 已为 `GetElementPtrInst` 分配 4 字节结果槽（存“地址”）。
  - `EmitBody()` 已对 `GetElementPtrInst` 做派发并调用 `EmitGetElementPtrInst`。

- **下标语义**
  - LLVM 的 `getelementptr inbounds [N x i32], ptr @arr, i32 0, i32 <elem>` 有两个下标：
    - 第一个 `0` 表示“在数组这一层”的索引（固定 0）；
    - 第二个才是**元素下标**，对应 C 的 `arr[elem]`。
  - 实现时：
    - 若存在第二个下标（`GetIndex(1) != nullptr`），用 **GetIndex(1)** 作为元素下标；
    - 否则用 **GetIndex(0)**（兼容单下标 GEP）。
  - 地址计算：`base + elem_index * 4`（int 为 4 字节），结果写回该 GEP 的栈槽。

- **Load/Store**
  无需改：GEP 结果槽里已是“元素地址”，现有 `LoadValueToReg(ptr)` + `lw $t0, 0($t0)` / `sw $t0, 0($t1)` 已正确完成对 `arr[i]` 的读写。

---

## 三、验收与跑通样例

**样例**（与 step_M2_global_vars_plan / step_M2_acceptance_and_M2d_plan 一致）：

```c
int arr[3] = {10, 20, 30};
int main() {
    return arr[1];
}
```

**预期**：
- `.data` 中有：`global_arr: .word 10, 20, 30`。
- main 中：GEP 用 `la` + 元素下标 1 × 4 得到 `arr[1]` 的地址并存入栈槽；load 从该地址取 20；return 通过已有逻辑把 20 放入 `$v0`。
- MARS 运行后，main 返回值（及当前临时打印）为 **20**。

**验收**：将上述源码放入 `testfile.txt`（或复制 `testfile_m2d.txt` 为 `testfile.txt`），编译生成 `mips.txt`，在 MARS 中运行，确认返回/输出为 20。

---

## 四、涉及文件一览

| 文件 | 变更 |
|------|------|
| `include/ir/GlobalVar.h` | 增加 `ArrayType` 前向声明，`IsArray()`、`GetArrayType()` 声明 |
| `src/ir/GlobalVar.cpp` | 实现 `IsArray()`、`GetArrayType()` |
| `src/mips/MipsEmitter.cpp` | `EmitDataSegment()` 按 `IsArray()` 分支；数组用 `.word v0,v1,...` 或 `.space 4*N`；增加 `ir/Constant.h`、`ir/GlobalVar.h`、`ir/Type.h` |
| `src/mips/FunctionEmitter.cpp` | `EmitGetElementPtrInst()` 使用“存在则用 GetIndex(1)，否则 GetIndex(0)”作为元素下标，计算 base + elem_index*4 并写回 GEP 结果槽 |

---

## 五、与后续步骤的边界

- **M2d 仅做“全局数组”**：base 为 `GlobalVar*` 的 GEP；.data 只扩展“全局 int 数组”的初值/空间。
- **局部数组**：base 为 Alloca 的 GEP 放在后续“局部数组 + GEP”中实现，基址为 `$sp + alloca_offset`，元素偏移仍为 index*4，逻辑与当前 GEP 一致，仅基址来源不同。

以上为 M2d 的完成说明与验收要点。

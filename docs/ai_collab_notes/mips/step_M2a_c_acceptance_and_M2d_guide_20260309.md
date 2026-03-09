# M2a–c 验收结论与 M2d 数组定义实现指南

**日期**：2026-03-09  
**范围**：M2a–c 验收；M2d 的实现框架 + 预期目标（“应当如何进行”）。

---

## 一、M2a–c 验收结论

依据 `step_M2_global_vars_plan.md` 的条目逐项核对当前实现。

### M2a：.data 段只发射标量 int 全局变量

| 要求 | 状态 | 依据 |
|------|------|------|
| 遍历 `module_.GetGlobalVars()`，仅处理标量 int | ✅ | `EmitDataSegment()` 中 `else` 分支处理非数组：`ConstantInt` → `.word 值`，否则 `.word 0` |
| 标签 `global_<name>:`，与 FunctionEmitter 一致 | ✅ | 使用同一 `GlobalLabel(name)`（`"global_" + name`） |
| 有初值 `.word <值>`，无初值 `.word 0` | ✅ | 代码 56–62 行 |
| 暂不处理数组（数组在 M2d） | ✅ | 数组走单独分支，M2a 验收不依赖该分支 |

**验收**：M2a 通过。跑通样例 `int a=5; int main(){ return 0; }` → `.data` 有 `global_a: .word 5`，MARS 可运行。

---

### M2b：LoadInst 从全局变量加载（main 中 return a）

| 要求 | 状态 | 依据 |
|------|------|------|
| EmitBody 派发 LoadInst，调用 EmitLoadInst | ✅ | `FunctionEmitter.cpp` 44–46 行 |
| 指针为 GlobalVar 时：la → lw 0(reg) → sw 到结果槽 | ✅ | `EmitLoadInst`: `LoadValueToReg(ptr,"$t0")`（GlobalVar 时生成 `la`）→ `lw $t0,0($t0)` → `sw $t0, value_offset_($sp)` |
| BuildStackFrame 为 LoadInst 分配槽 | ✅ | `BuildStackFrame` 中 `LoadInst` 已加入分配列表 |

**验收**：M2b 通过。跑通样例 `int a=5; int main(){ return a; }` → 返回值 5。

---

### M2c：StoreInst 向全局变量存储（main 中 a=42; return a;）

| 要求 | 状态 | 依据 |
|------|------|------|
| EmitBody 派发 StoreInst，调用 EmitStoreInst | ✅ | `FunctionEmitter.cpp` 47–49 行 |
| 目标为 GlobalVar 时：值入 $t0，地址入 $t1，sw $t0, 0($t1) | ✅ | `EmitStoreInst`: `LoadValueToReg(value,"$t0")`，`LoadValueToReg(ptr,"$t1")`（GlobalVar → `la`）→ `sw $t0, 0($t1)` |

**验收**：M2c 通过。跑通样例 `int a; int main(){ a=42; return a; }` → 返回值 42。

---

**M2a–c 总结**：三项均符合计划，**验收通过**。建议本地再跑一遍上述三个样例并确认 MARS 行为一致。

---

## 二、M2d 数组定义应当如何进行（实现框架 + 预期目标）

M2d 在 M2a–c 基础上，增加**全局 int 数组**的 .data 发射，以及通过 **GEP + Load/Store** 对数组元素的访问。下面按“实现框架/思路”和“预期目标”两部分说明。

---

### 2.1 实现框架与思路

#### （1）IR/类型侧：区分“标量全局”与“数组全局”

- **目标**：后端只需问“这个全局是不是数组？”“数组有几项？初值是什么？”，不必在 MipsEmitter 里写一坨 `GetType()/GetPointeeType()` 的类型判断。
- **做法**：在 `GlobalVar` 上提供两个接口（若尚未有则新增）：
  - **`bool GlobalVar::IsArray() const`**  
    当且仅当“类型为指针且 pointee 为 `ArrayType`”时返回 true。
  - **`ArrayType* GlobalVar::GetArrayType() const`**  
    是数组则返回 pointee 的 `ArrayType*`，否则返回 nullptr。
- **实现要点**：在 `GlobalVar.cpp` 中用 `PointerType` / `ArrayType` 的 `dynamic_cast` 实现；与现有 `Print` 里对数组的判断保持一致。

这样 .data 发射和后续“局部数组”都可复用同一套“是否数组 + 数组类型”的抽象。

---

#### （2）.data 段：标量 vs 数组两路分支

- **位置**：`MipsEmitter::EmitDataSegment()`。
- **思路**：对每个全局，先按“是否数组”分支，再分别发射。

| 分支 | 行为 |
|------|------|
| **标量**（`!global->IsArray()`） | 保持 M2a：`global_<name>: .word <初值或 0>` |
| **数组**（`global->IsArray()`） | 用 `global->GetArrayType()` 取 `ArrayType*`，`GetNumElements()` 得 N。<br>• **有初值**：`GetInitializer()` 为 `ConstantArray*`，遍历 `GetElements()`，每个元素取 `ConstantInt` 值（否则 0），输出 **`.word v0, v1, ...`**（MARS 支持一行多初值）。<br>• **无初值**：输出 **`.space 4*N`**（N 个 int 的零初始化）。 |

- **依赖**：`ir/Type.h`（`ArrayType::GetNumElements()`）、`ir/Constant.h`（`ConstantArray`、`ConstantInt`）、`GlobalVar` 的 `IsArray()` / `GetArrayType()`。

---

#### （3）GEP 发射：基址 + 元素下标 → 地址写入栈槽

- **位置**：`FunctionEmitter::EmitGetElementPtrInst()`。
- **语义**：LLVM 的 `getelementptr inbounds [N x i32], ptr @arr, i32 0, i32 <elem>` 中，第一个下标 0 表示“在数组这一层”的索引，第二个才是 C 的 `arr[elem]`。因此：
  - 若存在 **GetIndex(1)**，用 **GetIndex(1)** 作为元素下标；
  - 否则用 **GetIndex(0)**（兼容单下标 GEP）。
- **思路**：
  1. **基址**：`LoadValueToReg(inst->GetPointerOperand(), "$t0")`。当 base 为 `GlobalVar*` 时，`LoadValueToReg` 已生成 `la $t0, global_<name>`，即数组首地址。
  2. **下标**：把“元素下标”对应的 SSA 值（或常量）加载到寄存器（如 `$t1`）。
  3. **地址计算**：`addr = base + elem_index * 4`（int 占 4 字节）。MIPS：`sll $t2, $t1, 2`；`addu $t2, $t0, $t2`。
  4. **写回**：把得到的地址存到该 GEP 的**结果槽**：`sw $t2, value_offset_[inst]($sp)`。
- **栈帧**：`BuildStackFrame()` 中需为 **GetElementPtrInst** 分配 4 字节结果槽（存“元素地址”），并在 `EmitBody` 中派发 GEP 并调用 `EmitGetElementPtrInst`。

Load/Store 无需改：GEP 的结果槽里已是“元素地址”；现有 `EmitLoadInst` / `EmitStoreInst` 对指针操作数调用 `LoadValueToReg`，会从该槽取地址，再 `lw 0(reg)` / `sw 0(reg)`，即完成对 `arr[i]` 的读写。

---

#### （4）与后续“局部数组”的边界

- **M2d 只做“全局数组”**：GEP 的 base 为 `GlobalVar*`；.data 只扩展“全局 int 数组”的初值/空间。
- **局部数组**：base 为 Alloca 的 GEP 放在后续“局部数组 + GEP”中实现；基址为 `$sp + alloca_offset`，元素偏移仍为 `index*4`，GEP 发射逻辑与当前一致，仅基址来源不同。

---

### 2.2 预期目标与验收

**目标**：

- `.data` 段能正确发射全局 int 数组（有初值 `.word v0,v1,...`，无初值 `.space 4*N`）。
- main 中 `return arr[1]` 或 `arr[i]=x; ...` 通过 GEP 得到元素地址，再经现有 Load/Store 完成读写；MARS 运行结果正确。

**验收样例**：

```c
int arr[3] = {10, 20, 30};
int main() {
    return arr[1];
}
```

**预期**：

- `.data` 中有：`global_arr: .word 10, 20, 30`（或等价多行）。
- main 中：GEP 用 `la` 得到 `global_arr`，下标 1 × 4 得到 `arr[1]` 的地址并存入 GEP 结果槽；Load 从该地址取 20；Return 将 20 放入 `$v0`。
- MARS 运行后，main 返回值（及当前临时打印）为 **20**。

**验收步骤**：将上述源码放入 `testfile.txt`，编译生成 `mips.txt`，在 MARS 中运行，确认返回/输出为 20。

---

## 三、当前代码与 M2d 的对应关系（若已实现）

若项目中已实现 M2d，可按下表自检：

| 项目 | 预期实现位置 |
|------|----------------|
| `GlobalVar::IsArray()` / `GetArrayType()` | `include/ir/GlobalVar.h` + `src/ir/GlobalVar.cpp` |
| .data 数组分支（.word 初值 / .space） | `MipsEmitter::EmitDataSegment()` |
| GEP 结果槽分配 | `BuildStackFrame()` 中为 GetElementPtrInst 分配 4 字节 |
| GEP 派发 | `EmitBody()` 中 dynamic_cast GetElementPtrInst 并调用 EmitGetElementPtrInst |
| EmitGetElementPtrInst（base + index*4 → 槽） | `FunctionEmitter::EmitGetElementPtrInst()` |

完成上述项并跑通 `arr[3]={10,20,30}; return arr[1];` 即视为 M2d 验收通过。

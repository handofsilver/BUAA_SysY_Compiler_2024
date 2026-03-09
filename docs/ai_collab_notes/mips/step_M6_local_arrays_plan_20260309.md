# M6：局部数组（alloca + GEP + load/store）— 实现路线与验收样例

**目标**：支持函数内局部数组（`int a[N]`、`int a[N] = {...}`），包括 AllocaInst 栈上分配、以 alloca 为 base 的 GEP、以及通过 GEP 结果的 load/store；与 M2d 的全局数组形成完整的“数组”能力。

**约定**：本阶段只做 **int** 局部数组；char 数组 / 字符串初值可放到后续 M8。控制流（多块、分支、phi）未做时，样例均为**单块** main。

---

## 一、为什么先做 M6（趁热打铁）

- M2d 已做完**全局**数组：.data、GEP base=GlobalVar、Load/Store。
- **局部**数组只差：base 从“全局标签”换成“栈上 alloca 的起始偏移”；GEP 与 Load/Store 的“地址在栈槽”逻辑可复用。
- 先做完 M6，数组相关 IR 在单块内就全部打通，之后再上 M4（分支）/ M5（phi）/ M3（库函数）时，测试用例可以自由用局部数组。

---

## 二、伪代码实现思路

### 2.1 BuildStackFrame：为 AllocaInst 分配空间

- 遍历块内指令时，若为 **AllocaInst**：
  - 取类型：`GetType()` 为 PointerType，`GetPointeeType()` 为元素类型。
  - **标量**（`alloca i32` / `alloca i8`）：size = 4。
  - **数组**（`alloca [N x i32]`）：pointee 为 ArrayType，size = `GetNumElements() * 4`。
  - 分配：`value_offset_[alloca] = frame_size_`；`frame_size_ += size`。
- **不**为 AllocaInst 分配“结果槽”（alloca 的“值”是地址，用 offset 表示即可）。
- 分配顺序：建议与先导指导书一致——$ra 槽后，先处理 AllocaInst（通常集中在 entry 块），再为其他产生结果的指令（Binary、Load、GEP 等）分配 4 字节槽。这样 alloca 占的是一段连续区间，后面指令的 offset 都大于 alloca 区间末尾。

### 2.2 LoadValueToReg：AllocaInst 给出“地址”

- 当 `val` 为 **AllocaInst** 时：
  - 输出：`addiu reg, $sp, value_offset_[alloca]`（当前栈帧下，该 alloca 的起始地址）。
- 这样 GEP 的 base 为 alloca 时，base 地址就是 `$sp + offset`；Load/Store 的 ptr 为 GEP 结果时，沿用现有“从栈槽取地址再 lw/sw 0(reg)”即可。

### 2.3 EmitGetElementPtrInst：base 为 AllocaInst

- 已有逻辑：base → `$t0`，elem_index → `$t1`，`$t0 + ($t1<<2)` → 写回 GEP 结果槽。
- **本步**：确保 base 来自 **AllocaInst** 时，`LoadValueToReg(base, "$t0")` 得到的是地址。即在上一步已实现 AllocaInst → `addiu $t0, $sp, offset` 的前提下，无需改 GEP 发射，只需 BuildStackFrame 与 LoadValueToReg 支持 AllocaInst。

### 2.4 EmitLoadInst / EmitStoreInst：ptr 为“栈上地址”

- 当前实现：`LoadValueToReg(ptr, "$t0")`，再 `lw $t0, 0($t0)`（load）/ `sw ..., 0($t1)`（store）。
- **GlobalVar**：LoadValueToReg 已做 `la`，得到的是地址，后续 lw/sw 正确。
- **GEP 结果 / AllocaInst 作为指针**：ptr 在 `value_offset_` 中，当前 `else` 分支为 `lw reg, offset($sp)`，即把“栈槽里存的地址”取到 reg，再 `lw reg, 0(reg)` / `sw` 正确。
- 因此只需在 **LoadValueToReg** 里对 **AllocaInst** 做“取地址”（addiu），**不必**在 LoadInst/StoreInst 里区分 ptr 是 GlobalVar 还是 alloca/GEP；EmitLoadInst/EmitStoreInst 可保持现状（若当前对 GlobalVar 已是“la + lw 0(reg)”的语义，则局部数组自然打通）。

---

## 三、子步骤与验收样例（预期结果由你本地运行验证）

### M6a：BuildStackFrame 为 AllocaInst 分配；LoadValueToReg(AllocaInst) 取地址

**实现要点**
- BuildStackFrame 中识别 AllocaInst，按 pointee 是标量(4) 或数组(N×4) 计算 size，`value_offset_[alloca] = frame_size_`，`frame_size_ += size`。
- LoadValueToReg 中若 `val` 为 AllocaInst：`addiu reg, $sp, value_offset_[alloca]`。
- AllocaInst **不**再在“产生结果的指令”里占一个 4 字节结果槽（alloca 的“值”就是这段空间的起始偏移）。

**样例 1**（仅标量 alloca，不读写）

```c
int main() {
    int x;
    return 0;
}
```

**预期**：能编译、生成 mips.txt、MARS 能跑且返回 0；栈帧比“无 alloca”时至少多 4 字节（x 的槽）。

**样例 2**（局部数组，不读写）

```c
int main() {
    int a[5];
    return 0;
}
```

**预期**：能跑；栈帧比无 alloca 时多 20 字节（5×4）。

---

### M6b：GEP base = AllocaInst 时正确计算元素地址

**实现要点**
- 无需改 GEP 发射逻辑，只要 LoadValueToReg(AllocaInst) 已输出 `addiu $t0, $sp, offset`，GEP 的 base 在 `$t0`，下标在 `$t1`，`$t0 + ($t1<<2)` 写回 GEP 结果槽即可。
- 若有“两下标”与“单下标”的区分，沿用 M2d 的约定（第二下标为元素下标，无则用第一下标）。

**样例 3**（局部数组 + 初值 + 读元素）

```c
int main() {
    int a[3] = {10, 20, 30};
    return a[1];
}
```

**预期**：main 返回值 **20**（你本地 Run 验证）。

---

### M6c：局部数组的写 + 再读（Store 通过 GEP 结果）

**实现要点**
- StoreInst 的 ptr 为 GEP 结果时，当前应已是：LoadValueToReg(ptr) → 从栈槽取地址到 reg，再 `sw value, 0(ptr_reg)`。
- 若 M6a/M6b 已正确，本步通常无需改代码，仅用“写局部元素再返回”的样例验收。

**样例 4**（写局部数组元素再读）

```c
int main() {
    int a[3] = {1, 2, 3};
    a[1] = 99;
    return a[1];
}
```

**预期**：main 返回值 **99**。

---

## 四、与 M2d 的边界

- **M2d**：base 为 **GlobalVar** 的 GEP；.data 段；Load/Store 的 ptr 为 GlobalVar 或 GEP(GlobalVar)。
- **M6**：base 为 **AllocaInst** 的 GEP；栈上分配；Load/Store 的 ptr 为 AllocaInst（标量槽）或 GEP(AllocaInst)。
- 两套在 IR 上区分点只有“base 是 GlobalVar 还是 AllocaInst”；后端只需在 BuildStackFrame 与 LoadValueToReg 里区分这两种 base，GEP/Load/Store 的其余逻辑一致。

---

## 五、实现顺序小结

| 子步 | 内容 | 你本地验证的样例与预期 |
|------|------|------------------------|
| **M6a** | BuildStackFrame 为 AllocaInst 分配 size；LoadValueToReg(AllocaInst)→addiu reg,$sp,offset | `int x; return 0;` 能跑；`int a[5]; return 0;` 能跑，栈帧多 20 字节 |
| **M6b** | 保证 GEP base=Alloca 时 base 为 $sp+offset（依赖 M6a），GEP 发射不变 | `int a[3]={10,20,30}; return a[1];` → 返回值 **20** |
| **M6c** | 确认 Store 通过 GEP 结果写内存（一般无需改） | `int a[3]={1,2,3}; a[1]=99; return a[1];` → 返回值 **99** |

按 **M6a → M6b → M6c** 顺序做，每步用上表样例自测即可；全部通过即 M6 阶段完成。之后可接 M4（分支）/ M5（phi）或 M3（库函数）。

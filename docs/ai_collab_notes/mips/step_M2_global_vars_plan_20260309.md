# M2：.data 段与全局变量 — 实现路线与验收样例

**目标**：支持全局 `int` 标量的读写（声明、初值、在 main 中 load/store），生成正确的 `.data` 与 `lw`/`sw`，在 MARS 中跑通指定样例。

**约定**：全局变量在 MIPS 中的标签与 FunctionEmitter 一致，使用 **`global_` + 变量名**（如 `global_a`），保证 .data 与 .text 中引用一致。

---

## 一、子步骤拆解与跑通样例

### M2a：.data 段只发射“标量 int”全局变量

**实现目标**
- 在 `MipsEmitter::EmitDataSegment()` 中遍历 `module_.GetGlobalVars()`。
- **仅处理**：类型为 `ptr i32`（PointerType → IntegerType 32）的全局变量，即标量 `int`。
- 对每个这样的全局：
  - 输出标签：`global_<name>:`（与 FunctionEmitter 中 `GlobalLabel(name)` 一致）。
  - 若有初值（`GetInitializer()` 为 `ConstantInt*`）：输出 `.word <值>`。
  - 若无初值（nullptr）：输出 `.word 0`。
- **暂不处理**：数组、char、字符串常量。

**跑通样例**

```c
// testfile.txt
int a = 5;
int main() {
    return 0;
}
```

**预期**
- `mips.txt` 中 `.data` 段有且仅有：`global_a: .word 5`（或等价形式）。
- `.text` 与 main 行为不变（main 仍 return 0）。
- MARS 能正常汇编、运行、退出。

**验收**：打开 mips.txt，确认 `.data` 段存在且仅包含上述一行；MARS Run 正常。

---

### M2b：LoadInst 从全局变量加载（main 中 return a）

**实现目标**
- `EmitBody` 中在遍历指令时，对 **LoadInst** 做派发，调用 `EmitLoadInst(load)`。
- `EmitLoadInst` 本步只处理 **指针操作数是 GlobalVar** 的情况（即 `load i32, ptr @a`）：
  - 用 `la $t0, global_<name>` 取全局地址；
  - `lw $t0, 0($t0)` 加载值；
  - `sw $t0, result_offset($sp)` 存到该 LoadInst 结果的栈槽（BuildStackFrame 已为 LoadInst 分配槽）。
- 若指针不是 GlobalVar（例如 later 的 alloca），本步可跳过或断言/报错，留到“局部/数组”阶段。

**跑通样例**

```c
// testfile.txt
int a = 5;
int main() {
    return a;
}
```

**预期**
- `.data` 有 `global_a: .word 5`。
- main 中对应 `%x = load i32, ptr @a` 的序列类似：
  `la $t0, global_a` → `lw $t0, 0($t0)` → `sw $t0, <result_offset>($sp)`；
  随后 `return a` 会通过已有逻辑把该栈槽加载到 `$v0` 并返回。
- MARS 运行后，main 返回值为 **5**（若开了“打印 main 返回值”的临时代码，控制台应看到 5）。

**验收**：MARS Run，确认返回值/控制台输出为 5。

---

### M2c：StoreInst 向全局变量存储（main 中 a = 42; return a;）

**实现目标**
- `EmitBody` 中对 **StoreInst** 派发，调用 `EmitStoreInst(store)`。
- `EmitStoreInst` 本步只处理 **目标指针是 GlobalVar** 的情况（即 `store i32 %val, ptr @a`）：
  - 用 `LoadValueToReg(store->GetValueOperand(), "$t0")` 把要存的值放到 `$t0`；
  - `la $t1, global_<name>`；
  - `sw $t0, 0($t1)`。
- 若目标不是 GlobalVar，本步可跳过或留到后续。

**跑通样例**

```c
// testfile.txt
int a;
int main() {
    a = 42;
    return a;
}
```

**预期**
- `.data` 有 `global_a: .word 0`。
- main 中：先有一条“把 42 存到 global_a”的序列（`li/la` + `sw`），再有一条“从 global_a 加载到结果槽”，最后 return 把该槽加载到 `$v0`。
- MARS 运行后，main 返回值为 **42**。

**验收**：MARS Run，确认返回值/控制台输出为 42。

---

### M2d（可选，可拆成后续小步）：全局 int 数组

**实现目标**
- `.data` 段支持全局 `int arr[N]`：有初值则 `.word v0, v1, ...`，无初值则 `.word 0:N` 或逐 0。
- Load/Store 的目标若为“全局数组 + GEP”（即 base 为 GlobalVar，GEP 算偏移），则：
  - 基址用 `la` 得到全局标签；
  - 偏移按 GEP 的 index 计算（元素大小 4），得到地址后 `lw`/`sw`。

**跑通样例（若做 M2d）**

```c
int arr[3] = {10, 20, 30};
int main() {
    return arr[1];  // 期望 20
}
```

本步可与“局部数组 + GEP”一起做，也可在 M2 只做到“标量全局 + .data + load/store”，数组放到下一大步。

---

## 二、实现顺序小结

| 子步 | 内容 | 跑通样例 |
|------|------|----------|
| **M2a** | .data 段只做标量 int 全局（标签 `global_<name>`，.word 0 或初值） | `int a=5; int main(){ return 0; }` → .data 有 `global_a: .word 5`，MARS 能跑 |
| **M2b** | EmitBody 派发 LoadInst；EmitLoadInst 在 ptr=GlobalVar 时 la+lw+sw 到结果槽 | `int a=5; int main(){ return a; }` → 返回值 5 |
| **M2c** | EmitBody 派发 StoreInst；EmitStoreInst 在 ptr=GlobalVar 时 loadValueToReg + la + sw | `int a; int main(){ a=42; return a; }` → 返回值 42 |
| **M2d** | （可选）全局 int 数组 .data + GEP 基址的 load/store | `int arr[3]={10,20,30}; int main(){ return arr[1]; }` → 20 |

建议严格按 **M2a → M2b → M2c** 顺序做，每步都编译、生成 mips.txt、在 MARS 中跑一遍对应样例再进入下一步；M2d 可在 M2 收尾或与“局部数组”一起做。

---

## 三、IR 接口速查（M2 用到的）

- **Module**：`module_.GetGlobalVars()` → `const vector<unique_ptr<GlobalVar>>&`
- **GlobalVar**：
  - `GetName()` → 变量名（如 `"a"`）
  - `GetType()` → `PointerType*`，`GetPointeeType()` 为元素类型（`IntegerType*` 或 `ArrayType*`）
  - `GetInitializer()` → `Constant*`（nullptr / `ConstantInt*` / `ConstantArray*`）
  - `IsConstant()` → bool
- **类型判断**：
  - 标量 int：`PointerType* pt = dynamic_cast<PointerType*>(gv->GetType())`，`IntegerType* it = dynamic_cast<IntegerType*>(pt->GetPointeeType())`，且 `it && it->GetBits() == 32`
  - 数组：`GetPointeeType()` 为 `ArrayType*`
- **LoadInst**：`GetPointerOperand()` 为指针（本步若为 `GlobalVar*` 则走 M2b 逻辑）
- **StoreInst**：`GetValueOperand()`、`GetPointerOperand()`；本步若 `GetPointerOperand()` 为 `GlobalVar*` 则走 M2c 逻辑

---

## 四、与现有代码的衔接

- **FunctionEmitter**：已存在 `LoadValueToReg` 对 `GlobalVar` 的处理（`la` + `lw`），且使用 `GlobalLabel(name) = "global_" + name`。MipsEmitter 的 .data 段必须使用**同一标签名**（`global_<name>:`）。
- **BuildStackFrame**：若已为 `LoadInst` 分配结果槽，则 M2b 只需在 EmitBody 中派发 LoadInst 并实现“ptr=GlobalVar”的 EmitLoadInst。
- **EmitBody 派发**：在现有 BinaryInst、ReturnInst 之外，增加对 `LoadInst`、`StoreInst` 的 `dynamic_cast` 与对应 `Emit*` 调用；顺序与 IR 指令顺序一致即可。

完成 M2a/M2b/M2c 后，可再根据是否需要“全局数组”决定是否做 M2d，或把数组留到“局部数组 + GEP”一起做。

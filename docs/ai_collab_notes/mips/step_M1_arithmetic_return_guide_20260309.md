# 步骤 M1：纯算术 + return（单函数、单块）

**目标**：不再硬编码 main 的 body，而是**根据 main 的 IR 真正生成** MIPS，使得
`int main() { return 1 + 2; }`
在 MARS 中运行后，程序退出码为 **3**（即返回值 3）。

---

## 1. 本步实现目标（一句话）

**让 main 的 .text 由「遍历 main 的 IR」得到：只处理单块、且块内仅有 BinaryInst 与 ReturnInst 的情况；实现栈帧分配、prologue、二元运算与 return 的翻译。**

---

## 2. 本步范围

| 做 | 暂不做 |
|----|--------|
| 只处理 **main 一个函数**（其他函数仍可跳过或只打标签+jr $ra） | 其他函数、多块、分支、phi、全局变量、库函数调用 |
| main 内**仅一个 BasicBlock** | 多个块、br、phi |
| 指令只处理 **BinaryInst**（ADD/SUB/MUL/DIV/REM）和 **ReturnInst** | Load/Store、Call、Branch、Icmp、GEP、Alloca 等 |
| 操作数允许 **ConstantInt** 和**已在本块定义过的 SSA 值**（栈槽） | 全局变量、指针、数组 |

---

## 3. 验收方式

- 测试用例：`int main() { return 1 + 2; }`
  - 用本编译器生成 `mips.txt`，在 MARS 4.5 中 Run。
  - MARS 菜单：Run → 程序结束后，可在 Run 界面或通过“程序参数/退出码”查看；或简单看 Run 是否正常结束且无异常。
  - **预期**：返回值 3。若无法直接看退出码，可后续用 `putint` 打印验证；本步至少保证生成的指令语义正确（main 把 3 放入 `$v0` 并返回，入口 stub 再 exit）。
- 可选：`int main() { return 10 - 3 * 2; }` → 预期 4；`int main() { return 7 % 3; }` → 预期 1。

---

## 4. 需要新增/修改的内容

1. **FunctionEmitter**（每函数一个，负责该函数的栈帧 + 指令发射）
   - 构造函数：`(std::ostream& os, const ir::Function& func)`，保存 `os_`、`func_`。
   - **buildStackFrame()**：遍历 `func_.GetBlocks()` 的每个块、每条指令，为“会产生结果的 Value”分配栈偏移（见下）。
   - **emitPrologue()**：输出函数标签（main → `main:`）、`addiu $sp, $sp, -frame_size`、`sw $ra, ra_offset($sp)`。
   - **emitBody()**：遍历**唯一**基本块（本步仅一个块）的每条指令，根据类型调用 emitBinaryInst / emitReturnInst，其它类型可暂时忽略或断言/跳过。
   - **emitEpilogue()**：本步可不在单独函数里写，而在 emitReturnInst 内联：恢复 `$ra`、`addiu $sp, $sp, frame_size`、`jr $ra`。

2. **Value → 栈偏移**
   - 用 `std::unordered_map<const ir::Value*, int> value_offset_`；key 为 Value*，value 为相对**当前 $sp** 的字节偏移（正数表示 $sp 之上，即高地址）。
   - **buildStackFrame 分配顺序**（从高到低，即先分配的 offset 大）：
     - 先为 **$ra 保存槽** 留 4 字节（例如 offset = 0，表示 prologue 后 $sp 指向的位置存 $ra）。
     - 再遍历所有 Argument，每个 4 字节（main 无参数可忽略）。
     - 再遍历所有块的所有指令：**BinaryInst** 的结果占 4 字节；**ReturnInst** 不产生结果，不分配。本步无 Alloca、无 Call 返回值。
   - **frame_size** = 上述所有槽之和，且按 4 字节对齐（本步就是 4 的倍数）。

3. **emitBinaryInst(BinaryInst* inst)**
   - 取 lhs、rhs（可能为 ConstantInt 或已分配栈槽的 SSA 值）。
   - 若为 ConstantInt：用 `li $t0, imm`（或类似）；若为栈上的值：`lw $t0, offset($sp)`。需要辅助 **loadValueToReg(Value*, reg)**：ConstantInt → `li`，否则 `lw value_offset_[val]($sp)`。
   - 根据 GetOp()：ADD→addu, SUB→subu, MUL→mul, DIV→div+mflo, REM→div+mfhi，结果放入一个临时寄存器（如 $t1）。
   - 将结果存回该 BinaryInst 的栈槽：`sw $t1, offset($sp)`。

4. **emitReturnInst(ReturnInst* inst)**
   - 若 GetRetVal() 非空：用 loadValueToReg 将返回值加载到 **$v0**。
   - 输出 epilogue：`lw $ra, ra_offset($sp)`、`addiu $sp, $sp, frame_size`、`jr $ra`。

5. **MipsEmitter 的修改**
   - 在 Emit() 中，引导代码（.text、jal main、exit）不变。
   - **遍历** `module_.GetFunctions()`，对每个有 GetBlocks().empty()==false 的函数（即有 body 的）：
     - 若名为 `"main"`：构造 `FunctionEmitter(os_, *func)`，调用 `buildStackFrame()`、`emitPrologue()`、`emitBody()`（epilogue 在 ReturnInst 里）。
     - 其他函数：本步可仍只输出 `func_name:` + `jr $ra`，或同样走 FunctionEmitter 但只处理单块+Binary+Return。

---

## 5. 建议实现顺序（小步可验证）

1. **只接 FunctionEmitter，不改变行为**
   - 在 MipsEmitter::Emit() 里，对 main 不再手写 `main:` 和 `jr $ra`，改为构造 `FunctionEmitter(os_, main_func)`，调用 `buildStackFrame()`、`emitPrologue()`、`emitBody()`。
   - FunctionEmitter 的 emitBody() 先**空实现**：不输出任何指令（或只输出一条 jr $ra）。
   - **验证**：编译运行，mips.txt 仍有 main 标签和 jr $ra，且入口 stub 正常，MARS 能跑。

2. **buildStackFrame() 只做“数”**
   - 遍历 main 的唯一块，对每条 BinaryInst 分配一个 offset（从 4 开始，每次 +4；$ra 占 0），算好 frame_size。
   - **验证**：对 `return 1+2`，应有 1 个 BinaryInst 结果槽 + $ra 槽，frame_size=8。

3. **emitPrologue() 真实输出**
   - 输出 `addiu $sp, $sp, -frame_size`、`sw $ra, 0($sp)`（若 $ra 在 offset 0）。
   - **验证**：mips.txt 里 main 下有 addiu 和 sw。

4. **loadValueToReg**
   - 实现：若 Value 是 ConstantInt，`li reg, GetValue()`；否则 `lw reg, value_offset_[val]($sp)`。
   - 在 emitBinaryInst 里用两个临时寄存器（如 $t0、$t1）加载 lhs、rhs，算术后 sw 到结果槽。

5. **emitBinaryInst**
   - 根据 BinaryOp 发 addu/subu/mul/div+mflo/div+mfhi，再 sw 结果。

6. **emitReturnInst**
   - 加载 ret val 到 $v0，再输出 lw $ra、addiu $sp、jr $ra。

每完成 2～3 小步就编译、跑一次 MARS，用 `return 1+2` 验证，最后确认退出码为 3。

---

## 6. 栈帧约定（本步简化）

- 栈向低地址增长；prologue 后 `$sp` 指向“当前栈帧最低地址”。
- `value_offset_[V]` = 相对当前 `$sp` 的字节偏移，且 **lw/sw 用 offset($sp)** 访问。
- 本步：offset 0 存 $ra；offset 4 存第一个 BinaryInst 的结果（若只有一条 `%1 = add i32 1, 2`，则只有这一个结果槽）。
- 因此 **frame_size = 8**，prologue：`addiu $sp, $sp, -8`；`sw $ra, 0($sp)`；epilogue：`lw $ra, 0($sp)`；`addiu $sp, $sp, 8`；`jr $ra`。

---

## 7. IR 接口速查（本步用到）

- `func.GetBlocks()` → `const vector<unique_ptr<BasicBlock>>&`
- `block.GetInstructions()` → `const list<unique_ptr<Instruction>>&`
- `dynamic_cast<ir::BinaryInst*>(inst.get())` → GetOp(), GetLhs(), GetRhs()
- `dynamic_cast<ir::ReturnInst*>(inst.get())` → GetRetVal()
- `dynamic_cast<ir::ConstantInt*>(val)` → GetValue()
- 指令类型判断用 `dynamic_cast`，未识别的本步可跳过或先忽略。

---

完成本步后，你就有了“从 IR 到 MIPS 的第一条真实指令链”（栈帧 + 算术 + 返回）。下一步（M2 或控制流）再在此基础上扩展。

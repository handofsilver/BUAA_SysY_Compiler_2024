# MIPS 目标代码生成：先导性设计指导

**Date**: 2026-03-05  
**Phase**: MIPS Backend（LLVM IR → MIPS Assembly）  
**Status**: 设计阶段（编码前先导文档）  
**前提**: Lexer / Parser / SemanticAnalyzer / IRGenVisitor / Mem2Reg 全部完成；IR 对后端只读

---

## 一、设计目标

### 1.1 一句话描述

遍历内存中的 `ir::Module`（已经过 Mem2Reg 的 SSA 形式），将每个全局变量、每个函数、每条 IR 指令**翻译为等价的 MIPS 汇编指令序列**，写入 `mips.txt`，能在 MARS 4.5 上正确执行。

### 1.2 课程硬性约束

| 约束 | 来源 | 说明 |
|------|------|------|
| 输入文件 | `testfile.txt` | 编译器只读取此文件，不读标准输入 |
| 输出文件 | `mips.txt` | MIPS 汇编文本 |
| 仿真器 | MARS 4.5（课程组修改版） | 基础指令 + 伪指令，**不可用宏指令** |
| 库函数 | `getint` / `getchar` / `putint` / `putch` / `putstr` | 需通过 MARS syscall 实现 |
| 优化接口 | 后续竞速排序 | 需预留开启/关闭优化的开关 |
| 中间代码输出 | 可选输出优化前/后 IR | 已有 `llvm_ir.txt`，符合要求 |

### 1.3 本阶段的非目标

- **不做**寄存器分配（初版全栈分配，正确优先）
- **不做**指令调度 / 窥孔优化 / 乘除优化
- **不做**尾调用优化

这些全部留给后续优化阶段。初版目标：**正确性 100%，性能不关心**。

---

## 二、先验知识清单

在动手写后端之前，需要明确掌握以下知识。每一项后面标注了"我们用到哪些"以缩小范围。

### 2.1 MIPS 指令集（我们需要的子集）

后端初版只需要以下 MIPS 指令，按功能分组：

**算术逻辑**

| 指令 | 格式 | 用途 | 对应 IR |
|------|------|------|---------|
| `addu $d, $s, $t` | R | 无符号加（不触发溢出异常） | `BinaryInst(ADD)` |
| `addiu $t, $s, imm` | I | 立即数加 | 栈指针调整、常量加 |
| `subu $d, $s, $t` | R | 无符号减 | `BinaryInst(SUB)` |
| `mul $d, $s, $t` | R（伪指令） | 乘法（结果到 $d） | `BinaryInst(MUL)` |
| `div $s, $t` | R | 有符号除法 → HI:LO | `BinaryInst(DIV/REM)` |
| `mfhi $d` / `mflo $d` | R | 取 HI / LO | 配合 `div` |
| `slt $d, $s, $t` | R | Set on Less Than | `IcmpInst(SLT)` |
| `sle $d, $s, $t` | 伪 | Set on Less or Equal | `IcmpInst(SLE)` |
| `sgt $d, $s, $t` | 伪 | Set on Greater Than | `IcmpInst(SGT)` |
| `sge $d, $s, $t` | 伪 | Set on Greater or Equal | `IcmpInst(SGE)` |
| `seq $d, $s, $t` | 伪 | Set on Equal | `IcmpInst(EQ)` |
| `sne $d, $s, $t` | 伪 | Set on Not Equal | `IcmpInst(NE)` |
| `andi $t, $s, imm` | I | 按位与 | `TruncInst`（截断低 8 位） |

**数据传送**

| 指令 | 格式 | 用途 |
|------|------|------|
| `lw $t, offset($s)` | I | 从内存加载 32 位字 |
| `sw $t, offset($s)` | I | 向内存存储 32 位字 |
| `lb $t, offset($s)` | I | 加载字节（符号扩展） |
| `sb $t, offset($s)` | I | 存储字节 |
| `la $t, label` | 伪 | 加载地址 |
| `li $t, imm` | 伪 | 加载立即数 |
| `move $d, $s` | 伪 | 寄存器间移动 |

**控制流**

| 指令 | 格式 | 用途 |
|------|------|------|
| `j label` | J | 无条件跳转 |
| `jal label` | J | 跳转并链接（函数调用） |
| `jr $ra` | R | 从寄存器跳转（函数返回） |
| `beqz $s, label` | 伪 | 为零则跳转 |
| `bnez $s, label` | 伪 | 非零则跳转 |

**系统调用**

| `syscall` | 按 `$v0` 的值执行对应服务 |

### 2.2 MARS syscall 约定

我们的 IR 中有五个库函数，需要翻译为 MARS syscall：

| IR 库函数 | MARS syscall 号 ($v0) | 参数 | 返回 |
|-----------|----------------------|------|------|
| `@getint` | 5 (read integer) | 无 | `$v0` = 读入整数 |
| `@getchar` | 12 (read char) | 无 | `$v0` = 读入字符 |
| `@putint` | 1 (print integer) | `$a0` = 整数值 | 无 |
| `@putch` | 11 (print char) | `$a0` = 字符值 | 无 |
| `@putstr` | 4 (print string) | `$a0` = 字符串地址 | 无 |

### 2.3 MIPS 寄存器约定

| 寄存器 | 名称 | 用途 | 我们如何使用 |
|--------|------|------|-------------|
| `$zero` | `$0` | 常量 0 | 比较、零源 |
| `$v0` | `$2` | 函数返回值 / syscall 号 | 返回值、syscall |
| `$a0`–`$a3` | `$4`–`$7` | 函数参数 0–3 | 调用约定 |
| `$t0`–`$t9` | `$8`–`$15,$24,$25` | 临时（caller-saved） | 中间计算 |
| `$s0`–`$s7` | `$16`–`$23` | 保存（callee-saved） | 初版不用（全栈分配） |
| `$sp` | `$29` | 栈指针 | 栈帧管理 |
| `$fp` | `$30` | 帧指针 | 可选（初版用 `$sp` 相对寻址即可） |
| `$ra` | `$31` | 返回地址 | 函数调用/返回 |

**初版策略**：我们不做寄存器分配，只使用 `$t0`–`$t3` 作为**临时搬运**寄存器（从栈上 load 操作数到 `$t`，计算后 store 结果回栈）。所有 SSA 值的"家"都在栈上。

### 2.4 栈帧布局

MIPS 栈从高地址向低地址增长。我们定义每个函数的栈帧结构如下（由高到低）：

```
高地址
┌───────────────────────────────┐  ← 调用者的 $sp
│  参数溢出区（arg[4], arg[5], ...）│  ← 由调用者压入
├───────────────────────────────┤  ← 本函数入口时 $sp 指向这里
│  $ra 保存槽 (4 bytes)         │  offset = frame_size - 4
│  局部变量 / SSA 值的栈槽      │  每个 Value 占 4 bytes
│  ...                          │
│  alloca 的数组空间             │  按数组大小分配
│  函数调用参数溢出区（如有）     │  用于向被调函数传 >4 个参数
├───────────────────────────────┤  ← $sp（减完之后）
低地址
```

关键设计决定：

1. **$ra 必须保存**：任何非叶函数都可能执行 `jal`，会覆盖 `$ra`。为简单起见，初版对**所有函数**都保存 `$ra`。
2. **每个 `ir::Value*` 一个 4 字节槽**：函数启动时一遍扫描所有 BasicBlock 中的所有 Instruction，为每个产生结果的指令分配一个 offset。参数（`ir::Argument`）也需要槽位。
3. **AllocaInst 的数组**：`alloca [N x i32]` 需要 N×4 字节连续空间，`alloca i32` 需要 4 字节。AllocaInst 本身的"值"是该空间的起始地址。
4. **4 字节对齐**：所有槽位 4 字节对齐，MIPS 的 `lw`/`sw` 要求地址 4 字节对齐。
5. **调用者负责为参数 >4 个时在自己栈帧底部预留空间**。

### 2.5 调用约定（我们自己定义并统一遵守）

**参数传递**：
- 参数 0–3 通过 `$a0`–`$a3` 传递
- 参数 4+ 通过栈传递（压在**调用者**栈帧的底部区域）
- 被调函数入口时，需将 `$a0`–`$a3` 中的有效参数 store 到自己栈帧的对应参数槽

**返回值**：
- 返回值通过 `$v0` 传递

**$ra 保存**：
- 由被调函数（callee）在 prologue 中 `sw $ra, offset($sp)` 保存
- 在 epilogue 中 `lw $ra, offset($sp)` 恢复
- 简化处理：初版所有函数都保存 $ra，无需区分叶函数/非叶函数

**寄存器保存策略（初版简化）**：
- 初版不使用 `$s0`–`$s7`，因此无需保存 callee-saved 寄存器
- `$t0`–`$t3` 作为纯临时，不跨指令存活，无需保存

### 2.6 Phi 节点下降（Phi Lowering）

这是 SSA 形式到具体机器码的核心挑战之一。

**问题**：MIPS 没有 φ 指令。`%r = phi i32 [ %a, %pred1 ], [ %b, %pred2 ]` 表示"如果从 pred1 来，结果是 %a；如果从 pred2 来，结果是 %b"。

**解决方案**：在每个前驱块的**末尾**（跳转指令之前），插入 move 操作，将对应的 incoming value 搬到 phi 结果所在的栈槽。

**关键问题——并行复制（parallel copy）**：当一个块末尾有多个 phi 需要处理时，如果存在交叉依赖（比如 `phi1 = [ %x, ... ]` 和 `phi2 = [ %phi1, ... ]`），简单的逐个 move 可能覆盖还未读取的值。

**初版简化方案**：对于每个 phi 的每个前驱，先将 incoming value load 到临时寄存器 `$t`，再 store 到 phi 结果的栈槽。由于我们用栈上分配，load 操作不会破坏源数据（源在栈上，目标也在栈上），天然避免了覆盖问题。

> **但存在边界情况**：如果两个 phi 互相引用对方作为 incoming value（如循环中的交换），则需要一个额外的临时槽。初版先用"先全部 load 到寄存器/临时槽，再全部 store"的两阶段策略。

---

## 三、IR 到 MIPS 的完整映射

这是后端的核心——每种 IR 指令翻译成什么样的 MIPS 序列。以下假设全栈分配，`offset(V)` 表示 Value V 在栈帧中的偏移。

### 3.1 AllocaInst

`%ptr = alloca T`

AllocaInst 在栈帧布局阶段就已经处理：为其分配空间。AllocaInst 的"值"是所分配空间的起始地址。

- **标量 alloca**（`alloca i32` / `alloca i8`）：4 字节
- **数组 alloca**（`alloca [N x i32]`）：N × 4 字节

在 emitInst 阶段，AllocaInst **不生成任何指令**（空间已在 prologue 的 `addiu $sp` 中统一分配）。当其他指令需要 AllocaInst 的值（即地址）时，通过 `addiu $t, $sp, offset` 计算出地址。

### 3.2 LoadInst

`%val = load i32, ptr %ptr`

```mips
lw   $t0, offset(%ptr)($sp)   # 先取出指针值（%ptr 的内容是一个地址）
lw   $t0, 0($t0)              # 再从该地址加载实际值
sw   $t0, offset(%val)($sp)   # 存回 %val 的栈槽
```

**特殊情况**：如果 `%ptr` 是一个 AllocaInst（直接取其栈地址），则简化为：

```mips
lw   $t0, offset_of_alloca_content($sp)  # 直接从 alloca 对应的栈空间加载
sw   $t0, offset(%val)($sp)
```

类似地，如果 `%ptr` 是一个 GlobalVar，则：

```mips
la   $t0, global_label        # 取全局变量地址
lw   $t0, 0($t0)              # 从全局变量加载
sw   $t0, offset(%val)($sp)
```

### 3.3 StoreInst

`store i32 %val, ptr %ptr`

与 LoadInst 对称：先将 `%val` 取到寄存器，再将 `%ptr` 的地址取到另一个寄存器，最后 `sw`。

```mips
lw   $t0, offset(%val)($sp)   # 取值
lw   $t1, offset(%ptr)($sp)   # 取地址
sw   $t0, 0($t1)              # 写入
```

同样，对 AllocaInst / GlobalVar 作为目标指针时有简化形式。

### 3.4 BinaryInst

`%result = add/sub/mul/sdiv/srem i32 %lhs, %rhs`

```mips
# 共同前缀：加载操作数
lw   $t0, offset(%lhs)($sp)
lw   $t1, offset(%rhs)($sp)

# 按 BinaryOp 选择指令：
# ADD  → addu  $t2, $t0, $t1
# SUB  → subu  $t2, $t0, $t1
# MUL  → mul   $t2, $t0, $t1
# DIV  → div   $t0, $t1  +  mflo $t2
# REM  → div   $t0, $t1  +  mfhi $t2

sw   $t2, offset(%result)($sp)
```

**操作数为常量时的优化**：如果 `%lhs` 或 `%rhs` 是 `ConstantInt`，可以直接 `li` 到寄存器，不需要从栈 load。这是一个简单但有效的"常量操作数特判"。

### 3.5 IcmpInst

`%cmp = icmp slt/sgt/sle/sge/eq/ne i32 %lhs, %rhs`

```mips
lw   $t0, offset(%lhs)($sp)
lw   $t1, offset(%rhs)($sp)

# 按 IcmpPred 选择：
# SLT → slt $t2, $t0, $t1
# SGT → sgt $t2, $t0, $t1
# SLE → sle $t2, $t0, $t1
# SGE → sge $t2, $t0, $t1
# EQ  → seq $t2, $t0, $t1
# NE  → sne $t2, $t0, $t1

sw   $t2, offset(%cmp)($sp)
```

结果是 0 或 1（i1 在栈上仍占 4 字节）。

### 3.6 ZextInst / TruncInst

`%ext = zext i1 %v to i32` —— 在全栈分配下，i1 已经以 i32 形式存在栈上（值为 0 或 1），因此 ZextInst 是 **no-op**：直接复制源值到目标槽。

`%trunc = trunc i32 %v to i8` —— 取低 8 位：

```mips
lw   $t0, offset(%v)($sp)
andi $t0, $t0, 0xFF
sw   $t0, offset(%trunc)($sp)
```

### 3.7 BranchInst

**无条件分支** `br label %target`：

```mips
j    label_target
```

**条件分支** `br i1 %cond, label %if_true, label %if_false`：

```mips
lw   $t0, offset(%cond)($sp)
bnez $t0, label_if_true
j    label_if_false
```

### 3.8 CallInst

`%ret = call i32 @func(i32 %a, i32 %b, ...)`

**步骤 1：设置参数**

```mips
# 参数 0 → $a0
lw   $a0, offset(%a)($sp)
# 参数 1 → $a1
lw   $a1, offset(%b)($sp)
# ...
# 参数 4+ → sw 到当前栈帧底部的溢出区
```

**步骤 2：调用**

对于用户函数：`jal func_label`  
对于库函数：通过 syscall 实现（见 2.2 节）

**步骤 3：取回返回值**

```mips
sw   $v0, offset(%ret)($sp)   # 非 void 才需要
```

**库函数特殊处理（以 putint 为例）**：

```mips
lw   $a0, offset(%arg)($sp)   # 参数
li   $v0, 1                   # syscall 号
syscall
```

### 3.9 ReturnInst

`ret i32 %val` / `ret void`

```mips
# 有返回值时：
lw   $v0, offset(%val)($sp)

# epilogue：恢复 $ra，还原 $sp，跳回
lw   $ra, ra_offset($sp)
addiu $sp, $sp, frame_size
jr   $ra
```

### 3.10 GetElementPtrInst

`%addr = getelementptr [N x i32], ptr %base, i32 0, i32 %idx`

GEP 只做地址计算，不访问内存：

```mips
# 两索引形式：base 是数组指针，第一个索引 0，第二个索引 %idx
# 结果地址 = base_addr + idx * sizeof(element)

# 取 base 地址
# (如果 base 是 AllocaInst → addiu $t0, $sp, alloca_offset)
# (如果 base 是 GlobalVar → la $t0, label)
# (否则 → lw $t0, offset(%base)($sp))

lw   $t1, offset(%idx)($sp)   # 取索引
sll  $t1, $t1, 2              # idx * 4 (对 i32 元素)
addu $t0, $t0, $t1            # base + offset
sw   $t0, offset(%addr)($sp)  # 存结果地址
```

对于 `i8` 元素，不需要 `sll`（乘 1）。

### 3.11 PhiInst

PhiInst **不在自己所在块生成代码**。而是在**每个前驱块的末尾**（跳转指令发出之前），为该前驱对应的 incoming value 生成 move。

具体策略见 §2.6 "Phi 节点下降"。

---

## 四、大框架设计

### 4.1 Emission Pipeline 总览

```
                    ir::Module (只读)
                         │
                         ▼
          ┌─────────── MipsEmitter ───────────┐
          │                                    │
          │  1. emitDataSegment()              │
          │     遍历 Module::GetGlobalVars()   │
          │     输出 .data 段                   │
          │                                    │
          │  2. emitTextSegment()              │
          │     输出 .text                     │
          │     输出 jal main + exit 的引导代码  │
          │     遍历 Module::GetFunctions()    │
          │         │                          │
          │         ▼                          │
          │     FunctionEmitter (per-function) │
          │       ├─ 1. buildStackFrame()      │
          │       │    扫描所有 BB 的指令       │
          │       │    为每个 Value 分配 offset │
          │       │    计算 frame_size          │
          │       │                            │
          │       ├─ 2. emitPrologue()         │
          │       │    addiu $sp, -frame_size  │
          │       │    sw $ra, ...             │
          │       │    参数从 $a0-$a3/栈 → 栈槽 │
          │       │                            │
          │       ├─ 3. emitBody()             │
          │       │    遍历每个 BasicBlock      │
          │       │    输出块标签               │
          │       │    对每条 Instruction 调用  │
          │       │    emitInstruction()       │
          │       │                            │
          │       └─ 4. (epilogue 在 ReturnInst│
          │            的翻译中内联生成)         │
          └────────────────────────────────────┘
```

### 4.2 关键设计原则

1. **后端不修改 IR**：只读遍历 `ir::Module`，通过 `dynamic_cast` 识别指令类型。
2. **FunctionEmitter 是无状态的最小单元**：每个函数独立处理，不跨函数共享栈帧状态。
3. **指令选择集中在一处**：`emitInstruction(ir::Instruction*)` 是一个 if-else / dynamic_cast 链，每种指令类型对应一个 `emitXxx` 方法。不允许在其他地方散落 MIPS 指令输出。
4. **Value 位置查询统一**：提供一个 `loadValueToReg(ir::Value*, const string& reg)` 辅助函数，它内部判断 Value 是 ConstantInt、GlobalVar、AllocaInst 还是普通 SSA 值，统一处理到寄存器的加载。

### 4.3 Value 位置管理

后端最核心的数据结构是 **value-to-location 映射**：

```cpp
struct StackSlot {
    int offset;     // 相对 $sp 的偏移
    int size;       // 占用字节数（4 for i32, N*4 for array）
    bool is_addr;   // true 表示这个槽存的是一个地址（如 alloca 的空间起始）
};

std::unordered_map<const ir::Value*, StackSlot> value_map_;
```

**分配规则**（在 `buildStackFrame` 中一趟完成）：

1. 为 `$ra` 保留 4 字节（offset = 0，即栈帧最底部）
2. 遍历函数的所有 `Argument`，每个分配 4 字节（用于保存从 `$a0`–`$a3` / 栈传入的参数）
3. 遍历函数所有 BasicBlock 的所有 Instruction：
   - **AllocaInst**：按照 alloca 的类型计算大小（标量 4 字节，数组 `[N x T]` → N × sizeof(T)），分配对应大小的空间，`is_addr = true`
   - **其他产生结果的指令**（Load, Binary, Icmp, Call(非void), GEP, Zext, Trunc, Phi）：每个分配 4 字节
   - **不产生结果的指令**（Store, Branch, Return, void Call）：不分配
4. 如果函数内存在 `CallInst`，还需要预留**参数溢出区**（取所有 CallInst 中参数数量最大值，若 > 4 则为多余的参数预留 `(max_args - 4) * 4` 字节）
5. **frame_size** = 所有槽位的总大小（4 字节对齐）

### 4.4 MIPS 标签命名

| IR 实体 | MIPS 标签 |
|---------|----------|
| 函数 `@main` | `main:` |
| 函数 `@foo` | `func_foo:` |
| 基本块 `entry` in `@foo` | `func_foo_entry:` |
| 基本块 `if.then.2` in `@foo` | `func_foo_if_then_2:` |
| 全局变量 `@a` | `global_a:` |
| 全局字符串 | `str_0:`, `str_1:`, ... |

标签需全局唯一，用 `函数名_块名` 的方式可以保证不冲突。对块名中的 `.` 替换为 `_`（MARS 标签不支持 `.`）。

### 4.5 .data 段布局

```mips
.data
global_a:  .word 0              # int a;（初值 0）
global_b:  .word 42             # int b = 42;
global_arr: .word 1, 2, 3, 0, 0 # int arr[5] = {1, 2, 3};
str_0:     .asciiz "Hello\n"    # printf 中的字符串字面量
```

遍历 `Module::GetGlobalVars()` 输出：
- 标量：`.word <init_value>`（无初始化则 `.word 0`）
- 数组：`.word <v0>, <v1>, ...`（不足补 0）
- 字符串（作为全局常量存在）：`.asciiz "..."`

### 4.6 .text 段引导

```mips
.text
jal  main
li   $v0, 10
syscall              # exit
```

程序入口跳转到 `main`，`main` 返回后执行 exit syscall（10 号）。

---

## 五、与现有 IR 的集成点

后端需要遍历的 IR 接口，全部是只读的：

### 5.1 Module 层

```cpp
const auto& globals = module.GetGlobalVars();     // vector<unique_ptr<GlobalVar>>
const auto& functions = module.GetFunctions();     // vector<unique_ptr<Function>>
```

GlobalVar 接口：
- `gv->GetName()` → 变量名（如 "a"）
- `gv->GetType()` → PointerType*，`pointee` 为实际类型
- `gv->GetInitializer()` → Constant*（ConstantInt / ConstantArray / nullptr）
- `gv->IsConstant()` → bool

### 5.2 Function 层

```cpp
func->GetName()                           // "main", "foo"
func->GetBlocks()                         // vector<unique_ptr<BasicBlock>>
func->GetBlocks().empty()                 // true = 外部声明（库函数）
func->GetArguments()                      // vector<unique_ptr<Argument>>
func->GetType()                           // FunctionType*
```

FunctionType 可以获取返回类型和参数类型：
- `dynamic_cast<FunctionType*>(func->GetType())->GetReturnType()`
- `dynamic_cast<FunctionType*>(func->GetType())->GetParamTypes()`

### 5.3 BasicBlock 层

```cpp
bb->GetName()                             // "entry", "if.then", "for.cond"
bb->GetInstructions()                     // list<unique_ptr<Instruction>>
```

### 5.4 Instruction 识别

后端用 `dynamic_cast` 识别每种指令：

```cpp
if (auto* bin = dynamic_cast<ir::BinaryInst*>(inst)) {
    // bin->GetOp(), bin->GetLhs(), bin->GetRhs()
} else if (auto* load = dynamic_cast<ir::LoadInst*>(inst)) {
    // load->GetPointerOperand()
} else if (auto* store = dynamic_cast<ir::StoreInst*>(inst)) {
    // store->GetValueOperand(), store->GetPointerOperand()
} else if (auto* br = dynamic_cast<ir::BranchInst*>(inst)) {
    // br->IsConditional(), br->GetDest(), br->GetCond(), br->GetIfTrue(), br->GetIfFalse()
} else if (auto* call = dynamic_cast<ir::CallInst*>(inst)) {
    // call->GetCallee(), call->GetArg(i), call->GetNumArgs()
} else if (auto* ret = dynamic_cast<ir::ReturnInst*>(inst)) {
    // ret->GetRetVal() (nullptr for void)
} else if (auto* gep = dynamic_cast<ir::GetElementPtrInst*>(inst)) {
    // gep->GetPointerOperand(), gep->GetIndex(0), gep->GetIndex(1)
} else if (auto* icmp = dynamic_cast<ir::IcmpInst*>(inst)) {
    // icmp->GetPredicate(), icmp->GetLhs(), icmp->GetRhs()
} else if (auto* zext = dynamic_cast<ir::ZextInst*>(inst)) {
    // zext->GetOperandValue()
} else if (auto* trunc = dynamic_cast<ir::TruncInst*>(inst)) {
    // trunc->GetOperandValue()
} else if (auto* phi = dynamic_cast<ir::PhiInst*>(inst)) {
    // phi->GetNumIncoming(), phi->GetIncomingValue(i), phi->GetIncomingBlock(i)
} else if (auto* alloca = dynamic_cast<ir::AllocaInst*>(inst)) {
    // no-op in emitInstruction
}
```

### 5.5 Value 类型识别

当需要将一个 `ir::Value*` 加载到寄存器时，需要判断它的实际类型：

| Value 类型 | 如何加载到 `$t` |
|-----------|----------------|
| `ConstantInt` | `li $t, value` |
| `GlobalVar` | `la $t, global_label`（取地址） |
| `AllocaInst` | `addiu $t, $sp, alloca_offset`（取栈上地址） |
| `Argument` / 其他 `Instruction` | `lw $t, offset($sp)`（从栈槽加载值） |
| `Function` | `la $t, func_label`（函数地址，罕见） |

---

## 六、实现路线图

### M0：骨架搭建（不生成任何有效指令）

**目标**：在 `main.cpp` 中接入 MIPS 后端调用，能编译运行并输出空的 `mips.txt`。

- 创建 `include/mips/MipsEmitter.h`、`src/mips/MipsEmitter.cpp`
- 创建 `include/mips/FunctionEmitter.h`、`src/mips/FunctionEmitter.cpp`
- 修改 `CMakeLists.txt` 添加 `src/mips/*.cpp`
- 修改 `main.cpp`：在 `result.module->Print(...)` 之后调用 `MipsEmitter` 输出到 `mips.txt`
- `MipsEmitter::Emit()` 遍历 Module 但只输出注释（`# TODO`）

**验证**：编译通过，运行后生成 `mips.txt`（可能只有注释/空内容）。

### M1：纯算术 + return（单函数，无控制流）

**目标**：`int main() { return 1 + 2; }` 能在 MARS 中正确退出。

**涉及 IR 指令**：`BinaryInst(ADD/SUB/MUL/DIV/REM)`、`ReturnInst`、`ConstantInt`

**关键实现**：
- `FunctionEmitter::buildStackFrame()` —— 扫描指令，分配栈槽
- `FunctionEmitter::emitPrologue()` —— `addiu $sp`, `sw $ra`
- `emitBinaryInst()` —— 加载操作数、算术、存结果
- `emitReturnInst()` —— 取返回值到 `$v0`，epilogue
- `.text` 段引导代码（`jal main` + exit syscall）

**测试**：
```c
int main() { return 1 + 2; }          // 预期 exit code 3
int main() { return 10 - 3 * 2; }     // 预期 exit code 4
int main() { return 7 / 2; }          // 预期 exit code 3
int main() { return 7 % 3; }          // 预期 exit code 1
```

### M2：全局变量（.data 段）

**目标**：全局 int 变量/常量的读写。

**涉及 IR 指令**：`LoadInst`、`StoreInst`（操作对象为 `GlobalVar`）

**关键实现**：
- `emitDataSegment()` —— 遍历 GlobalVars，输出 `.word` / `.asciiz`
- `loadValueToReg()` 中增加 GlobalVar 的 `la` 支持
- `emitLoadInst()` / `emitStoreInst()`

**测试**：
```c
int a = 5;
int main() { return a; }              // 预期 5
int a; int main() { a = 42; return a; }  // 预期 42
```

### M3：库函数调用（getint / putint / putch / putstr）

**目标**：基本 IO 功能。

**涉及 IR 指令**：`CallInst`（callee 为库函数）

**关键实现**：
- 识别库函数名，生成对应 syscall 序列
- 字符串字面量输出到 `.data` 段（`.asciiz`）

**测试**：
```c
int main() {
    int a = getint();
    putint(a);
    putch(10);  // 换行
    return 0;
}
```

### M4：多基本块 + 分支

**目标**：if/else、for 循环。

**涉及 IR 指令**：`BranchInst`（条件 + 无条件）、`IcmpInst`、`ZextInst`

**关键实现**：
- `emitBranchInst()` —— j / bnez+j
- `emitIcmpInst()` —— slt/seq/...
- 基本块标签输出
- `emitZextInst()` —— 通常 no-op（copy）

**测试**：
```c
int main() {
    int a = getint();
    if (a > 0) { putint(1); } else { putint(0); }
    return 0;
}
```

### M5：Phi 节点下降

**目标**：正确处理 Mem2Reg 产生的 φ 节点。

**关键实现**：
- 在遍历 BasicBlock 的指令之前，先收集该块的所有 PhiInst
- 在 `emitBranchInst()` 输出跳转之前，为目标块的 phi 节点生成 move
- 实现两阶段 parallel copy（先 load 全部 incoming 到临时寄存器，再 store）

**测试**：
```c
int main() {
    int a = getint();
    int b;
    if (a > 0) { b = 1; } else { b = 2; }
    putint(b);  // 这里 b 是 phi 节点
    return 0;
}
```

### M6：数组

**目标**：局部数组 + 全局数组的读写。

**涉及 IR 指令**：`AllocaInst`（数组）、`GetElementPtrInst`、`LoadInst` + `StoreInst`

**关键实现**：
- `buildStackFrame()` 中为数组 alloca 分配 N×4 字节
- `emitGetElementPtrInst()` —— 地址计算（base + idx * element_size）
- 全局数组在 .data 段输出 `.word` 序列

**测试**：
```c
int main() {
    int a[5] = {1, 2, 3, 4, 5};
    putint(a[2]);  // 预期 3
    return 0;
}
```

### M7：用户自定义函数调用

**目标**：完整的函数调用约定，包括参数 > 4 个时的栈传参。

**涉及 IR 指令**：`CallInst`（callee 为用户函数）

**关键实现**：
- 调用前设置 `$a0`–`$a3`，超出部分压栈
- `FunctionEmitter::emitPrologue()` 中保存 `$a0`–`$a3` 到栈槽
- 递归函数正确保存/恢复 `$ra`

**测试**：
```c
int add(int a, int b) { return a + b; }
int main() { putint(add(3, 4)); return 0; }  // 预期 7

int fib(int n) {
    if (n <= 1) return n;
    return fib(n-1) + fib(n-2);
}
int main() { putint(fib(10)); return 0; }    // 预期 55
```

### M8：TruncInst + i8/char 支持

**目标**：字符类型相关操作（getchar、putch、char 数组）。

**涉及 IR 指令**：`TruncInst`、i8 相关 Load/Store

**测试**：
```c
int main() {
    int c = getchar();
    putch(c);
    return 0;
}
```

---

## 七、文件结构规划

```
include/mips/
    MipsEmitter.h          # 顶层驱动：持有 ostream&，遍历 Module
    FunctionEmitter.h      # 每函数处理：栈帧布局、prologue/epilogue、指令翻译

src/mips/
    MipsEmitter.cpp
    FunctionEmitter.cpp
```

### 7.1 MipsEmitter 接口草案

```cpp
namespace mips {

class MipsEmitter {
public:
    explicit MipsEmitter(std::ostream& os, const ir::Module& module);
    void Emit();

private:
    void emitDataSegment();
    void emitTextSegment();
    void emitGlobalVar(const ir::GlobalVar& gv);

    std::ostream& os_;
    const ir::Module& module_;
    int string_literal_counter_ = 0;
};

} // namespace mips
```

### 7.2 FunctionEmitter 接口草案

```cpp
namespace mips {

class FunctionEmitter {
public:
    FunctionEmitter(std::ostream& os, const ir::Function& func,
                    const ir::Module& module);
    void Emit();

private:
    void buildStackFrame();
    void emitPrologue();
    void emitBody();
    void emitEpilogue();

    void emitInstruction(const ir::Instruction* inst);
    void emitBinaryInst(const ir::BinaryInst* inst);
    void emitLoadInst(const ir::LoadInst* inst);
    void emitStoreInst(const ir::StoreInst* inst);
    void emitBranchInst(const ir::BranchInst* inst);
    void emitCallInst(const ir::CallInst* inst);
    void emitReturnInst(const ir::ReturnInst* inst);
    void emitGetElementPtrInst(const ir::GetElementPtrInst* inst);
    void emitIcmpInst(const ir::IcmpInst* inst);
    void emitZextInst(const ir::ZextInst* inst);
    void emitTruncInst(const ir::TruncInst* inst);
    void emitPhiMoves(const ir::BasicBlock* from, const ir::BasicBlock* to);

    // 辅助：将任意 Value 加载到指定寄存器
    void loadValueToReg(const ir::Value* val, const std::string& reg);
    // 辅助：将寄存器的值存回 Value 的栈槽
    void storeRegToValue(const std::string& reg, const ir::Value* val);
    // 辅助：获取基本块的 MIPS 标签名
    std::string getBlockLabel(const ir::BasicBlock* bb) const;
    // 辅助：判断是否为库函数
    bool isLibFunction(const std::string& name) const;

    std::ostream& os_;
    const ir::Function& func_;
    const ir::Module& module_;

    std::unordered_map<const ir::Value*, int> value_offset_;  // Value → $sp offset
    int frame_size_ = 0;
    int ra_offset_ = 0;

    // Phi lowering：收集每个块的 phi 信息
    std::unordered_map<const ir::BasicBlock*,
                       std::vector<const ir::PhiInst*>> block_phis_;
};

} // namespace mips
```

---

## 八、关键设计决策总结

| 决策 | 选择 | 理由 |
|------|------|------|
| Value 存储策略 | 全栈分配 | 最简单、最易调试；正确优先 |
| 临时寄存器 | `$t0`–`$t3` | 足够做二元运算和 load/store 搬运 |
| $ra 保存 | 所有函数都保存 | 不需要叶函数分析，简单统一 |
| Phi lowering | 前驱块末尾插入 move + 两阶段策略 | 栈上分配天然避免覆盖 |
| 帧指针 | 不使用 $fp（初版） | $sp 在函数体内不变，直接用 $sp+offset |
| 标签命名 | `funcname_blockname` | 保证全局唯一，MARS 兼容 |
| 指令选择 | dynamic_cast 链 | 和 IR 的 Print 一致，简洁明了 |

---

## 九、与优化阶段的接口预留

课程要求"预留优化接口"。我们的设计天然满足：

1. **IR 层优化**（如常量折叠、死代码消除）在 `pass/` 下新增 FunctionPass，在 `main.cpp` 中 Mem2Reg 之后、MIPS 生成之前插入即可。
2. **MIPS 层优化**（如寄存器分配）可以在 `FunctionEmitter` 内部替换 `value_offset_` 的分配策略，从全栈改为图着色。
3. **开关控制**：`main.cpp` 中已有 `kEnableMem2Reg` 模式，新增 `kEnableRegAlloc`、`kEnablePeephole` 等 bool 即可。

---

## 十、风险点与注意事项

### 10.1 PhiInst 的 incoming 不在 def-use 链中

如 Mem2Reg 设计文档所述，`PhiInst::incoming_` 是独立存储的 `vector<IncomingPair>`，不在 `User::operands_` 体系中。后端必须通过 `GetIncomingValue(i)` / `GetIncomingBlock(i)` 显式访问，不能依赖 `GetOperand(i)`。

### 10.2 GEP 的两种形式

IR 中的 GEP 有两种构造形式（对应两种 IR 文本形式）：

1. **两索引**：`getelementptr [N x T], ptr %base, i32 0, i32 %idx` —— base 是指向数组的指针（如局部数组的 alloca 或全局数组地址），结果是元素地址
2. **单索引**：`getelementptr T, ptr %base, i32 %idx` —— base 已经是指向元素的指针（如函数参数传入的数组指针），结果是 base + idx * sizeof(T)

在 `GetElementPtrInst` 的实现中，通过 `GetNumOperands()` 区分：操作数个数 3 为两索引（operand 0=base, 1=idx0, 2=idx1），操作数个数 2 为单索引（operand 0=base, 1=idx）。

### 10.3 void 函数调用

`CallInst` 的结果类型可能是 void（如 `call void @putint(i32 %x)`），此时不需要为 CallInst 分配栈槽，也不需要 `sw $v0`。通过检查 `call->GetType()` 是否为 `VoidType` 来判断。

### 10.4 i8 与 i32 的混用

SysY 的 char 类型在 IR 中表示为 i8。部分操作（如 `putch`）接受 i8 值，但算术运算全部在 i32 上进行。后端需要注意：
- 栈上所有值统一占 4 字节（即使 i8 也用 `lw`/`sw`，不用 `lb`/`sb`，除非明确操作 char 数组的某个字节）
- 全局 char 数组的 `.data` 段用 `.byte` 而非 `.word`（或者统一扩展为 `.word`——但 `putstr` 需要 `.asciiz` 格式的连续字节）

### 10.5 MARS 的 .asciiz 与字符串

`putstr` 对应 syscall 4，需要 `$a0` 指向以 `\0` 结尾的字符串。IR 中的字符串字面量通常作为全局 i8 数组存在。在 `.data` 段中用 `.asciiz "..."` 输出最方便。

---

## 十一、下一步行动

完成本文档的阅读和理解后，我们按以下顺序推进：

1. **M0**：搭建骨架——创建文件、修改 CMake、接入 main.cpp
2. **M1**：纯算术 + return——实现 `buildStackFrame`、`emitPrologue`、`emitBinaryInst`、`emitReturnInst`
3. 每完成一个里程碑，用 MARS 4.5 验证，再推进下一个

**不要跳过任何一步**。每个里程碑都是在前一个的基础上增量添加，框架一旦搭好，后续每步只是"在 emitInstruction 的 if-else 链里加一个新分支"。

---

*本文档作为 MIPS 后端开发的完整先导参考。后续各里程碑的实现细节、踩坑记录将在独立笔记中补充。*

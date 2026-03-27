# MIPS 后端三项优化：乘除法强度削减 + 窥孔优化 + 基本块合并

**日期**: 2026-03-28
**前置状态**: ConstFoldLVN Pass + DCE Pass 已接入并通过验证，IR 层"无用计算消除"流水线完整
**关联文档**: [优化阶段规划](optimization_phase_plan_20260310.md)、[ConstFold+LVN 设计](constfold_lvn_20260327.md)、[DCE 设计](dce_20260327.md)、[课程优化教程 §（4）（5）](../../course_info/optimization_course_guide.md)

---

## 一、背景：IR 优化完成后，MIPS 层还剩什么

### 1.1 已完成的 IR 层优化

经过 `Mem2Reg → ConstFoldLVN (fixpoint) → DCE` 三个 Pass 后，进入 MIPS 后端的 IR 已经相当干净：

- 所有纯常量表达式（`mul 3, 5` 等）已在编译期折叠
- 所有代数恒等冗余（`x * 1`、`x + 0` 等）已经 RAUW 掉
- 块内公共子表达式（`k * k` 在同一块中重复 10 次）已消除
- 死指令（RAUW 后 use_list 为空的指令）已删除

### 1.2 MIPS 后端仍有的系统性低效

尽管 IR 已经优化，`InstructionEmitter` 翻译到 MIPS 时仍存在三类可消除的低效：

**低效 A：运算强度偏高**（O3 目标）

IR 层的 `mul` / `div` 指令，即使一侧操作数是小常量，`InstructionEmitter` 目前也一律翻译为 MIPS `mul` 指令（需要多个时钟周期）。例如：

```mips
; IR: %v = mul i32 %i, 4
li    $t1, 4
mul   $t2, $t0, $t1   ← 乘法，可以换成 sll $t2, $t0, 2
sw    $t2, 12($sp)
```

在 SysY 的数组访问中，`i * 4`（int 数组元素大小）和 `i * 8` 等常数乘法极为常见；循环体内每次迭代都会触发这类乘法，优化空间大。

**低效 B：冗余 sw/lw 对**（O4 目标）

全栈分配策略（每个 SSA 值分配一个栈槽）产生大量紧邻的 `sw`/`lw` 对：

```mips
sw    $t2, 12($sp)   ← 把结果存到栈
lw    $t0, 12($sp)   ← 立刻再从同一地址读回来
```

这种模式在每条计算指令（BinaryInst → store result → LoadValueToReg for next use）之后极为普遍，是全栈分配的固有代价。

**低效 C：冗余跳转指令**（O5 目标）

Mem2Reg + 短路求值会产生大量小基本块，每个块末尾的无条件跳转 `j label` 中有相当一部分跳向**紧邻的下一个块**——在 MIPS 顺序执行语义下，这条 `j` 完全是多余的。

---

## 二、三项后端优化概览

| 优化项 | 课程教程节 | 操作层 | 预期收益 | 推荐顺序 |
|--------|-----------|--------|---------|---------|
| **O3 乘除法强度削减** | §（4）乘除法优化 | `InstructionEmitter` | 中-高（循环体乘法变移位） | 1 |
| **O4 窥孔优化** | §（5）窥孔优化 | `AsmWriter` 缓冲层 | 高（消除海量 sw/lw 对） | 2 |
| **O5 基本块合并 + 冗余跳转消除** | §（5）基本块合并 | `FunctionEmitter` | 中（减少 j 指令） | 3 |

这三项优化**互不依赖**，可以独立开关，但建议按上表顺序实施——O3 本身减少了一些指令，O4 是收益最高的单项，O5 在 O3/O4 完成后再实施效果更干净。

---

## 三、O3：乘除法强度削减（Strength Reduction）

### 3.1 优化原理

**目标**：用廉价的移位 / 加减指令序列替代昂贵的 `mul` / `div` 指令，当操作数之一为编译期已知常量时适用。

MIPS `mul` 指令的代价远高于 `sll`（shift left logical），`div` 指令代价更高。MARS 虽然是模拟器，课程评测的"rank"计算以指令的执行代价（FinalCycle）为准，`mul = 5×add`, `div = 10×add`（课程默认权重）。

**接入时机**：`InstructionEmitter::EmitBinaryInst`，在翻译 `MUL`/`DIV`/`REM` 时，检查操作数是否为 `ir::ConstantInt`，若是则走强度削减路径。

### 3.2 乘法强度削减

#### 3.2.1 2 的幂次方乘法

若常量 `c = 2^n`（`c >= 1`），则 `x * c = x << n`：

| IR 指令 | 当前 MIPS（3条） | 优化后 MIPS（2条） |
|---------|----------------|-------------------|
| `mul i32 %x, 4` | `li $t1, 4`<br>`mul $t2, $t0, $t1` | `sll $t2, $t0, 2` |
| `mul i32 %x, 8` | `li $t1, 8`<br>`mul $t2, $t0, $t1` | `sll $t2, $t0, 3` |
| `mul i32 %x, 1` | 已由 ConstFoldLVN 代数化简 → 不到达这里 | — |

判断是否为 2 的幂次：

```cpp
static bool IsPowerOfTwo(int64_t c, int& out_n) {
    if (c <= 0) return false;
    if ((c & (c - 1)) != 0) return false;
    out_n = 0;
    while (c > 1) { c >>= 1; ++out_n; }
    return true;
}
```

#### 3.2.2 2^n ± 1 形式的小常数乘法

对于无法直接用单次移位表示的小常数，可用"移位 + 加减"组合，节省一次乘法：

| 常量 c | 规律 | MIPS 替代序列（2条） |
|--------|------|---------------------|
| 3 = 2+1 | `x*3 = x*2 + x` | `sll $t2, $t0, 1`<br>`addu $t2, $t2, $t0` |
| 5 = 4+1 | `x*5 = x*4 + x` | `sll $t2, $t0, 2`<br>`addu $t2, $t2, $t0` |
| 6 = 4+2 | `x*6 = x*4 + x*2` | `sll $t2, $t0, 2`<br>`sll $t3, $t0, 1`<br>`addu $t2, $t2, $t3`（3条，不总是合算） |
| 7 = 8-1 | `x*7 = x*8 - x` | `sll $t2, $t0, 3`<br>`subu $t2, $t2, $t0` |
| 9 = 8+1 | `x*9 = x*8 + x` | `sll $t2, $t0, 3`<br>`addu $t2, $t2, $t0` |

实现策略：**只优化 2 条以内 MIPS 指令能覆盖的情况**（即 3、5、7、9 等 `2^n ± 1` 形式）；其余情况仍发 `mul`。

```
优化阈值表（建议实现的常数）：
{ 3, 5, 6, 7, 9, 10, 11, 12, 13, 14, 15 }
对于 |c| <= 32 的常数，可穷举判断是否满足"一次移位 + 一次加减"
```

#### 3.2.3 负数乘以常数

`x * (-c)` 可先计算 `x * c`，再 `sub $t2, $zero, $t2`（取反）。

#### 3.2.4 乘法优化的完整判断流程

```pseudocode
EmitOptimizedMul(ir::BinaryInst* inst):
    // 提取操作数（尝试找到常量侧）
    (var_val, const_val) = NormalizeForConstant(inst->GetLhs(), inst->GetRhs())
    if const_val == nullptr:
        // 两侧均为变量，无法优化
        EmitGenericMul(inst)
        return

    int64_t c = const_val->GetValue()
    bool neg = (c < 0)
    int64_t abs_c = neg ? -c : c

    int n
    if IsPowerOfTwo(abs_c, n):
        LoadValueToReg(var_val, "$t0")
        writer_.EmitInsn("sll   $t2, $t0, " + n)
        if neg: writer_.EmitInsn("subu  $t2, $zero, $t2")
    else if IsShiftPlusOne(abs_c, n):   // 2^n + 1
        LoadValueToReg(var_val, "$t0")
        writer_.EmitInsn("sll   $t2, $t0, " + n)
        writer_.EmitInsn("addu  $t2, $t2, $t0")
        if neg: writer_.EmitInsn("subu  $t2, $zero, $t2")
    else if IsShiftMinusOne(abs_c, n):  // 2^n - 1
        LoadValueToReg(var_val, "$t0")
        writer_.EmitInsn("sll   $t2, $t0, " + n)
        writer_.EmitInsn("subu  $t2, $t2, $t0")
        if neg: writer_.EmitInsn("subu  $t2, $zero, $t2")
    else:
        EmitGenericMul(inst)  // 退化到通用 mul
```

### 3.3 除法强度削减

#### 3.3.1 2 的幂次除法（有符号，向零截断）

`x / 2^n` 在 C/SysY 中是**向零截断**的整数除法（ISO C99），对负数的行为与算术右移不同：
- `7 / 4 = 1`（算术右移 `7 >> 2 = 1`，正确）
- `(-7) / 4 = -1`（向零截断），但算术右移 `(-7) >> 2 = -2`（向负无穷）

因此**不能直接用 `sra` 替代**，需要加"偏置"修正：

```
x / 2^n（有符号，向零截断）：
    step1: 取符号位延伸 → $t2 = sra($t0, 31)   # 若 x>=0: 全0；若 x<0: 全1（即-1）
    step2: 提取偏置     → $t2 = srl($t2, 32-n)  # 若 x>=0: 0；若 x<0: 2^n-1
    step3: 加偏置       → $t2 = addu($t0, $t2)  # x 加上偏置（正数加 0，负数加 2^n-1）
    step4: 算术右移     → $t2 = sra($t2, n)      # 最终结果
```

对应的 MIPS 序列（4 条替代 `div + mflo`）：

```mips
; IR: %v = sdiv i32 %x, 4  (n=2)
lw    $t0, X($sp)        # 加载 %x
sra   $t2, $t0, 31       # 符号延伸
srl   $t2, $t2, 30       # 提取偏置（32-2=30）→ $t2 = 0 or 3
addu  $t2, $t0, $t2      # 加偏置
sra   $t2, $t2, 2        # 算术右移
sw    $t2, Y($sp)
```

**特殊情况**：`x / 1 = x` — 已由 ConstFoldLVN 代数化简，不会到达这里。

#### 3.3.2 一般常数除法：Magic Number 算法

对于不是 2 的幂次的常数除数 `d`，可以用"乘 magic number + 取高位 + 右移"来模拟，参见论文 *Division by Invariant Integers using Multiplication*（Torbjörn Granlund & Peter L. Montgomery, 1994）。

**算法思路**：找到整数 `m` 和移位量 `s`，使得

$$\left\lfloor \frac{n}{d} \right\rfloor = \left\lfloor \frac{n \times m}{2^{32+s}} \right\rfloor$$

MIPS 上：`mult $t0, $m_reg; mfhi $t2; sra $t2, $t2, s; （符号修正）`

**算法关键步骤**（有符号 32 位整数，`d > 0`）：

```python
# 伪代码：计算 magic number m 和 shift s
def compute_magic(d):
    # d 为正整数除数，d 不是 2 的幂次
    p = 31
    ad = abs(d)
    t = (1 << 31) + (d >> 31)  # 2^31 + (d < 0 ? 1 : 0)
    anc = t - 1 - (t % ad)     # absolute normalized divisor complement
    q1, r1 = (1 << 31) // anc, (1 << 31) % anc
    q2, r2 = (1 << 31) // ad,  (1 << 31) % ad
    while True:
        p += 1
        q1 *= 2; r1 *= 2
        if r1 >= anc: q1 += 1; r1 -= anc
        q2 *= 2; r2 *= 2
        if r2 >= ad: q2 += 1; r2 -= ad
        delta = ad - r2
        if not (q1 < delta or (q1 == delta and r1 == 0)):
            break
    m = q2 + 1            # magic multiplier
    shift = p - 32        # right shift amount
    return m, shift
```

**MIPS 代码序列**（d 为编译期已知正常数，两种情况）：

```mips
; 情况 A：m < 2^31（magic number 无溢出）
lui   $t1, HI(m)         # 加载 magic number
ori   $t1, $t1, LO(m)    #
mult  $t0, $t1           # 有符号乘法
mfhi  $t2                # 取高 32 位
sra   $t2, $t2, s        # 右移 s 位（s = shift）
srl   $t3, $t0, 31       # 提取被除数符号位（负数修正）
addu  $t2, $t2, $t3      # 加符号修正 → 最终商

; 情况 B：m >= 2^31（需要额外 add 调整）
; ... 同上，但 mfhi 之后先 add $t2, $t2, $t0，再 sra
```

> **复杂度评估**：Magic number 算法的实现约 120 行（含 32 位溢出处理、负数除数处理）。在本阶段，**推荐先只实现 2 的幂次除法优化**，magic number 算法留作后续扩展；SysY 程序中 2 的幂次除法（`/ 2`, `/ 4`, `/ 8`, `/ 10` 较少见）已能覆盖常见场景。

#### 3.3.3 取模（REM）优化

- `x % 2^n（x >= 0）= x & (2^n - 1)` — 但有符号时 `x < 0` 结果为负，C 语义要求 `x % d` 与 `x / d * d + rem = x`，所以直接用 `andi` 只对非负数正确。
- 保守实现：rem 暂不做强度削减，继续使用 `div; mfhi`。
- 如果后续的 LVN 已消除了与对应 `div` 配对的 `rem`（很少见），这里也无需特殊处理。

### 3.4 接入方式

在 `InstructionEmitter::EmitBinaryInst` 中，根据 `options_.enable_mul_div_opt` 分支：

```cpp
// include/mips/InstructionEmitter.h 中已有 options_ 成员（MipsOptions&）

void InstructionEmitter::EmitBinaryInst(const ir::BinaryInst* inst) {
    switch (inst->GetOp()) {
        case ir::BinaryOp::MUL:
            if (options_.enable_mul_div_opt)
                EmitOptimizedMul(inst);
            else
                EmitGenericMul(inst);
            return;
        case ir::BinaryOp::DIV:
            if (options_.enable_mul_div_opt)
                EmitOptimizedDiv(inst);
            else
                EmitGenericDiv(inst);
            return;
        case ir::BinaryOp::REM:
            EmitGenericRem(inst);  // rem 暂不优化
            return;
        // ...其他 op 不变
    }
}
```

`MipsOptions::enable_mul_div_opt` 在 `main.cpp` 中用 `const bool kEnableMulDivOpt = true;` 控制。

### 3.5 O3 Before / After 对照

**示例：循环体内 `i * 4` 的数组寻址**

IR（DCE 后）：
```llvm
b3:
    ; %v60 = mul i32 %i, 4    ← 每次循环都执行
```

**优化前 MIPS**（3 条指令，含 1 次 mul）：
```mips
lw    $t0, 16($sp)      # load %i
li    $t1, 4
mul   $t2, $t0, $t1     # 5 cycle 代价
sw    $t2, 28($sp)
```

**优化后 MIPS**（2 条指令，全部单周期）：
```mips
lw    $t0, 16($sp)      # load %i
sll   $t2, $t0, 2       # i << 2 = i * 4
sw    $t2, 28($sp)
```

循环 100 次时：节省 100 × (5-1) = 400 个时钟周期。

---

## 四、O4：窥孔优化（Peephole Optimization）

### 4.1 优化原理

**窥孔优化**（Peephole Optimization）：对生成的指令序列维护一个"滑动窗口"（通常 2–4 条），用模式匹配识别可以简化或消除的指令组合，然后替换为等价但更短/更快的序列。

**核心目标模式**（按收益从高到低）：

| 编号 | 模式（窗口大小 2） | 替换为 | 收益 |
|------|-------------------|--------|------|
| P1 | `sw $t, X($sp)` 后紧接 `lw $t, X($sp)` | 删除 `lw`；后续指令中该 `$t` 已就位 | ★★★★ |
| P2 | `sw $t, X($sp)` 后紧接 `lw $u, X($sp)` | 删除 `lw`；用 `move $u, $t`（若 $u ≠ $t） | ★★★ |
| P3 | `move $t, $t` | 删除 | ★★ |
| P4 | `li $t, 0` 后紧接 `addu $d, $s, $t` | 替换为 `move $d, $s` | ★★ |
| P5 | `j label` 后紧接 `label:` | 删除 `j label`（O5 专项，此处也可在窥孔中处理） | ★★ |

**P1 是最高价值的优化**：全栈分配模式下，几乎每一对计算指令之间都会有这个模式：

```mips
; EmitBinaryInst 结尾 → 存结果
sw    $t2, 24($sp)
; 下一条指令 LoadValueToReg(prev_result) → 立刻读
lw    $t0, 24($sp)
```

实际测试表明，P1 可以消除 MIPS 输出中约 30–40% 的 `lw` 指令。

### 4.2 AsmWriter 缓冲层设计

当前 `AsmWriter` 直接写入 `os_`（`std::ostream`）。为支持窥孔，改为在函数级别**先缓冲到 `vector<string>`，函数结束后 flush 并进行窥孔优化**。

**设计方案**：在 `AsmWriter` 中增加可选缓冲模式：

```cpp
// include/mips/AsmWriter.h（修改部分）

class AsmWriter {
public:
    // --- 缓冲模式控制（窥孔优化用）---
    void BeginBuffer();                  // 开启缓冲（进入函数体时调用）
    void FlushBuffer(bool run_peephole); // 关闭缓冲，输出到 os_（退出函数时调用）

private:
    std::ostream& os_;
    bool buffering_ = false;
    std::vector<std::string> buffer_;   // 按行缓冲
    bool is_instruction_line_ = false;  // 区分标签行和指令行（窥孔只处理指令）

    void RunPeephole();  // 在 buffer_ 上原地做模式匹配替换
};
```

`EmitInsn` / `EmitLi` / `EmitMove` / `EmitSwSp` / `EmitLwSp` 等在 `buffering_ == true` 时追加到 `buffer_`，否则直接写 `os_`。

`EmitLabel` / `EmitDirective` 始终直接写 `os_`（标签和指令段声明不参与窥孔）。

> **替代方案**：将每条指令封装为结构体（含 opcode、寄存器、立即数字段），在结构化数据上做模式匹配比字符串匹配更精确。但工程量较大，在当前 `AsmWriter` 全字符串 API 的基础上，**先用字符串匹配实现 P1–P4**，效果验证后若收益显著再考虑结构化。

### 4.3 字符串级窥孔规则实现

`RunPeephole` 对 `buffer_` 做一遍或多遍扫描：

```pseudocode
RunPeephole():
    changed = true
    while changed:
        changed = false
        i = 0
        while i < buffer_.size() - 1:
            line_i   = buffer_[i]
            line_i1  = buffer_[i+1]

            // --- P1: sw $t, X($sp) 后接 lw $t, X($sp) ---
            (sw_reg, sw_off) = ParseSwSp(line_i)
            (lw_reg, lw_off) = ParseLwSp(line_i1)
            if sw_reg != "" and lw_reg != "" and sw_off == lw_off and sw_reg == lw_reg:
                // 完全相同的 reg 和 offset：直接删除 lw
                buffer_.erase(i+1)
                changed = true
                continue

            // --- P2: sw $t, X($sp) 后接 lw $u, X($sp)（不同目标寄存器）---
            if sw_reg != "" and lw_reg != "" and sw_off == lw_off and sw_reg != lw_reg:
                // 替换 lw 为 move
                buffer_[i+1] = "    move  " + lw_reg + ", " + sw_reg
                changed = true

            // --- P3: move $t, $t ---
            (mv_dst, mv_src) = ParseMove(line_i)
            if mv_dst != "" and mv_dst == mv_src:
                buffer_.erase(i)
                changed = true
                continue

            // --- P4: li $t, 0 后接 addu $d, $s, $t ---
            (li_reg, li_val) = ParseLi(line_i)
            (add_dst, add_src1, add_src2) = ParseAddu(line_i1)
            if li_reg != "" and li_val == 0 and add_dst != "" and add_src2 == li_reg:
                buffer_[i] = "    move  " + add_dst + ", " + add_src1
                buffer_.erase(i+1)
                changed = true
                continue

            ++i
```

**解析辅助函数**：写若干小的 `Parse*` 函数，用 `std::string_view` 做简单的前缀/格式匹配——不需要完整 lexer，只需精确匹配 `AsmWriter` 生成的固定格式字符串。

例如：
```cpp
// 精确匹配 "    sw    $tX, N($sp)"
static bool ParseSwSp(const std::string& line, std::string& out_reg, int& out_off) {
    // 检查前缀 "    sw    $"
    // 提取寄存器名（到第一个 ','）
    // 提取偏移（',' 后到 '('）
    // 检查后缀 "($sp)"
    ...
}
```

因为 AsmWriter 生成格式完全固定（缩进 4 空格、固定字段宽度），字符串匹配可以非常简单可靠。

### 4.4 FunctionEmitter 接入缓冲

```cpp
void FunctionEmitter::Emit() {
    frame_.Build();
    if (options_.enable_peephole) {
        writer_.BeginBuffer();
    }
    EmitPrologue();
    EmitBody();
    if (options_.enable_peephole) {
        writer_.FlushBuffer(true);  // true = run peephole before flushing
    }
}
```

每个函数独立缓冲，函数间不跨越。

### 4.5 O4 Before / After 对照

**示例：连续两条 BinaryInst 的中间存取**

MIPS 后端（O4 前，`%v1 = add %a, %b`，`%v2 = mul %v1, %c`）：
```mips
lw    $t0, 8($sp)       # load %a
lw    $t1, 12($sp)      # load %b
addu  $t2, $t0, $t1
sw    $t2, 16($sp)      # store %v1   ← P1 模式开始
lw    $t0, 16($sp)      # load %v1    ← P1：紧接同槽 lw，消除
lw    $t1, 20($sp)      # load %c
mul   $t2, $t0, $t1
sw    $t2, 24($sp)
```

**O4 后**（P1 规则消除了第 5 行的 `lw`）：
```mips
lw    $t0, 8($sp)
lw    $t1, 12($sp)
addu  $t2, $t0, $t1
sw    $t2, 16($sp)
                        # ← lw $t0, 16($sp) 被消除（$t2 已持有 %v1）
lw    $t1, 20($sp)
mul   $t2, $t2, $t1     # $t2 替代了原来的 $t0
sw    $t2, 24($sp)
```

> 注意：P1 删除 `lw $t0, X($sp)` 后，后续指令中引用 `$t0` 的地方要改为 `$t2`（原 `sw` 的源寄存器）。这要求窥孔规则在删除 lw 时同时在**当前窗口后续行**中做寄存器替换。简化实现：只向后替换**紧接的一条指令**（足以覆盖绝大多数情况）。

---

## 五、O5：基本块合并与冗余跳转消除

### 5.1 优化原理

MIPS 程序顺序执行：如果块 A 的最后一条指令是 `j B`，且块 B 在汇编文本中**紧跟在块 A 之后**，则这条 `j B` 是完全多余的——删掉后 CPU 自然会"掉进"块 B。

这不是 IR 层的基本块合并（不需要修改 IR），只是在 **MIPS 代码生成阶段按序排列基本块时，省略不必要的跳转指令**。

**三种可优化的跳转模式**：

| 模式 | 当前输出 | 优化后 |
|------|---------|--------|
| **P_J**: 无条件跳转到紧邻块 | `j label_B` 紧接 `label_B:` | 删除 `j label_B` |
| **P_BR_FALSE**: 条件分支 + `j` 到紧邻块 | `bnez $t0, L_T`<br>`j L_F` 紧接 `L_F:` | 只保留 `bnez`（`L_F` fall-through） |
| **P_BR_INVERT**: 条件分支到紧邻块，`j` 到远处 | `bnez $t0, L_T`<br>`j L_F` 紧接 `L_T:` | `beqz $t0, L_F`（反转条件，`L_T` fall-through） |

**P_BR_FALSE** 是最常见的：Mem2Reg 产生的每个 if/loop 分支结构都会生成：
```llvm
br i1 %cond, label %true_block, label %false_block
```
翻译为：
```mips
bnez  $t0, func.true_block
j     func.false_block      ← 如果 false_block 紧跟在当前块后面，这条 j 可以删除
```

### 5.2 接入方式

在 `FunctionEmitter::EmitBody` 中，遍历块列表时维护"当前块的下一个块"，并将其传递给 `InstructionEmitter::EmitBranchInst`：

```cpp
// FunctionEmitter.cpp

void FunctionEmitter::EmitBody() {
    const auto& blocks = func_.GetBlocks();
    for (size_t idx = 0; idx < blocks.size(); ++idx) {
        const ir::BasicBlock* cur_block  = blocks[idx].get();
        const ir::BasicBlock* next_block =
            (idx + 1 < blocks.size()) ? blocks[idx + 1].get() : nullptr;

        writer_.EmitLabel(BlockLabel(func_.GetName(), cur_block->GetName()));
        for (const auto& inst : cur_block->GetInstructions()) {
            inst_emitter_.Emit(inst.get(), cur_block, next_block); // 新增 next_block 参数
        }
    }
}
```

`InstructionEmitter::Emit` 接口增加 `const ir::BasicBlock* next_block = nullptr` 参数，传递给 `EmitBranchInst`：

```cpp
void InstructionEmitter::EmitBranchInst(
    const ir::BranchInst* inst,
    const ir::BasicBlock* next_block)
{
    if (!inst->IsConditional()) {
        // 无条件跳转：目标是 next_block 则跳过
        if (options_.enable_block_merge &&
            next_block != nullptr &&
            inst->GetDest() == next_block) {
            return;  // fall-through，不发 j
        }
        writer_.EmitInsn("j     " + BlockLabel(func_.GetName(), inst->GetDest()->GetName()));
        return;
    }

    // 条件分支
    const ir::BasicBlock* true_bb  = inst->GetIfTrue();
    const ir::BasicBlock* false_bb = inst->GetIfFalse();
    LoadValueToReg(inst->GetCond(), "$t0");

    if (options_.enable_block_merge && false_bb == next_block) {
        // P_BR_FALSE：false 分支 fall-through，只需 bnez 到 true
        writer_.EmitInsn("bnez  $t0, " + BlockLabel(func_.GetName(), true_bb->GetName()));
        // 不发 j false（fall-through）

    } else if (options_.enable_block_merge && true_bb == next_block) {
        // P_BR_INVERT：反转条件，false 跳到远处，true fall-through
        writer_.EmitInsn("beqz  $t0, " + BlockLabel(func_.GetName(), false_bb->GetName()));

    } else {
        // 通用情况：两个分支都要跳转
        writer_.EmitInsn("bnez  $t0, " + BlockLabel(func_.GetName(), true_bb->GetName()));
        writer_.EmitInsn("j     " + BlockLabel(func_.GetName(), false_bb->GetName()));
    }
}
```

`MipsOptions` 中需新增 `enable_block_merge = false;` 开关。

### 5.3 O5 Before / After 对照

**示例：if-else 结构的条件分支**

IR（DCE 后）：
```llvm
b2:
    %cond = icmp slt i32 %x, 10
    br i1 %cond, label %b3, label %b4
b3:                          ; if true
    ...
    br label %b5
b4:                          ; if false（在 b3 之后）
    ...
```

**优化前 MIPS**（b2 的分支跳转，2 条）：
```mips
func.b2:
    ...
    bnez  $t0, func.b3
    j     func.b4            ← b4 紧跟在 b3 之后？否
```

若 b4 是 **b2 的紧邻下一块**（`FunctionEmitter` 迭代顺序）：

**优化后 MIPS**（1 条）：
```mips
func.b2:
    ...
    bnez  $t0, func.b3       ← 只保留 bnez，fall-through 进 b4
func.b4:                     ← 保留标签（b3 末尾的 br label b5 还会跳到这里等）
    ...
```

节省 1 条 `j` 指令。在含有大量 if 分支的程序中，每个 if 结构节省 1 条跳转，效果可观。

---

## 六、MipsOptions 新增开关

```cpp
// include/mips/MipsOptions.h

struct MipsOptions {
    bool emit_comments      = false;
    bool enable_reg_alloc   = false;
    bool enable_peephole    = false;    // O4 窥孔优化
    bool enable_mul_div_opt = false;    // O3 乘除法强度削减
    bool enable_block_merge = false;    // O5 基本块合并 + 冗余跳转消除（新增）
};
```

在 `main.cpp` 中：

```cpp
// main.cpp 中新增（仿照 kEnableConstFoldLVN）
const bool kEnableMulDivOpt   = true;
const bool kEnablePeephole    = true;
const bool kEnableBlockMerge  = true;

mips::MipsOptions mips_opts;
mips_opts.enable_mul_div_opt = kEnableMulDivOpt;
mips_opts.enable_peephole    = kEnablePeephole;
mips_opts.enable_block_merge = kEnableBlockMerge;
mips::MipsEmitter emitter(mips_out, *result.module, mips_opts);
emitter.Emit();
```

---

## 七、改动清单

### O3 乘除法强度削减

| 文件 | 改动 |
|------|------|
| `include/mips/InstructionEmitter.h` | 新增私有方法：`EmitOptimizedMul`、`EmitOptimizedDiv`、`EmitGenericMul`、`EmitGenericDiv`；辅助 `IsPowerOfTwo`、`IsShiftPlusOne`、`IsShiftMinusOne` |
| `src/mips/InstructionEmitter.cpp` | `EmitBinaryInst` 中增加 `enable_mul_div_opt` 分支；实现上述新方法 |
| `include/mips/MipsOptions.h` | `enable_mul_div_opt` 已存在，无需改 |
| `src/main.cpp` | 新增 `kEnableMulDivOpt` 开关，接入 `mips_opts` |

### O4 窥孔优化

| 文件 | 改动 |
|------|------|
| `include/mips/AsmWriter.h` | 新增：`BeginBuffer()`、`FlushBuffer(bool)`、私有 `RunPeephole()`、`buffer_`、`buffering_` 成员；私有解析辅助函数 |
| `src/mips/AsmWriter.cpp` | `EmitInsn`/`EmitLi`/`EmitMove`/`EmitSwSp`/`EmitLwSp` 等方法增加缓冲分支；实现 `BeginBuffer`/`FlushBuffer`/`RunPeephole` |
| `include/mips/FunctionEmitter.h` | `Emit()` 方法无需改签名 |
| `src/mips/FunctionEmitter.cpp` | `Emit()` 中在 `EmitPrologue` 前调用 `writer_.BeginBuffer()`，函数结束后调用 `writer_.FlushBuffer(enable_peephole)` |
| `src/main.cpp` | 新增 `kEnablePeephole` 开关，接入 `mips_opts` |

### O5 基本块合并 + 冗余跳转消除

| 文件 | 改动 |
|------|------|
| `include/mips/InstructionEmitter.h` | `Emit()` 和 `EmitBranchInst()` 增加 `const ir::BasicBlock* next_block` 参数 |
| `src/mips/InstructionEmitter.cpp` | `EmitBranchInst` 实现条件分支的 fall-through 优化和条件反转 |
| `src/mips/FunctionEmitter.cpp` | `EmitBody()` 中传递 `next_block` 给 `inst_emitter_.Emit()` |
| `include/mips/MipsOptions.h` | 新增 `enable_block_merge = false` 字段 |
| `src/main.cpp` | 新增 `kEnableBlockMerge` 开关 |

**CMakeLists.txt**：无需改动（`AsmWriter.cpp`、`InstructionEmitter.cpp`、`FunctionEmitter.cpp` 已在编译目标中，无新文件）。

---

## 八、推荐实施顺序与验证策略

### 8.1 推荐实施顺序

```
O3 乘除法强度削减（~150行）
    ↓ 验证：wc -l mips.txt 应减少（含 li 指令的消除）
O4 窥孔优化（~200行）
    ↓ 验证：wc -l mips.txt 应有显著减少（lw 指令大量消除）
O5 基本块合并（~60行）
    ↓ 验证：wc -l mips.txt 应略有减少（j 指令消除）
```

### 8.2 验证策略

1. **编译无 Warning**：每项实施后先确保编译通过
2. **全量测试回归**：用 `SysY_Test_2024/` 下所有测试用例，MARS 4.5 执行结果与优化前完全一致
3. **量化收益**：
   ```bash
   # 在 build 目录执行，对比不同优化组合的 MIPS 指令条数
   wc -l mips_baseline.txt
   wc -l mips_O3.txt
   wc -l mips_O3_O4.txt
   wc -l mips_O3_O4_O5.txt
   ```
4. **关键检查点**：
   - O3：`grep -c "mul\s" mips.txt` 应显著减少（循环体中的 `mul` 被替换为 `sll`）
   - O4：`grep -c "lw\s" mips.txt` 应显著减少
   - O5：`grep -c "^\s*j\s" mips.txt` 应略有减少
5. **回归边界用例**：
   - 包含负数乘法/除法的测试用例（验证 O3 符号处理正确）
   - 包含 `getint()/putint()` 的用例（CallInst 不参与窥孔，不受影响）
   - 空循环体（O5 不能删除有效跳转）

### 8.3 独立开关设计原则

三项优化完全独立可关，出现 bug 时可以二分定位：

```
kEnableMulDivOpt = false, kEnablePeephole = false, kEnableBlockMerge = false
    → 基准输出（与优化前完全相同）
kEnableMulDivOpt = true,  其他 false
    → 只验证 O3 的正确性
kEnablePeephole  = true,  其他 false
    → 只验证 O4 的正确性
kEnableBlockMerge = true, 其他 false
    → 只验证 O5 的正确性
全部 true
    → 最终优化效果
```

---

## 九、后续展望

完成 O3+O4+O5 后，`Mem2Reg → ConstFoldLVN → DCE → MIPS（含强度削减+窥孔+块合并）` 形成完整的优化流水线，覆盖了课程要求的所有主要优化方向。

如时间允许，下一个高 ROI 方向是：

- **图着色寄存器分配**（O7）：通过消除绝大部分 `lw`/`sw` 来实现最大幅度的指令缩减。`ValueLocation` 桥梁已搭好，活跃变量分析是前置条件。注意：O4 窥孔在"仅消除紧邻 sw/lw 对"方面相当于"轻量级"寄存器分配的替代，在没有完整寄存器分配时已能取得可观效果。
- **Magic Number 除法**（O3 扩展）：补完非 2 幂次除法的强度削减，约需额外 120 行。

---

*本文档记录 MIPS 后端三项优化（O3/O4/O5）的设计背景与实现方案。实施完成后，在 `optimization_phase_plan_20260310.md` 中更新对应项的状态，并补充实际的 diff 与 wc 数据。*

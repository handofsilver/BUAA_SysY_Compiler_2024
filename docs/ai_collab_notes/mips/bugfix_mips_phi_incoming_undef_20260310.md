# Bug Fix: MIPS 后端对 phi 的 undef incoming 断言失败

**Date**: 2026-03-10
**Files**: `src/mips/FunctionEmitter.cpp`（`LoadValueToReg`）、`src/mips/StackFrame.cpp`（`GetOffset`）
**症状**: 对会产生“带 undef incoming 的 phi”的 SysY 程序开启 MIPS 生成时，断言失败：

```
Compiler: .../StackFrame.cpp:23: int mips::StackFrame::GetOffset(const ir::Value *) const:
Assertion `it != value_offset_.end() && "Value has no stack slot"' failed.
```

**现象区分**：单纯的 `if (a == 2 || b == 3) { c = 5; } else { c = 6; }`（无循环）在本项目中**不一定**产生 undef；而 **循环内含有短路求值**（如 `for` 里 `if (arr[i]==0 || d=='A')`）时，会稳定出现 undef，并触发上述断言。

---

## 1. 触发情形

### 1.1 为何是“循环 + 短路”才出现 undef

- **只有 if 的短路**：`if (a||b) { ... } else { ... }` 的合并块上，两条边（then/else）通常都会给出一个值（或常量或前驱块里的计算结果），phi 的两条 incoming 都可能被定义，因此不一定出现 undef。
- **循环头 phi**：`for (i=0; i<n; i=i+1) { ... if (x||y) ... }` 中，循环头（for.cond）既有 **entry** 边（第一次进入），又有 **for.step** 边（从循环体回来）。循环体内短路等控制流会产生一些 SSA 值，经合并块（如 if.next）流回 for.step，再回到 for.cond。这些值**只在回边上有定义**，从 **entry** 进入时尚未执行过循环体，对应 phi 的 entry 前驱上就是 **undef**。

因此 undef 出现在 **循环头的 phi** 上，且来自 **entry → for.cond** 这条边。

### 1.2 最小 SysY 样例（会产生 undef）

**循环 + 循环内短路**即可稳定复现，下面为最简写法：

```c
void f(int x, int y) {
    int i;
    for (i = 0; i < 2; i = i + 1) {
        if (x == 0 || y == 1)
            ;
    }
}
```

- `for` 产生循环头（for.cond）、体（for.body）、步长（for.step）、出口（for.after）。
- 循环体内的 `if (x == 0 || y == 1)` 产生短路控制流：or.then / or.rhs / or.merge，再回到 for.step → for.cond。

### 1.3 CFG 示意（Mem2Reg 后，仅画与 undef 相关的部分）

循环头 **for.cond** 有两条前驱：**entry**（首次进入）和 **for.step**（回边）。归纳变量 i 和“短路结果”等都会在 for.cond 上用 phi 合并；前者在 entry 边有常量 0，后者在 entry 边**没有定义**，故为 undef。

```
                    entry
                      │
                      │  (首次进入，短路结果尚未计算)
                      ▼
              ┌───────────────┐
              │  for.cond     │◄──────────┐
              │  %i = phi ... │           │
              │  %sc = phi [undef,entry], [%v,for.step]  ← undef 来自这条边
              └───────┬───────┘           │
                      │                   │
           i < 2?     │         i >= 2?   │
              ▼       │            ▼      │
         for.body     │       for.after   │
              │       │            (exit) │
              │  ... if (x==0||y==1) ...  │
              │  or.then / or.rhs         │
              │  or.merge                 │
              ▼       │                   │
         for.step ────┴───────────────────┘
                      (回边，%v 有定义)
```

- **entry → for.cond**：循环体尚未执行，短路结果对应的 phi 在该边上无定义 → **undef**。
- **for.step → for.cond**：从循环体回来，短路已在 or.merge 等处产生 SSA 值 %v → phi 取 **%v**。

### 1.4 对应的 LLVM IR 片段（Mem2Reg 后）

循环头会出现**从 entry 来的 undef**，例如：

```llvm
for.cond:
  %i  = phi i32 [ 0, %entry ], [ %i.next, %for.step ]
  %sc = phi i32 [ undef, %entry ], [ %v, %for.step ]
  ...
```

- `%i`：归纳变量，entry 给 0，回边给 %i.next，两条边都有定义。
- `%sc`：短路/合并结果，**仅回边有定义**；entry 边为 **undef**。`PhiInst::GetIncomingValue(i)` 在该边上返回 **`nullptr`**（与 [Mem2Reg 的 use-before-def 修复](../mem2reg/bugfix_mem2reg_use_before_def_segv_20260310.md) 一致）。

修复后，此类 IR 可正常生成 MIPS。

---

## 2. 根因分析

### 2.1 Phi lowering 的流程

MIPS 在**前驱块末尾**（分支前）为后继块中的 phi 做“移动插入”：

- 对每条“前驱 → 后继”边，收集该边上每个 phi 的 **(phi, incoming_value)**。
- 对每个 `incoming_value` 调用 `LoadValueToReg(incoming, "$t0")`，再把 `$t0` 存到该 phi 的栈槽。

因此 `incoming` 会传入 **phi 的 GetIncomingValue(i)**，在 undef 边上即为 **`nullptr`**。

### 2.2 LoadValueToReg 修复前的行为

```cpp
void FunctionEmitter::LoadValueToReg(const ir::Value* val, const std::string& reg) {
    if (auto* ci = dynamic_cast<const ir::ConstantInt*>(val)) {
        os_ << kIndent << "li    " << reg << ", " << ci->GetValue() << "\n";
    } else if (auto* gv = ...) { ... }
    } else if (auto* alloca = ...) { ... }
    } else {
        os_ << kIndent << "lw    " << reg << ", " << frame_.GetOffset(val) << "($sp)\n";
    }
}
```

- `val == nullptr` 时，所有 `dynamic_cast` 都不命中，会进入 **else**，对 `val` 调用 `frame_.GetOffset(val)`。
- **StackFrame** 只为“本函数内的 Alloca、产生结果的 Instruction、参数”分配栈槽；**常量**和 **nullptr** 都没有 slot。
- 于是 `value_offset_.find(nullptr)` 得到 `end()`，触发断言：`"Value has no stack slot"`。

### 2.3 小结

| 来源           | 内容                     | 后端期望处理方式     |
|----------------|--------------------------|----------------------|
| Mem2Reg 的 phi | 某条 incoming = nullptr | 不查栈槽，按 undef 生成代码 |

---

## 3. 修复策略

在 **LoadValueToReg** 开头显式处理 `val == nullptr`：不访问栈帧，直接生成“未定义值”的占位指令（与 Mem2Reg 用常量 0 表示 use-before-def 一致），然后返回。

```cpp
void FunctionEmitter::LoadValueToReg(const ir::Value* val, const std::string& reg) {
    // Phi incoming can be nullptr (undef) from Mem2Reg; no stack slot exists.
    if (val == nullptr) {
        os_ << kIndent << "li    " << reg << ", 0\n";
        return;
    }
    // ... 原有 ConstantInt / GlobalVar / AllocaInst / else (lw from slot)
}
```

- **仅改一处**：`FunctionEmitter::LoadValueToReg`。
- **语义**：undef 在 MIPS 上用立即数 0 表示；若将来需要区分“未初始化”与“常量 0”，可再扩展（例如用单独伪指令或约定）。
- **与 Mem2Reg 文档的关系**：Mem2Reg 端保证“未定义路径”上 phi 的 incoming 填 `nullptr` 且不产生非法 IR；MIPS 端必须对所有可能传入 `LoadValueToReg` 的 Value 类型（含 nullptr）给出合法生成，否则就会在 GetOffset 或其它地方断言/崩溃。

---

## 4. 设计启示

- 凡是从 **phi 的 GetIncomingValue(i)** 得来的 `Value*`，后端都必须考虑 **nullptr**（undef），不能假定“一定是某条指令/参数/常量”。
- 栈槽只对“本函数内分配了 slot 的 Value”有意义；常量、全局、undef 都没有 slot，在“把 Value 装入寄存器”的通用路径里要先做类型/空指针判断，再决定是立即数、加载地址还是 `lw` 栈槽。

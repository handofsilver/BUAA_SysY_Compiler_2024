# Mem2Reg 优化 Pass 设计规划

**Date**: 2026-03-04
**Phase**: IR Optimization (before MIPS backend)
**Goal**: 将 alloca/store/load 模式提升为纯 SSA φ 节点

---

## 一、为什么需要 Mem2Reg？

当前的 IR 生成策略是**伪 SSA**：
- 每个局部变量对应一个 `alloca`（栈槽）
- 读变量 → `load`，写变量 → `store`
- 条件分支汇合处用的是 `alloca + store + load` 而非 `phi`

```
; 当前生成的 IR（伪 SSA 风格）
%a = alloca i32
store i32 0, ptr %a
; ... if/else 两支各 store 不同值 ...
%val = load i32, ptr %a   ; 汇合处读值
```

真正的 SSA（Static Single Assignment）形式应该是：
```
; Mem2Reg 变换后
%val = phi i32 [ 0, %if.then ], [ 1, %if.else ]
```

Mem2Reg 的意义：
1. 消除大量冗余的内存操作，生成更高效的 MIPS 代码
2. 使 IR 进入"完全 SSA 形式"，为后续寄存器分配铺路
3. 是工业界（LLVM、GCC）的标准优化 pass

---

## 二、整体实现步骤（宏观路线图）

Mem2Reg 的完整实现需要以下五步，**严格有序**，每步都依赖前一步：

```
Step 1: CFG 构建
  └─ 给 BasicBlock 添加前驱/后继信息
  └─ 函数: BuildCFG(Function*)

Step 2: 支配树（Dominator Tree）计算
  └─ 算法: 经典迭代数据流 / Cooper 简单算法
  └─ 结果: 每个块的"支配者"集合 / idom（直接支配者）

Step 3: 支配边界（Dominance Frontier）计算
  └─ 基于支配树的 DF 计算
  └─ 结果: DF[B] = B 支配边界的块集合

Step 4: PhiInst 类 & 插入 φ 节点
  └─ 新增 PhiNode 指令类
  └─ 基于 DF，在需要的位置插入空 φ 节点

Step 5: SSA 重命名（Renaming）
  └─ DFS 遍历支配树
  └─ 将所有 load/store/alloca 替换为 SSA Value
  └─ 填充 φ 节点的操作数
```

**完成后**：函数内所有可提升（promotable）的 `alloca` 被消除，IR 进入完全 SSA 形式。

---

## 三、架构设计：Pass 基础设施

### 3.1 Pass 接口设计

```cpp
// include/pass/Pass.h
class FunctionPass {
public:
    virtual ~FunctionPass() = default;
    virtual bool Run(ir::Function& func) = 0;  // 返回 true 表示 IR 被修改
    virtual std::string GetName() const = 0;
};
```

所有优化 Pass 都继承 `FunctionPass`。Mem2Reg 是一个 `FunctionPass`。

### 3.2 Pass Manager（简化版）

```cpp
// include/pass/PassManager.h
class PassManager {
    std::vector<std::unique_ptr<FunctionPass>> passes_;
public:
    void AddPass(std::unique_ptr<FunctionPass> pass);
    void RunAll(ir::Module& module);  // 对每个函数跑所有 pass
};
```

### 3.3 Mem2Reg Pass 内部结构

```cpp
// include/pass/Mem2Reg.h
class Mem2RegPass : public FunctionPass {
    // 内部依赖的分析结果（每次 Run 重新计算）
    struct CFGInfo {
        std::unordered_map<ir::BasicBlock*, std::vector<ir::BasicBlock*>> succs;
        std::unordered_map<ir::BasicBlock*, std::vector<ir::BasicBlock*>> preds;
    };

    struct DomTreeInfo {
        std::unordered_map<ir::BasicBlock*, ir::BasicBlock*> idom;      // 直接支配者
        std::unordered_map<ir::BasicBlock*, std::vector<ir::BasicBlock*>> children; // 支配树子节点
    };

    struct DFInfo {
        std::unordered_map<ir::BasicBlock*, std::set<ir::BasicBlock*>> df; // 支配边界
    };

public:
    bool Run(ir::Function& func) override;
    std::string GetName() const override { return "mem2reg"; }

private:
    void BuildCFG(ir::Function& func, CFGInfo& cfg);
    void BuildDomTree(ir::Function& func, const CFGInfo& cfg, DomTreeInfo& dom);
    void ComputeDF(ir::Function& func, const DomTreeInfo& dom,
                   const CFGInfo& cfg, DFInfo& df);
    void InsertPhiNodes(ir::Function& func, const DFInfo& df,
                        const std::vector<ir::AllocaInst*>& promotable);
    void RenamePass(ir::BasicBlock* bb, ...);

    std::vector<ir::AllocaInst*> CollectPromotableAllocas(ir::Function& func);
    bool IsPromotable(ir::AllocaInst* alloca);
};
```

### 3.4 文件结构规划

```
include/pass/
  Pass.h           ← FunctionPass 基类
  PassManager.h    ← PassManager
  Mem2Reg.h        ← Mem2RegPass 声明
  CFGBuilder.h     ← CFG 构建工具（可复用）
  DomTree.h        ← 支配树计算（可复用）

src/pass/
  PassManager.cpp
  Mem2Reg.cpp      ← 主逻辑（可拆分多个 cpp）
  CFGBuilder.cpp
  DomTree.cpp
```

### 3.5 PhiInst 新指令

```cpp
// 在 include/ir/Instruction.h 中新增
class PhiInst : public Instruction {
    // operands: [val0, label0, val1, label1, ...]
    // 每对 (value, predecessor_block) 称为一个 incoming
public:
    void AddIncoming(ir::Value* val, ir::BasicBlock* pred);
    std::pair<ir::Value*, ir::BasicBlock*> GetIncoming(int i) const;
    int GetNumIncoming() const;
    void Print(std::ostream& os, const IRPrintContext* ctx) const override;
    // 打印: %r = phi i32 [ %a, %entry ], [ %b, %if.then ]
};
```

---

## 四、关键设计决策

### 决策1：CFG 是计算缓存还是实时查询？

**选择**：在每次 Pass 运行时**计算并缓存**到局部结构（`CFGInfo`），而非永久写入 `BasicBlock`。

**理由**：
- Mem2Reg 会修改 IR（删除指令、插入 phi），修改后 CFG 就失效了
- 避免"修改 IR 后忘记更新 CFG"的 bug
- 每次 Pass 开始时重算，成本可接受（函数内块数不多）

### 决策2：支配树算法选择

**选择**：Keith Cooper 的**简单迭代算法**（非 Lengauer-Tarjan）。

**理由**：
- 简单，容易实现正确
- 对于 SysY 函数规模完全够用
- Lengauer-Tarjan 虽然 O(n·α(n))，但实现复杂度高，不值当

### 决策3：Mem2Reg 只处理哪些 alloca？

**可提升（promotable）的 alloca**：
- `alloca` 的结果只被 `store`（写）和 `load`（读）使用
- 没有被当作指针传给函数（`call` 的参数）
- 没有通过 GEP 计算地址（即不是数组）
- 本质上：`alloca i32`（标量），且所有 use 都是直接 load/store

**不可提升**：数组的 alloca、地址被取走的 alloca。

---

## 四（续）支配树：概念与算法

### 4.1 支配关系的直观含义

- **A 支配 B**（A dominates B）：从函数**入口**出发，到达 B 的**任意一条**路径都必须经过 A。
- 推论：入口块支配所有可达块；每个块支配自己。
- **严格支配**：A 严格支配 B ⇔ A 支配 B 且 A ≠ B。
- **直接支配者 idom(B)**：B 的“最近”的严格支配者——即严格支配 B 的块中，不被其他严格支配者支配的那一个。若 B 有多个前驱，idom(B) 就是这些前驱的“交汇点”。

### 4.2 支配树（Dominator Tree）

- 以基本块为节点，边为 `(idom(B), B)`，形成一棵**树**（入口为根）。
- 树上的祖先关系 = 支配关系：A 是 B 的祖先 ⇔ A 支配 B。
- Mem2Reg 用支配树做两件事：
  1. **支配边界 DF** 的计算依赖支配树；
  2. **SSA 重命名**时按支配树 DFS 顺序遍历，保证定义先于使用。

### 4.3 Cooper 迭代算法（求 dom 集合）

**数据流方程**：

- `dom(entry) = { entry }`
- 对其它块 B：`dom(B) = { B } ∪ ( ∩_{P ∈ pred(B)} dom(P) )`

即：B 的支配者 = 自己 ∪ 所有前驱的支配者集合的**交集**。迭代直到不动点。

**从 dom 得到 idom**：

- `idom(B)` = dom(B) 中**严格支配 B** 且**不被 dom(B) 中其它块支配**的那一块。
- 等价说法：idom(B) 是 dom(B) \ {B} 中，在支配树意义上**最深的**（即离 B 最近的那一个）。实现时可在收敛后对每个 B，在 dom(B)\{B} 里找满足 `dom(B) ⊇ dom(idom_candidate)` 且“最小”的块。

更常见的实现是：在迭代过程中维护的是 **idom** 的近似，然后用 **dom** 集合来校验。我们采用另一种等价实现：**先迭代出完整 dom 集合**，再对每个 B 用“取前驱的最近共同支配者”的方式算 idom（见下文代码注释）。

### 4.4 接口约定

- **入口块**：约定为 `func.GetBlocks()[0]`（与 IRGenVisitor 生成顺序一致）。
- **DomTreeInfo** 提供：
  - `GetIdom(bb)`：返回 bb 的直接支配者，入口块返回 `nullptr`。
  - `GetChildren(bb)`：支配树中 bb 的子节点列表，便于 DFS。
- 构建函数：`DomTreeInfo BuildDomTree(const CFGInfo& cfg, ir::BasicBlock* entry)`。

---

## 五、实现进度追踪

| 步骤 | 状态 | 关键文件 |
|------|------|---------|
| Pass 基础设施（`FunctionPass`, `PassManager`） | 待实现 | `include/pass/Pass.h` |
| PhiInst 指令类 | 待实现 | `include/ir/Instruction.h` |
| CFG 构建 | 待实现 | `src/pass/CFGBuilder.cpp` |
| 支配树计算 | 待实现 | `src/pass/DomTree.cpp` |
| 支配边界计算 | 待实现 | `src/pass/DomTree.cpp` |
| φ 节点插入 | 待实现 | `src/pass/Mem2Reg.cpp` |
| SSA 重命名 | 待实现 | `src/pass/Mem2Reg.cpp` |
| main.cpp 中接入 Pass | 待实现 | `src/main.cpp` |

---

## 六、验证方法

每个步骤完成后，验证方式如下：

1. **CFG 构建后**：打印每个块的前驱/后继，人工验证
2. **支配树后**：对简单函数（直线代码、单层 if）手算并比对
3. **φ 节点插入后**：`.ll` 文件中出现 `phi` 指令，但操作数为 `undef`
4. **重命名后**：用 `lli` 运行 `.ll` 文件，结果与未优化版本一致

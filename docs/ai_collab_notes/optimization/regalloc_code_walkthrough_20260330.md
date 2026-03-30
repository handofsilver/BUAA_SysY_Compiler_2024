# 图染色寄存器分配：代码级完整演算

**日期**：2026-03-30
**前置阅读**：[原理详解](regalloc_graph_coloring_theory_20260328.md)、[计算实例](regalloc_graph_coloring_examples_20260328.md)
**目的**：你已经手算过算法，但代码看起来像另一个世界。本文用同一个具体例子，**把每一步算法推导和对应的 C++ 数据结构状态并排展示**，让你在脑子里能同时运行"纸上算法"和"代码执行"两条线。

---

## 0. 代码导读：整体调用链

```
RegAllocator::Run()          ← RegAlloc.cpp:728
  ├─ BuildLiveness()         ← LivenessAnalysis.cpp:617
  │    ├─ PartitionBlocks()         # buffer → vector<MipsBlock>
  │    ├─ BuildCFG()                # 填 succs/preds
  │    ├─ ComputeBlockDefUse()      # block_def, block_use
  │    ├─ ComputeBlockLiveness()    # live_in, live_out (fixpoint)
  │    ├─ ComputeInstructionLiveness()   # inst_live_out[i]
  │    └─ BuildInterferenceGraph()  # InterferenceGraph
  │
  ├─ ColorWithCoalesce()     ← RegAlloc.cpp:87
  │    ├─ 初始化
  │    ├─ main loop: Simplify → Coalesce → Freeze → Spill
  │    └─ Select
  │
  ├─ RewriteBuffer()         ← RegAlloc.cpp:624
  │    └─ RewriteInstruction() per inst
  │
  └─ ApplyCalleeSaved()      ← RegAlloc.cpp:674
```

贯穿全流程的核心数据类型：

| 数据结构 | 定义位置 | 作用 |
|---------|---------|------|
| `vector<MipsInst> buffer` | `MipsInst.h` | 指令流（AsmWriter 产出）|
| `RegIdMap reg_ids` | `LivenessAnalysis.h` | 寄存器名字 ↔ 整数 ID |
| `vector<MipsBlock> blocks` | `LivenessAnalysis.h` | CFG 上的基本块 |
| `InterferenceGraph ig` | `LivenessAnalysis.h` | 干涉图：`adj_[]`, `degree_[]`, `moves_[]` |
| `vector<int> color` | 局部于 `ColorWithCoalesce` | 节点 ID → 调色板索引 |
| `vector<int> alias` | 局部于 `ColorWithCoalesce` | union-find，Coalesce 合并后的代表节点 |
| `ColoringResult cr` | `RegAlloc.h` | 最终输出：`color_by_node[]`, `actual_spills` |

---

## 1. 演示用例

我们设计一个包含 5 个虚拟寄存器和 **1 条 MOVE 指令**的单块函数。这个例子足够小以便手算，同时能触发 Coalesce 消除 MOVE。

### 1.1 MipsInst buffer（函数体部分）

`FunctionEmitter` 产生如下 buffer（省略 prologue/epilogue，下文专门讲）：

```
[0] LABEL  "func_foo"
[1] LW     dst=$vr0, src1=$sp, imm=0        # $vr0 = mem[$sp+0]（参数a）
[2] LW     dst=$vr1, src1=$sp, imm=4        # $vr1 = mem[$sp+4]（参数b）
[3] ADDU   dst=$vr2, src1=$vr0, src2=$vr1   # $vr2 = $vr0 + $vr1
[4] MOVE   dst=$vr3, src1=$vr1              # $vr3 = $vr1  ← 关键 MOVE
[5] ADDU   dst=$vr4, src1=$vr2, src2=$vr3   # $vr4 = $vr2 + $vr3
[6] SW     dst=$vr4, src1=$sp, imm=8        # mem[$sp+8] = $vr4（输出）
[7] LW     dst=$ra, src1=$sp, imm=...
[8] JR     src1=$ra
```

在 C++ 中，每条指令是一个 `MipsInst` 结构体（`include/mips/MipsInst.h`）：

```cpp
struct MipsInst {
    MipsOpcode  op   = ...;
    std::string dst  = "";   // 目标寄存器（或 SW 的 value 寄存器）
    std::string src1 = "";   // 第一源寄存器（或 LW/SW 的 base）
    std::string src2 = "";   // 第二源寄存器（R-type）
    int64_t     imm  = 0;    // 立即数 / 栈偏移
    std::string label = "";  // 跳转目标 / 标签名
};
```

**为什么用结构体而不是字符串？** 活跃性分析需要精确知道每条指令的 def 和 use。字符串需要解析，脆弱且繁琐；结构体可以通过 `GetDefs(inst)` / `GetUses(inst)` 直接查表。

---

## 2. Build 阶段：从 buffer 到 InterferenceGraph

`BuildLiveness()` 是 Build 阶段的总调度，分六步。我们逐步跟踪。

### 2.1 PartitionBlocks — buffer → vector\<MipsBlock\>

```cpp
// LivenessAnalysis.cpp:255
static std::vector<MipsBlock> PartitionBlocks(const std::vector<MipsInst>& buffer) {
    for (int i = 0; i < buffer.size(); ++i) {
        if (buffer[i].op == MipsOpcode::LABEL) {
            if (!blocks.empty()) blocks.back().end = i;
            MipsBlock blk;
            blk.start = i;   // 含 LABEL 本身
            blk.end   = buffer.size(); // 暂时
            blk.label = buffer[i].label;
            blocks.push_back(blk);
        }
    }
}
```

对我们的例子（单块函数），结果：

```
blocks[0] = { start=0, end=9, label="func_foo", succs=[], preds=[] }
```

`MipsBlock::start/end` 是 buffer 的下标闭区间 [start, end)。

### 2.2 BuildCFG — 填 succs/preds

`BuildCFG` 找每个块的最后一条（和倒数第二条）真实指令，识别 JR/J/BNEZ/BEQZ，连边。

对我们的例子：最后真实指令是 `JR $ra`，`BuildCFG` 识别为函数返回，**不添加后继**：

```
blocks[0].succs = []   // JR → 无后继
```

### 2.3 ComputeBlockDefUse — 计算 block_def, block_use

```cpp
// LivenessAnalysis.cpp:377
// 从前向后扫描，先处理 use（若未被 def 则加入 block_use），后处理 def
for (int i = blk.start; i < blk.end; ++i) {
    for (const auto& u : GetUses(buffer[i])) {
        int uid = reg_ids.GetOrCreate(u);    // ← 这里给寄存器分配整数 ID
        if (block_def[bi].count(uid) == 0)
            block_use[bi].insert(uid);
    }
    for (const auto& d : GetDefs(buffer[i])) {
        int did = reg_ids.GetOrCreate(d);
        block_def[bi].insert(did);
    }
}
```

**RegIdMap 的构建就在这里**——按遇到顺序分配 ID：

| 第一次遇到 | 指令 | 分配 ID |
|-----------|------|---------|
| `$sp` | [1] LW use | 0 |
| `$vr0` | [1] LW def | 1 |
| `$vr1` | [2] LW def | 2 |
| `$vr2` | [3] ADDU def | 3 |
| `$vr3` | [4] MOVE def | 4 |
| `$vr4` | [5] ADDU def | 5 |
| `$ra` | [7] LW def | 6 |

（`GetUses`/`GetDefs` 的逻辑在 `LivenessAnalysis.cpp:40–188`）

`RegIdMap` 内部维护两个结构（`LivenessAnalysis.h:38`）：

```cpp
unordered_map<string, int> name_to_id_;   // "$vr0" -> 1
vector<string>             id_to_name_;   // id_to_name_[1] = "$vr0"
```

计算完毕后，`block_use[0]` 和 `block_def[0]`（只有一个块）：

- `block_use[0]` = {\$sp=0}（\$sp 在函数头就被 LW 用到，且从未在此函数中被定义）
- `block_def[0]` = {\$vr0=1, \$vr1=2, \$vr2=3, \$vr3=4, \$vr4=5, \$ra=6}

> **注意**：`block_use` 是"向上暴露的 use"——只有**在任何 def 之前**就被 use 的寄存器才算。\$vr0 在 [1] 被 def，后来在 [3] 被 use，但 def 先于 use，所以 \$vr0 **不在** block_use 里。

### 2.4 ComputeBlockLiveness — 迭代不动点

```
live_in[B]  = use[B]  ∪  (live_out[B] - def[B])
live_out[B] = ∪ live_in[S]   for S in succs(B)
```

对我们的单块例子，succs 为空，所以：

```
live_out[0] = {}      // 没有后继
live_in[0]  = {$sp=0}  // use[0] ∪ ({} - def[0]) = {$sp}
```

一轮迭代即收敛（无循环）。

代码（`LivenessAnalysis.cpp:446`）用**逆后序**迭代（对反向数据流问题等价于后序），`changed` 标记驱动不动点：

```cpp
bool changed = true;
while (changed) {
    changed = false;
    for (int idx = rpo.size()-1; idx >= 0; --idx) {   // 反向 RPO
        int bi = rpo[idx];
        // 重新计算 live_out 和 live_in
        if (new_in != live_in[bi]) { changed = true; live_in[bi] = new_in; }
        live_out[bi] = move(new_out);
    }
}
```

### 2.5 ComputeInstructionLiveness — 指令级活跃集合

这是最重要的一步。代码从块末尾**向前**扫描，维护滚动的 `live` 集合：

```cpp
// LivenessAnalysis.cpp:496
std::unordered_set<int> live = block_live_out[bi];   // 从块出口开始

for (int i = blk.end-1; i >= blk.start; --i) {
    if (!buffer[i].IsInsn()) continue;

    inst_live_out[i] = live;    // ← 记录"执行完 i 之后"哪些寄存器活跃

    // 更新 live：移除 def，加入 use → 得到 live_in(i)
    for (const auto& d : GetDefs(buffer[i]))
        live.erase(reg_ids.GetOrCreate(d));
    for (const auto& u : GetUses(buffer[i]))
        live.insert(reg_ids.GetOrCreate(u));
}
```

**关键理解**：`inst_live_out[i]` 是**执行完指令 i 之后**（即"i 的出口处"）活跃的寄存器集合。这正是构建干涉图所需要的：当指令 i 定义 d 时，d 与 `inst_live_out[i]` 中所有寄存器干涉。

逐指令推导（从后往前，以 ID 表示，忽略 [0] LABEL、[7] LW \$ra、[8] JR）：

| 步骤 | 指令 | `live`（=本指令 live_out） | def | use | 更新后 live（=本指令 live_in） |
|-----|------|--------------------------|-----|-----|-------------------------------|
| 初始 | — | {} (block_live_out) | — | — | — |
| [8] JR \$ra | JR src1=\$ra | {} | — | {\$ra=6} | **{6}** |
| [7] LW \$ra | LW dst=\$ra | {6} | {6} | {\$sp=0} | **{0}** |
| [6] SW \$vr4 | SW dst=\$vr4, src1=\$sp | {0} | — | {5,\$sp=0} | **{5, 0}** |
| [5] ADDU \$vr4 | ADDU dst=\$vr4 | {5, 0} | {5} | {3, 4} | **{3, 4, 0}** |
| [4] MOVE \$vr3 | MOVE dst=\$vr3 | {3, 4, 0} | {4} | {2} | **{3, 2, 0}** |
| [3] ADDU \$vr2 | ADDU dst=\$vr2 | {3, 2, 0} | {3} | {1, 2} | **{2, 1, 0}** |
| [2] LW \$vr1 | LW dst=\$vr1 | {2, 1, 0} | {2} | {0} | **{1, 0}** |
| [1] LW \$vr0 | LW dst=\$vr0 | {1, 0} | {1} | {0} | **{0}** |

结果 `inst_live_out` 数组（存的是 ID 集合）：

```
inst_live_out[1] = {1, 0}      # {$vr0, $sp}  ($vr0 定义后立即活跃)
inst_live_out[2] = {2, 1, 0}   # {$vr1, $vr0, $sp}
inst_live_out[3] = {3, 2, 0}   # {$vr2, $vr1, $sp}  ($vr0 在 [3] 被使用后死亡)
inst_live_out[4] = {3, 4, 0}   # {$vr2, $vr3, $sp}
inst_live_out[5] = {5, 0}      # {$vr4, $sp}
inst_live_out[6] = {0}         # {$sp}
```

活跃区间可视化（ID 1-5 即 \$vr0-\$vr4）：

```
指令:    [1]   [2]   [3]   [4]   [5]   [6]
$vr0:    |─────────────|               (在[3]作为 use 后死亡)
$vr1:          |─────────────|         (在[4]作为 MOVE src 后死亡)
$vr2:                |─────────────|   (在[5]作为 use 后死亡)
$vr3:                      |─────|     (在[5]作为 use 后死亡)
$vr4:                            |─────|
```

注意：\$vr1（id=2）和 \$vr3（id=4）的活跃区间**相邻**——\$vr1 在 [4] 的 MOVE 完成后死亡，\$vr3 接着活跃。这意味着它们可能可以共享同一个物理寄存器。

### 2.6 BuildInterferenceGraph — 构建干涉图

```cpp
// LivenessAnalysis.cpp:570
for (int i = 0; i < buffer.size(); ++i) {
    auto defs = GetDefs(buffer[i]);
    bool is_move = (buffer[i].op == MipsOpcode::MOVE);

    if (is_move && !buffer[i].dst.empty() && !buffer[i].src1.empty()) {
        ig.AddMove(dst_id, src_id);   // 记录 move pair，供 Coalesce 使用
    }

    for (const auto& d : defs) {
        int d_id = reg_ids.GetOrCreate(d);
        for (int r : inst_live_out[i]) {
            if (r == d_id) continue;
            // MOVE 特殊规则：src 和 dst 之间不加干涉边
            if (is_move && r == reg_ids.GetOrCreate(buffer[i].src1)) continue;
            ig.AddEdge(d_id, r);
        }
    }
}
```

逐指令推导干涉边（只看 \$vr0-\$vr4，\$sp 的边也会加入，下文说明其影响）：

| 指令 | def | inst_live_out 中的其他寄存器 | 新增干涉边 | 备注 |
|------|-----|---------------------------|-----------|------|
| [1] LW \$vr0 | 1 | {0} | **1—0** (\$vr0—\$sp) | |
| [2] LW \$vr1 | 2 | {1, 0} | **2—1, 2—0** | |
| [3] ADDU \$vr2 | 3 | {2, 0} | **3—2, 3—0** | \$vr0(id=1) 不在 live_out[3]！ |
| [4] MOVE \$vr3 | 4 | {3, 0} | **4—3, 4—0** | ⚠ **4—2 不加边**（MOVE 特殊规则：\$vr1=id2 是 src） |
| [5] ADDU \$vr4 | 5 | {0} | **5—0** | |
| [6] SW | — | — | 无 | SW 无 def |

**`ig.AddMove(4, 2)`**：记录 (\$vr3=4, \$vr1=2) 为 move pair。

`InterferenceGraph` 内部（`LivenessAnalysis.h:87`）：

```cpp
class InterferenceGraph {
    int num_nodes_;
    vector<unordered_set<int>> adj_;     // adj_[u] = 邻居集合
    vector<int>                degree_;  // degree_[u] = adj_[u].size()
    vector<pair<int,int>>      moves_;   // move pairs
    unordered_set<int>         move_related_;
};
```

**最终干涉图状态**（只画 \$vr0-\$vr4 间的边，\$sp 的边用注释标注）：

```
$vr0(1) ─── $vr1(2)
            │
$vr2(3) ─── $vr1(2)      注意：$vr2—$vr1 有边
    │
$vr3(4) ─── $vr2(3)      注意：$vr3—$vr2 有边，但 $vr3—$vr1 无边！

            ($vr4 只与 $sp 有边，与其他 $vrX 无干涉)

+ 每个 $vrX 都与 $sp(0) 有干涉边（来自 LW/SW/ADDIU 指令）
move_related_ = {2, 4}    ($vr1 和 $vr3)
moves_        = [(4, 2)]  ($vr3 = move $vr1)
```

**初始度数**（包含 \$sp 边）：

| 节点 | 名字 | 邻居（ID） | degree |
|-----|------|-----------|--------|
| 0 | \$sp | {1,2,3,4,5,...} | ≥5 |
| 1 | \$vr0 | {0, 2} | 2 |
| 2 | \$vr1 | {0, 1, 3} | 3 |
| 3 | \$vr2 | {0, 2, 4} | 3 |
| 4 | \$vr3 | {0, 3} | 2 |
| 5 | \$vr4 | {0} | 1 |

---

## 3. ColorWithCoalesce — 核心着色算法

`ColorWithCoalesce(ig, reg_ids)`（`RegAlloc.cpp:87`）是一个带 union-find 的 worklist 算法。理解它的关键是认清"**工作图**"和"**原始图**"的区别：

- `ig`（原始图）：只读，记录初始的完整干涉关系。
- `adj_work`（工作图）：可变，Coalesce 合并时会把邻居集合合并到代表节点。

### 3.1 初始化

```cpp
// RegAlloc.cpp:93
const int kN = reg_ids.Size();   // = 7 (含 $sp, $ra 等)
const int kK = ColoringResult::kNumPaletteColors;  // = 18 (实际项目)
// 本例为了便于演算，假设 kK = 3

vector<int>  color(kN, -1);       // 着色结果，-1 = 未着色
vector<bool> precolored(kN, false); // 物理 $t/$s 寄存器标为 precolored
```

**`is_cand(u)` 谓词**（RegAlloc.cpp:105）：判断节点是否参与图着色

```cpp
auto is_cand = [&](int u) -> bool {
    return !precolored[u] && RegIdMap::IsAllocatable(reg_ids.GetName(u));
};
```

`IsAllocatable(name)` 返回 true 当且仅当 name 是 `$vr*`（虚拟寄存器）或 `$t0-$t9`/`$s0-$s7`（调色板中的物理寄存器）。

在我们的例子中：
- `$sp(0)`: `IsAllocatable=false` → **not candidate**
- `$vr0(1)–$vr4(5)`: `IsVirtual=true` → **candidates** ✓
- `$ra(6)`: `IsAllocatable=false` → **not candidate**

工作图和有效度数的初始化：

```cpp
// adj_work[u] 初始复制自 ig.Neighbors(u)
// eff_degree[u] 初始 = ig.Degree(u)
vector<unordered_set<int>> adj_work(kN);
vector<int> eff_degree(kN, 0);
for (int u = 0; u < kN; ++u) {
    adj_work[u] = ig.Neighbors(u);
    eff_degree[u] = ig.Degree(u);
}
```

**初始 eff_degree**（以我们的例子）：

```
eff_degree = [-1, 2, 3, 3, 2, 1, -1]
              $sp $vr0 $vr1 $vr2 $vr3 $vr4 $ra
              (索引:0   1    2    3    4    5   6)
```

alias（union-find）和状态标志的初始化：

```cpp
vector<int>  alias(kN);
iota(alias.begin(), alias.end(), 0);  // alias[i] = i（每个节点是自己的代表）
vector<bool> on_stack(kN, false);
vector<bool> coalesced_flag(kN, false);
stack<int>   sel_stack;

list<pair<int,int>> pending_moves(ig.GetMoves().begin(), ig.GetMoves().end());
// pending_moves = [(4, 2)]  ($vr3=4, $vr1=2)
```

初始状态汇总：

```
alias          = [0, 1, 2, 3, 4, 5, 6]   （恒等映射）
on_stack       = [F, F, F, F, F, F, F]
coalesced_flag = [F, F, F, F, F, F, F]
pending_moves  = [(4, 2)]
sel_stack      = (empty)
```

---

### 3.2 主循环第 1 轮：Simplify \$vr4

**进入主循环**（RegAlloc.cpp:245）。循环首先检查是否还有活跃候选节点。有，继续。

**Phase 1: Simplify**（RegAlloc.cpp:261）

计算 `move_related_set()`（RegAlloc.cpp:159）：遍历 `pending_moves`，alias 归一化后收集仍活跃的候选节点：

```cpp
auto move_related_set = [&]() -> unordered_set<int> {
    unordered_set<int> mr;
    for (const auto& mv : pending_moves) {
        for (int raw : {mv.first, mv.second}) {
            int a = get_alias(raw);
            if (is_cand(a) && !on_stack[a] && !coalesced_flag[a])
                mr.insert(a);
        }
    }
    return mr;
};
```

当前 pending_moves = [(4,2)]，move_related = **{2, 4}** = {\$vr1, \$vr3}。

扫描候选节点，找非 move_related、degree < K=3 的：

| 节点 | is_cand | on_stack | coalesced | in move_related | eff_degree < 3 |
|:---:|:-------:|:--------:|:---------:|:--------------:|:--------------:|
| 1 (`$vr0`) | ✓ | F | F | **否** | 2 < 3 ✓ → **PUSH！** |

```cpp
sel_stack.push(1);          // 压栈 $vr0
on_stack[1] = true;
// 对 $vr0 的活跃邻居调用 decrement_degree
for (int t : active_adj(1)) {
    decrement_degree(t);
}
```

`active_adj(1)`（RegAlloc.cpp:144）：遍历 `adj_work[1] = {0, 2}`，过滤 on_stack和 coalesced 的节点，alias 归一化：

- `t_raw=0: kT=alias[0]=0, !on_stack[0], !coalesced[0]` → **include**
- `t_raw=2: kT=alias[2]=2, !on_stack[2], !coalesced[2]` → **include**

`active_adj(1) = {0, 2}`

`decrement_degree(0 = $sp)`：`is_cand(0) = false` → **跳过**（\$sp 不是候选节点，不维护其 eff_degree）。

`decrement_degree(2 = $vr1)`：`is_cand(2)=true, !on_stack[2], !coalesced[2]` → `eff_degree[2]--` = **2**。

**状态更新**：

```
on_stack       = [F, T, F, F, F, F, F]   ($vr0 入栈)
eff_degree     = [-, 2, 2, 3, 2, 1, -]   ($vr1 从3降到2)
sel_stack      = [1]   (bottom→top: $vr0)
```

`simplified=true`，执行 `continue` → **重启主循环**。

---

### 3.3 主循环第 2 轮：Simplify \$vr4

move_related = {2, 4}（unchanged，pending_moves 未变）。

扫描候选节点：
- 1: on_stack → 跳过
- 2: move_related → 跳过
- 3: eff_degree=3，NOT < 3 → 跳过
- 4: move_related → 跳过
- **5 (\$vr4)**: not move_related, eff_degree=1 < 3 → **PUSH！**

```
sel_stack.push(5)
on_stack[5] = true
active_adj(5): adj_work[5] = {0}
  → decrement_degree(0): is_cand($sp)=false → skip
```

**状态更新**：

```
on_stack   = [F, T, F, F, F, T, F]   ($vr4 入栈)
sel_stack  = [1, 5]   (bottom→top)
```

`continue` → 重启。

---

### 3.4 主循环第 3 轮：Coalesce

**Phase 1 Simplify 失败**：

- 3 (`$vr2`): eff_degree=3，not < 3 → 跳过
- 2, 4: move_related → 跳过
- 没有可以 Simplify 的节点，`simplified=false`。

**Phase 2: Coalesce**（RegAlloc.cpp:287）

先清理 pending_moves（移除已失效的 move）：

```cpp
for (auto& [first, second] : pending_moves) {
    int kX = get_alias(first);   // alias[4] = 4 → kX=4
    int kY = get_alias(second);  // alias[2] = 2 → kY=2
    // 检查：kX==kY? 都 gone? 都 precolored? 有干涉边?
    // adj_work[4].count(2) = 0  → 无干涉边
    // 两者都活跃，都是候选 → 保留
}
```

pending_moves 保持 [(4, 2)]。

尝试 George 合并（RegAlloc.cpp:316）：

```cpp
int u = get_alias(4) = 4;   // $vr3
int v = get_alias(2) = 2;   // $vr1
// 都不是 precolored，所以不交换
// 尝试 george(u=4, v=2)
```

`george(4, 2)`（RegAlloc.cpp:194）：检查 v=2 的所有活跃邻居是否都满足 `george_ok(t, u=4)`:

```cpp
auto george_ok = [&](int t, int u) -> bool {
    return eff_degree[t] < kK             // 低度数？
        || precolored[t]                  // 已预着色？
        || adj_work[u].count(t) > 0;     // t 已经与 u 干涉？
};
```

`active_adj(2)`（v=2 的活跃邻居）：adj_work[2] = {0, 1, 3}，过滤：
- t_raw=0: kT=0, !on_stack, !coalesced → include
- t_raw=1: kT=1, **on_stack[1]=true** → **exclude**（\$vr0 已在栈上）
- t_raw=3: kT=3, !on_stack, !coalesced → include

active_adj(2) = **{0, 3}**

对每个 t 检查 george_ok(t, u=4)：

| t | 名字 | eff_degree[t] < K=3? | precolored[t]? | adj_work[4].count(t)>0? | george_ok? |
|--|-----|---------------------|----------------|------------------------|-----------|
| 0 | \$sp | 5 → NO | false | **YES**（\$vr3—\$sp 有边）| ✓ |
| 3 | \$vr2 | 3 → NO | false | **YES**（\$vr3—\$vr2 有边）| ✓ |

所有邻居满足 george_ok → **george(4, 2) = true！** 可以合并。

调用 `combine(u=4, v=2)`（RegAlloc.cpp:208）——\$vr1(2) 并入 \$vr3(4)：

```cpp
coalesced_flag[2] = true;
alias[2] = 4;               // $vr1 的代表节点改为 $vr3

// 遍历 v=2 的邻居，把它们"继承"给 u=4
for (int t_raw : adj_work[2]) {    // adj_work[2] = {0, 1, 3}
    int kT = get_alias(t_raw);
    if (kT == 4 || kT == 2) continue;

    bool is_new_edge = adj_work[4].insert(kT).second;
    if (is_new_edge) {
        adj_work[kT].insert(4);
        // 如果两端都是活跃非栈节点，则 eff_degree 各+1
        // 如果已有边（非 new），则不增加
    }
    // v 消失，kT 少了一个活跃邻居
    decrement_degree(kT);
}
```

逐一处理 adj_work[2] = {0, 1, 3} 中的邻居：

**t_raw=0（\$sp）**：kT=0。adj_work[4] 已有 0（\$vr3 原本就与 \$sp 干涉）→ `is_new_edge=false`。decrement_degree(0): is_cand(\$sp)=false → skip。

**t_raw=1（\$vr0）**：kT=get_alias(1)=1（\$vr0 还未合并，alias[1]=1）。adj_work[4]原来是 {0, 3}，没有 1 → **is_new_edge=true**！

```
adj_work[4].insert(1)   → adj_work[4] = {0, 3, 1}
adj_work[1].insert(4)   → adj_work[1] = {0, 2, 4}

// u=4: !precolored, !on_stack, !coalesced → eff_degree[4]++ = 3
// kT=1: on_stack[1]=true → 不增加 eff_degree[1]

decrement_degree(1): on_stack[1]=true → skip
```

**t_raw=3（\$vr2）**：kT=3。adj_work[4] 已有 3 → `is_new_edge=false`。decrement_degree(3): is_cand=true, !on_stack, !coalesced → `eff_degree[3]--` = **2**。

combine 完毕后状态：

```
coalesced_flag = [F, F, T, F, F, F, F]   ($vr1=2 已合并)
alias          = [0, 1, 4, 3, 4, 5, 6]   ($vr1 的代表是 $vr3=4)
adj_work[4]    = {0, 1, 3}               ($vr3 现在代表 $vr1 和 $vr3)
eff_degree     = [-, 2, -, 2, 3, 1, -]   ($vr2=3 从3降到2，$vr3=4 从2升到3)
```

> **为什么 eff_degree[4] 升到 3？** combine 把 \$vr1 的邻居全部继承给 \$vr3。\$vr1有邻居 \$vr0(on_stack)，它与 \$vr3 之间原本没有边，add 了新边，所以 \$vr3 的 eff_degree +1。尽管 \$vr0 已在栈上，边仍被记录（代码注释中承认这是一个轻微的过计数）。
>
> 在实际 K=18 的项目中，这不会影响正确性——5 个虚拟寄存器加上任意过计数都远小于 18。

pending_moves 从 [(4,2)] 变为 **[]**（erase(it)）。

`coalesced_one=true`，执行 `continue` → **重启主循环**。

---

### 3.5 主循环第 4 轮：Simplify \$vr2（合并后）

**Phase 1 Simplify**：

move_related_set()：pending_moves 为空 → **move_related = {}**。

扫描候选节点（非 on_stack、非 coalesced）：
- 2: coalesced → 跳过
- 3 (\$vr2): not move_related, eff_degree=**2** < 3 → **PUSH！**

```
active_adj(3): adj_work[3] = {0, 2, 4}
  t_raw=0: kT=0, active → include
  t_raw=2: kT=get_alias(2)=4, !on_stack[4], !coalesced[4] → include (以 kT=4)
  t_raw=4: kT=get_alias(4)=4, 已在 seen 中 → skip（去重！）
active_adj(3) = {0, 4}

decrement_degree(0): is_cand=false → skip
decrement_degree(4): eff_degree[4]-- = 2
```

> **注意去重**：adj_work[3] 里同时有 2（\$vr1 原始 ID）和 4（\$vr3），但 alias[2]=4，所以两个 t_raw 都 resolve 到 kT=4，`seen` 集合过滤掉重复。这就是 `active_adj`用 `unordered_set<int> seen` 的原因。

状态：

```
on_stack   = [F, T, F, T, F, T, F]   ($vr2 入栈)
eff_degree = [-, 2, -, 2, 2, 1, -]
sel_stack  = [1, 5, 3]  (bottom→top)
```

---

### 3.6 主循环第 5 轮：Simplify \$vr3

候选节点只剩 4（\$vr3，代表合并后的 \$vr1+\$vr3）：

eff_degree[4] = 2 < 3 → **PUSH！**

```
active_adj(4): adj_work[4] = {0, 1, 3}
  t_raw=0: kT=0, active → include
  t_raw=1: on_stack[1]=true → exclude
  t_raw=3: on_stack[3]=true → exclude
active_adj(4) = {0}

decrement_degree(0): is_cand=false → skip
```

所有候选节点已在栈上或被合并，主循环退出。

```
sel_stack = [1, 5, 3, 4]  (bottom→top，4=$vr3 在栈顶)
```

---

### 3.7 Select — 从栈顶弹出着色

**Select 阶段**（RegAlloc.cpp:417）从栈顶开始弹出，为每个节点找一个**不与邻居冲突**的颜色（调色板索引）：

```cpp
while (!sel_stack.empty()) {
    int kU = sel_stack.top(); sel_stack.pop();

    // 收集邻居已用的颜色
    vector<bool> used(kK, false);
    for (int nb_raw : adj_work[kU]) {
        int kNb = get_alias(nb_raw);
        int nb_color = color[kNb];
        if (nb_color >= 0 && nb_color < kK)
            used[nb_color] = true;
    }

    // 分配第一个可用颜色
    int chosen = -1;
    for (int c = 0; c < kK; ++c) {
        if (!used[c]) { chosen = c; break; }
    }
    if (chosen >= 0) color[kU] = chosen;
    else actual_spills.insert(kU);   // Briggs 乐观着色失败 → 实际溢出
}
```

**弹出 id=4（\$vr3，栈顶）**：

邻居 adj_work[4] = {0, 1, 3}：
- nb_raw=0: color[0]=-1 → skip
- nb_raw=1: color[1]=-1 → skip（\$vr0 还没着色）
- nb_raw=3: color[3]=-1 → skip（\$vr2 还没着色）

used = [false, false, false]，chosen = **0**（第一个调色板索引 = \$t0）。

```
color[4] = 0   ($vr3 → $t0)
```

**弹出 id=3（\$vr2）**：

邻居 adj_work[3] = {0, 2, 4}：
- nb_raw=0: color[0]=-1 → skip
- nb_raw=2: get_alias(2)=4, color[4]=**0** → used[0]=true
- nb_raw=4: color[4]=0 → used[0]=true（已标）

used = [true, false, false]，chosen = **1**（\$t1）。

```
color[3] = 1   ($vr2 → $t1)
```

**弹出 id=5（\$vr4）**：

邻居 adj_work[5] = {0}：color[0]=-1 → skip。

used = [false, false, false]，chosen = **0**（\$t0）。

```
color[5] = 0   ($vr4 → $t0)
```

**弹出 id=1（\$vr0）**：

邻居 adj_work[1] = {0, 2, 4}（combine 时增加了 4）：
- nb_raw=0: color[0]=-1 → skip
- nb_raw=2: get_alias(2)=4, color[4]=0 → used[0]=true
- nb_raw=4: color[4]=0 → used[0]=true

used = [true, false, false]，chosen = **1**（\$t1）。

```
color[1] = 1   ($vr0 → $t1)
```

**传播合并节点的颜色**（RegAlloc.cpp:452）：

```cpp
for (int i = 0; i < kN; ++i) {
    if (coalesced_flag[i])
        color[i] = color[get_alias(i)];
}
// coalesced_flag[2]=true, get_alias(2)=4, color[4]=0
// → color[2] = 0   ($vr1 → $t0)
```

**最终着色结果**：

| 节点 ID | 寄存器名 | color | 物理寄存器 | 备注 |
|--------|---------|-------|-----------|------|
| 0 | \$sp | -1 | — | 非候选 |
| 1 | \$vr0 | 1 | **\$t1** | |
| 2 | \$vr1 | 0 | **\$t0** | 通过 coalesce 继承 \$vr3 的颜色 |
| 3 | \$vr2 | 1 | **\$t1** | |
| 4 | \$vr3 | 0 | **\$t0** | |
| 5 | \$vr4 | 0 | **\$t0** | |

**验证干涉关系不冲突**：
- \$vr0(\$t1) — \$vr1(\$t0)：颜色 1 vs 0 ✓（有干涉边，不同色）
- \$vr1(\$t0) — \$vr2(\$t1)：颜色 0 vs 1 ✓（有干涉边，不同色）
- \$vr2(\$t1) — \$vr3(\$t0)：颜色 1 vs 0 ✓（有干涉边，不同色）
- \$vr0(\$t1) — \$vr2(\$t1)：颜色 1 = 1，**但两者无干涉边** ✓（\$vr0 死在 \$vr2 定义之前）

`ColoringResult cr`（`RegAlloc.h:28`）返回：

```cpp
cr.color_by_node = [-1, 1, 0, 1, 0, 0, -1]
cr.actual_spills = {}   // 空！无溢出
```

---

## 4. RewriteBuffer — 将虚拟寄存器替换为物理寄存器

`RewriteBuffer`（RegAlloc.cpp:624）遍历 buffer，对每条指令调用 `RewriteInstruction`，把 `$vrX` 名字替换为物理寄存器名。

```cpp
static void RewriteBuffer(vector<MipsInst>& buffer, const RegIdMap& reg_ids,
                          const ColoringResult& cr, const vector<int>& spill_slots) {
    vector<MipsInst> new_buf;
    for (const auto& inst : buffer) {
        auto chunk = RewriteInstruction(inst, reg_ids, cr, spill_slots);
        new_buf.insert(new_buf.end(), chunk.begin(), chunk.end());
    }
    buffer = move(new_buf);
}
```

**两类映射函数**（RegAlloc.cpp:464–501）：

```cpp
// 用于 def（写目标）：spilled 节点用 $k0 作 scratch
static string MapDefReg(const string& reg, const RegIdMap& reg_ids,
                        const ColoringResult& cr) {
    if (!RegIdMap::IsVirtual(reg)) return reg;   // 已是物理寄存器，不变
    int node = reg_ids.Get(reg);
    if (cr.actual_spills.count(node))  return "$k0";          // 溢出 → scratch
    return kAllocatableRegNames[cr.color_by_node[node]];       // 查颜色
}

// 用于 use（读操作）：spilled 节点先 lw，放入 prefix
static string MapRegName(const string& reg, ..., bool is_use) {
    if (!RegIdMap::IsVirtual(reg)) return reg;
    int node = reg_ids.Get(reg);
    if (cr.actual_spills.count(node)) {
        string tmp = (scratch_used == 0) ? "$k0" : "$k1";
        ++scratch_used;
        if (is_use) prefix.push_back(MakeLw(tmp, spill_slots[vid], "$sp"));
        return tmp;
    }
    return kAllocatableRegNames[cr.color_by_node[node]];
}
```

**逐条指令的改写**：

**[3] ADDU \$vr2, \$vr0, \$vr1** → （RegAlloc.cpp:613，default 分支）

```
MapDefReg($vr2) → color[3]=1 → "$t1"
MapRegName($vr0, is_use) → color[1]=1 → "$t1"
MapRegName($vr1, is_use) → color[2]=0 → "$t0"
结果：addu $t1, $t1, $t0
```

**[4] MOVE \$vr3, \$vr1** → （RegAlloc.cpp:595，MOVE 分支）

```cpp
m.dst  = MapDefReg($vr3)          → color[4]=0 → "$t0"
m.src1 = MapRegName($vr1, is_use)  → color[2]=0 → "$t0"
prefix.empty() && m.dst == m.src1  → "$t0" == "$t0"
// 且 dst 不是 $k0/$k1
→ return out;   // out 为空！MOVE 指令被消除！
```

**这就是 Coalesce 的收益**：把 \$vr1 和 \$vr3 合并为同一物理寄存器 \$t0 之后，`move$vr3, $vr1` 变成 `move $t0, $t0`，在 RewriteInstruction 中被直接丢弃。

**[5] ADDU \$vr4, \$vr2, \$vr3**：

```
MapDefReg($vr4) → color[5]=0 → "$t0"
MapRegName($vr2) → color[3]=1 → "$t1"
MapRegName($vr3) → color[4]=0 → "$t0"
结果：addu $t0, $t1, $t0
```

**最终改写后的 buffer**（函数体部分）：

```
[1] lw   $t1, 0($sp)        # 原 lw $vr0
[2] lw   $t0, 4($sp)        # 原 lw $vr1
[3] addu $t1, $t1, $t0      # 原 addu $vr2, $vr0, $vr1
                             # 原 move $vr3, $vr1 → 已消除！
[5] addu $t0, $t1, $t0      # 原 addu $vr4, $vr2, $vr3
[6] sw   $t0, 8($sp)        # 原 sw $vr4
```

5 条虚拟寄存器指令变成了 4 条物理寄存器指令（减少了 1 条 MOVE）。

---

## 5. ApplyCalleeSaved — 被调用者保存寄存器

改写完成后，buffer 里可能出现了 \$s0-\$s7（callee-saved 寄存器）。这取决于着色结果：调色板索引 0–9 对应 \$t0-\$t9（caller-saved），10–17 对应 \$s0-\$s7（callee-saved）。

在我们的例子中，所有颜色都 ≤ 1（\$t0, \$t1），不涉及 \$s 系列，`ApplyCalleeSaved`是空操作。

若函数使用了 \$s 系列（例如 K=18 时虚拟寄存器数量较多，color 超过 9），`ApplyCalleeSaved`（RegAlloc.cpp:674）会：

1. 扫描 buffer，收集所有用到的 `$s*` 寄存器：
   ```cpp
   set<string> used_s;
   for (auto& m : buf) {
       consider(m.dst); consider(m.src1); consider(m.src2);
   }
   ```

2. 在 prologue 的 `addiu $sp,$sp,-F` 之后插入 `sw $s* , k*4($sp)`（保存）：
   ```cpp
   // 找到 prologue addiu：buf[i].dst=="$sp" && buf[i].imm<0
   for (int j = 0; j < count; ++j)
       buf.insert(buf.begin() + i+1+j, MakeSw(ordered[j], j*4, "$sp"));
   ```

3. 在每条 `lw $ra, offset($sp)` 之前插入 `lw $s*, k*4($sp)`（恢复）：
   ```cpp
   // 找到 epilogue 的 lw $ra
   // 在其前面插入所有 $s* 的 load
   ```

4. 调用 `AdjustStackFrameForCalleeSaved` 把所有 `offset($sp)` 操作上移 `kStackDelta`（为 callee-saved 腾出空间）。

这是一个纯机械的"模式识别 + 插入"过程，与染色算法本身无关。

---

## 6. 关键数据结构速查表

| 变量 | 类型 | 语义 | 不变式 |
|-----|------|------|-------|
| `adj_work[u]` | `unordered_set<int>` | u 的工作邻居集（含 coalesced 节点的原始ID） | Coalesce 后可能有冗余 ID，通过 `get_alias` 归一 |
| `eff_degree[u]` | `int` | u 的活跃邻居数 | Simplify 时精确，Coalesce 后对 on_stack 节点可能轻微过计数 |
| `alias[u]` | `int` | union-find 代表节点（path halving） | alias[u]=u 表示 u是自己的代表 |
| `on_stack[u]` | `bool` | u 是否已压入 sel_stack | 压入后不参与 active_adj、eff_degree 更新 |
| `coalesced_flag[u]` | `bool` | u 是否被合并（消失） | 合并后 u 的邻居职责转移给 alias[u] |
| `pending_moves` | `list<pair<int,int>>` | 尚未解决的 move pair | 每次 continue都要重新 get_alias 归一化 |
| `color[u]` | `int` | 调色板索引（0–17）或 -1 | Select 结束后，coalesced 节点通过传播获得颜色 |

---

## 7. 常见疑问

**Q：adj_work 里为什么保留 on_stack 节点的原始 ID？不会影响 george_ok 判断吗？**

`adj_work[u]` 从不删除元素——只增加（Coalesce 时）。on_stack 节点的 ID 仍保留，但`active_adj(u)` 在迭代时会过滤掉 `on_stack` 的节点，而 `george_ok(t, u)` 接收的是 `active_adj` 的输出（已过滤），所以不影响 Coalesce 决策。Select 阶段读 `adj_work[u]` 时不过滤 on_stack，因为此时所有节点都已 off_stack，邻居的颜色已确定。

**Q：pending_moves 里的节点 ID 随 Coalesce 会变吗？**

pending_moves 存的是**原始** ID（如 4 和 2），不会自动更新。每次用到时都要先 `get_alias()` 归一化为当前代表节点。这就是主循环中每轮都要重新调用 `move_related_set()`（内部有 `get_alias`）而不缓存的原因。

**Q：Coalesce 的 combine(u, v) 合并后，alias[v]=u。如果后来又 Coalesce u 到另一个节点 w，那 v 的代表是 w 还是 u？**

alias 是 union-find，`get_alias` 会通过路径压缩追踪到最终代表。alias[v]=u，alias[u]=w → get_alias(v) 先到 u，再跳到 w，返回 w。路径压缩（path halving）会在过程中缩短链。

---

*本文档为代码级演算，完整理解后建议结合 `cerr` 日志（`RegAllocator::Run` 中的 `[RegAlloc]` 输出）在实际编译一个函数时对照验证。*

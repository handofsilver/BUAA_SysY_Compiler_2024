# IR 层循环依赖与不完整类型问题修复记录

**Date**: 2026-02-15
**Context**: LLVM IR 数据结构（Instruction / BasicBlock / Function / Module）在 C++ 中的头文件依赖与析构链。

---

## 一、问题现象（编译报错）

在引入 `Function.cpp` 并定义 `Function::~Function() = default`、且 `BasicBlock` 持有 `std::list<std::unique_ptr<Instruction>>` 后，出现两类错误：

### 1. “definition of implicitly declared destructor”

- **位置**：`Function.cpp` 中写有 `Function::~Function() = default;`
- **原因**：头文件 `Function.h` 里没有显式声明析构函数，编译器认为析构是“隐式声明”的，在 .cpp 里再定义就变成重复定义。

### 2. “invalid application of 'sizeof' to an incomplete type 'ir::Instruction'”

- **位置**：在编译 `Function.cpp`（或任何会间接触发“销毁 `BasicBlock`”的翻译单元）时，在 `std::unique_ptr` / `std::list` 的析构实例化中报错。
- **原因**：销毁 `unique_ptr<Instruction>` 或 `list<unique_ptr<Instruction>>` 时，编译器需要**完整类型** `Instruction`（用于 `sizeof` 和析构调用）。而在当时的包含关系下，该翻译单元里只看到了 `Instruction` 的**前向声明**，没有看到类定义，因此报“incomplete type”。

---

## 二、为何说“十分隐蔽”

1. **依赖链是间接的**
   出错点往往在 `Function.cpp` 或 `Module.cpp`，但真正缺的是 `Instruction` 的完整定义。
   逻辑链是：
   `~Module()` / `~Function()` → 销毁 `vector<unique_ptr<Function>>` / `vector<unique_ptr<BasicBlock>>` → 销毁 `BasicBlock` → 销毁成员 `list<unique_ptr<Instruction>>` → 需要完整 `Instruction`。
   编译器只会在**第一次实例化**这串析构的翻译单元里报错，所以报错位置和“谁没包含谁”的关系不直观。

2. **前向声明“看起来够用”**
   `BasicBlock` 里只有 `BasicBlock* parent_` 等指针时，前向声明足够；
   但一旦成员是 `std::list<std::unique_ptr<Instruction>>`，在**析构**或**实例化容器**时就必须看到 `Instruction` 的完整定义。
   前向声明能通过编译声明，却会在析构/模板实例化时暴露问题。

3. **循环依赖的“隐藏”**
   - `Instruction` 需要 `BasicBlock*`（父块）；
   - `BasicBlock` 需要 `list<unique_ptr<Instruction>>`（指令列表）。
   若头文件互相包含，就会形成 **Instruction.h ↔ BasicBlock.h** 的循环。
   用前向声明可以避免“直接循环包含”，但“谁在哪个 .cpp 里先包含谁”仍然决定了在哪个 TU 里能拿到完整类型，容易在新增/移动 .cpp 时再次踩坑。

---

## 三、最终采用的工程实践

采用 **“头文件只做声明，实现全部进 .cpp”** 的方式，既消除不完整类型问题，又避免循环包含，也便于维护。

### 3.1 头文件（Instruction.h）

- **只保留声明**：所有指令类（Instruction、AllocaInst、LoadInst、StoreInst、BinaryInst、BranchInst、CallInst、ReturnInst、GetElementPtrInst、IcmpInst、ZextInst、TruncInst）的成员函数仅在头文件中**声明**，不写函数体。
- **只前向声明 BasicBlock**：`class BasicBlock;`，**不**在 `Instruction.h` 里 `#include "ir/BasicBlock.h"`。
- **依赖**：仅包含 `User.h`、`AST.h`、`<string>`、`<vector>` 等与声明相关的头文件。

这样 `Instruction.h` 不依赖 `BasicBlock.h`，不会形成“Instruction.h → BasicBlock.h → Instruction.h”的循环。

### 3.2 实现文件（Instruction.cpp）

- **集中所有实现**：上述所有指令类的构造函数和成员函数**定义**都放在 `Instruction.cpp` 中。
- **在此处包含 BasicBlock**：在 .cpp 里 `#include "ir/Instruction.h"` 和 `#include "ir/BasicBlock.h"`，在此翻译单元中同时看到完整的 `Instruction` 和 `BasicBlock`，因此 BranchInst 等需要 `BasicBlock*` 转 `Value*`、`static_cast<BasicBlock*>` 的代码都可以安全编译。

### 3.3 BasicBlock.h 的包含关系

- **BasicBlock.h 包含 Instruction.h**：
  `#include "ir/Instruction.h"`，这样在定义 `BasicBlock` 时，`Instruction` 已经是**完整类型**，
  `std::list<std::unique_ptr<Instruction>>` 的析构等用法合法。
- **依赖方向**：`BasicBlock.h` → `Instruction.h`；`Instruction.h` 不包含 `BasicBlock.h`，仅前向声明。
  单向依赖，无循环。

### 3.4 Function / Module 的 .cpp

- **无需再“先包含 Instruction.h”**：因为 `BasicBlock.h` 已经包含 `Instruction.h`，任何包含 `Function.h` 或 `Module.h` 的翻译单元在展开到 `BasicBlock.h` 时都会得到完整的 `Instruction`，析构链不会遇到不完整类型。
- **Function 析构**：不在 .cpp 里单独定义 `~Function()`，使用头文件中的默认析构即可（或按需在头文件中显式声明 `~Function() = default;`）。

---

## 四、小结与可复用的原则

| 要点 | 说明 |
|------|------|
| **谁需要完整类型** | 含有 `unique_ptr<T>`、`vector<T>` 等会析构 T 的成员时，在**定义/析构**该成员的翻译单元中，T 必须是完整类型。 |
| **前向声明的局限** | 前向声明只能用于指针/引用声明；一旦涉及“析构 T”或“sizeof(T)”就必须包含 T 的完整定义。 |
| **避免循环包含** | 若 A 需要 B 的完整类型、B 需要 A 的指针：只在 A 的头文件中前向声明 B，在 A 的 .cpp 中再 `#include "B.h"` 并写清实现。 |
| **推荐结构** | 头文件：只声明 + 前向声明；.cpp：包含所需完整类型头文件并写实现。这样依赖清晰、编译稳定，也便于后续按类拆分 .cpp。 |

这份记录可作为日后在 IR 层或类似“双向引用 + 容器持有 unique_ptr”的 C++ 模块中，排查“incomplete type”“循环依赖”的参考。

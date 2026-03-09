# 重构：从 IRGenVisitor 中抽取 IRDeclEmitter

**日期**：2026-02-28
**分支**：`llvm_ir`
**动机**：单一职责——将底层的内存/声明 IR 生成逻辑与 AST 遍历逻辑分离。

---

## 1. 问题

`IRGenVisitor` 中的 `EmitGlobalConstDef`、`EmitLocalConstDef`、`EmitGlobalVarDef`、`EmitLocalVarDef` 等函数混杂了三个层面的逻辑：

1. **AST 遍历与求值**（Visitor 的本职工作）
2. **内存布局策略**（entry-block alloca、全局变量创建）
3. **底层指令拼装**（GEP + Store 循环、零填充）

这导致单个函数动辄 80-100 行，可读性差，修改成本高。`CreateEntryBlockAlloca`、`EmitGlobalStringLiteral` 等辅助函数散落在 Visitor 中，职责混乱。

## 2. 方案：IRDeclEmitter

新增 `IRDeclEmitter` 类（`include/irgen/IRDeclEmitter.h` + `src/irgen/IRDeclEmitter.cpp`），封装所有**内存分配与声明相关**的底层 IR 生成逻辑。

### 2.1 类接口

```cpp
class IRDeclEmitter {
public:
    explicit IRDeclEmitter(IRGenContext& ctx);

    // 全局定义：标量 (size=0) 或一维数组 (size>0)；init_vals 为空则使用 zeroinitializer。
    void EmitGlobal(const std::string& name, ir::Type* elem_type, int size,
                    const std::vector<int>& init_vals, bool is_const);

    // 全局常量字符串字面量（以空字符结尾的 i8 数组）。
    ir::Value* EmitGlobalStringLiteral(const std::string& str);

    // 局部 entry-block alloca。size=0 → 标量，>0 → [size x elem_type] 数组。
    ir::Value* EmitLocalAlloca(const std::string& name, ir::Type* elem_type, int size);

    // GEP + Store 循环进行局部数组初始化；对 init_vals.size() 之后的元素补零。
    void EmitLocalArrayInit(ir::Value* alloca_ptr, ir::Type* elem_type, int size,
                            const std::vector<ir::Value*>& init_vals);

    // 通过全局字面量 + 逐元素 load/store 将字符串拷入局部数组；尾部补零。
    void EmitLocalStringInit(ir::Value* alloca_ptr, ir::Type* elem_type, int size,
                             const std::string& str);
};
```

### 2.2 设计约束

- **无 AST 依赖**：`IRDeclEmitter` 不包含 `#include "AST.h"`。输入仅为纯 C++ 类型（`vector`、`string`）和 IR 指针。
- **依赖注入**：所有 IR 资源（`module`、`builder`、`types`）通过注入的 `IRGenContext&` 获取。
- **所有权不变**：Emitter 不拥有任何 IR 对象，它通过 `ctx_` 的 API 创建它们。

## 3. 迁移内容

| 从 `IRGenVisitor`             | 至 `IRDeclEmitter`              | 可见性   |
|-------------------------------|---------------------------------|----------|
| `CreateEntryBlockAlloca()`    | `CreateEntryBlockAlloca()`      | private  |
| `EmitGlobalStringLiteral()`   | `EmitGlobalStringLiteral()`     | public   |
| `BuildConstArrayInit()`       | `BuildConstArrayInit()`         | private  |
| `printf_str_counter_`         | `str_literal_counter_`          | private  |
| GEP+Store 循环逻辑            | `EmitLocalArrayInit/StringInit` | public   |
| 全局变量创建逻辑              | `EmitGlobal()`                  | public   |

## 4. 保留在 IRGenVisitor 中的内容

| 内容                          | 原因                                                     |
|-------------------------------|----------------------------------------------------------|
| `EmitGlobalConstDef()` 等     | 仍负责 AST 遍历（从 AST 节点中收集初始值）               |
| `BuildConstScalarInit()`      | 依赖 `current_decl_btype_`（Visitor 遍历状态）            |
| `GetCurDeclType()`            | 依赖 `current_decl_btype_`                               |
| `EvalArraySizeFromConstExp()` | 依赖 AST 的 `ConstExp*`                                  |

## 5. 调用点更新

| 文件                         | 修改内容                                                          |
|------------------------------|-------------------------------------------------------------------|
| `IRGenVisitor.cpp`           | 构造函数添加 `decl_emitter_(ctx_)`；`EmitShortCircuitAND/OR` 使用 `decl_emitter_.EmitLocalAlloca` |
| `IRGenVisitorFunc.cpp`       | `VisitFuncDef` 形参 alloca：`decl_emitter_.EmitLocalAlloca("", alloc_ty, 0)` |
| `IRGenVisitorStmt.cpp`       | `VisitPrintfStmt`：`decl_emitter_.EmitGlobalStringLiteral(literal)` |

## 6. 行为变更

### 6.1 局部数组零填充（新增行为）

**重构前**：以 ExpList 初始化的局部数组**不会**对剩余元素补零。
**重构后**：`EmitLocalArrayInit` 无条件对 `init_vals.size()` 到 `size` 之间的元素补零。

这是更安全的默认行为，与 SysY 部分初始化语义一致（类似 C 的规则："如果提供了任何初始化器，则剩余元素零初始化"）。

### 6.2 语义等价性

除上述零填充外，所有其他路径产生的 IR 输出完全一致。全局数组原本就有零填充。字符串初始化逻辑（全局字面量 + 逐字节拷贝 + 尾部补零）完整保留。

## 7. 文件清单

**新增文件**：
- `include/irgen/IRDeclEmitter.h`
- `src/irgen/IRDeclEmitter.cpp`

**修改文件**：
- `include/IRGenVisitor.h` — 添加 `IRDeclEmitter decl_emitter_`；移除 `CreateEntryBlockAlloca`、`EmitGlobalStringLiteral`、`BuildConstArrayInit`、`printf_str_counter_`
- `src/IRGenVisitor.cpp` — 重构 `Emit{Global,Local}{Const,Var}Def` 以委托给 `decl_emitter_`；移除已迁移的实现
- `src/IRGenVisitorFunc.cpp` — `CreateEntryBlockAlloca` → `decl_emitter_.EmitLocalAlloca`
- `src/IRGenVisitorStmt.cpp` — `EmitGlobalStringLiteral` → `decl_emitter_.EmitGlobalStringLiteral`

## 8. 重构后代码流程示例

### 重构前：`EmitLocalVarDef` 处理 `int arr[3] = {1, a, 3};`

```
IRGenVisitor::EmitLocalVarDef(VarDef&, ir::Type*)
├── CreateEntryBlockAlloca([3 x i32])     ← 内存布局策略
├── RegisterVariable("arr", alloca)
├── for i in 0..2:
│   ├── list[i]->Accept(*this)            ← AST 遍历
│   ├── ConvertToTargetType(temp_value_)
│   ├── GetInt32Constant(i)               ← 底层指令拼装
│   ├── CreateGEP(ptr_type, alloca, 0, i)
│   └── CreateStore(val, gep)
```

### 重构后：

```
IRGenVisitor::EmitLocalVarDef(VarDef&, ir::Type*)
├── decl_emitter_.EmitLocalAlloca("arr", i32, 3)   ← 内存分配委托
├── RegisterVariable("arr", alloca_ptr)
├── for exp in list:
│   ├── exp->Accept(*this)                          ← AST 遍历（Visitor 本职）
│   └── vals.push_back(ConvertToTargetType(...))
└── decl_emitter_.EmitLocalArrayInit(ptr, i32, 3, vals)  ← 指令拼装委托
    ├── for i in 0..vals.size(): GEP + Store
    └── for i in vals.size()..3: GEP + Store(0)   ← 零填充
```

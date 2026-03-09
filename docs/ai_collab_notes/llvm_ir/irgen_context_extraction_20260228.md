# IRGenContext 提取重构笔记

**Date**: 2026-02-28

**Phase**: Code Generation (AST → LLVM IR)

**Type**: 纯结构重构（无逻辑变更）

---

## 一、动机

`IRGenVisitor` 同时承担了三种不同性质的职责：

| 类别 | 成员举例 | 性质 |
|------|----------|------|
| 环境资源 | `module_`, `builder_`, `types_`, `current_function_` | 全局共享的 IR 生成上下文 |
| 符号表 | `Scope`, `scopes_`, `PushScope()`, `LookupVariable()` | 作用域管理 |
| 遍历状态 | `temp_value_`, `is_lval_mode_`, `break_targets_` | AST 遍历的临时状态 |

**问题**：如果要把复杂逻辑（如数组初始化）抽到独立的辅助类，该类无法访问 `module`/`builder`/符号表——除非传一大堆参数或直接持有 `IRGenVisitor&`（高度耦合）。

**解决**：引入 `IRGenContext`，将环境资源和符号表集中到一个可传递的轻量对象中。

---

## 二、重构前后对比

### 重构前

```
IRGenVisitor
├── module_          : unique_ptr<Module>
├── builder_         : unique_ptr<IRBuilder>
├── types_           : TypeManager&
├── current_function_: Function*
├── scopes_, PushScope(), PopScope(), RegisterVariable(), LookupVariable()
├── temp_value_, is_lval_mode_, func_arg_want_pointer_, is_global_, ...
└── Visit* 方法 + 辅助方法
```

### 重构后

```
IRGenVisitor
├── module_  : unique_ptr<Module>    ← 所有权不变
├── builder_ : unique_ptr<IRBuilder> ← 所有权不变
├── types_   : TypeManager&
├── ctx_     : IRGenContext           ← 值成员，非拥有指针
│   ├── module   : Module*
│   ├── builder  : IRBuilder*
│   ├── types    : TypeManager&
│   ├── current_function : Function*
│   ├── scopes_ (符号表)
│   └── PushScope / PopScope / RegisterVariable / LookupVariable
├── temp_value_, is_lval_mode_, func_arg_want_pointer_, is_global_, ...  ← 不动
└── Visit* 方法 + 辅助方法  ← 逻辑不动
```

---

## 三、关键设计决策

### 1. `ctx_` 用值成员而非 `unique_ptr`

`IRGenContext` 不需要多态，也无需延迟构造。C++ 按声明顺序初始化成员，所以只要 `module_` 和 `builder_` 在 `ctx_` 之前声明，构造函数初始化列表中即可安全取 `.get()`：

```cpp
IRGenVisitor::IRGenVisitor()
    : module_(std::make_unique<ir::Module>()),
      builder_(std::make_unique<ir::IRBuilder>()),
      ctx_(module_.get(), builder_.get(), types_) {}
```

### 2. `IRScopeGuard` 改为接收 `IRGenContext&`

重构前 `IRScopeGuard` 持有 `IRGenVisitor&`，调用 `visitor_->PushScope()`。重构后 `PushScope/PopScope` 迁入 `IRGenContext`，`IRScopeGuard` 改为：

```cpp
IRScopeGuard::IRScopeGuard(IRGenContext& ctx) : ctx_(&ctx) {
    ctx_->PushScope();
}
IRScopeGuard::~IRScopeGuard() {
    ctx_->PopScope();
}
```

好处：`IRGenVisitor` 不再需要公开 `PushScope/PopScope`，接口更干净。

### 3. 严格不迁移遍历状态

以下成员必须留在 `IRGenVisitor` 中，因为它们与 AST 遍历逻辑紧密耦合：

- `temp_value_` — 表达式求值的临时传递
- `is_lval_mode_` — LVal 访问模式
- `func_arg_want_pointer_` — 函数实参指针传递标志
- `current_decl_btype_` — 当前声明的基础类型
- `is_global_` — 全局/局部作用域标志
- `break_targets_` / `continue_targets_` — 循环控制流栈
- `call_args_` — 函数调用参数收集
- `printf_str_counter_` — printf 字符串常量计数器

---

## 四、机械替换规则

所有 5 个 `IRGenVisitor*.cpp` 文件中，按以下规则全局替换：

| 替换前 | 替换后 |
|--------|--------|
| `module_->` | `ctx_.module->` |
| `builder_->` | `ctx_.builder->` |
| `types_.` | `ctx_.types.` |
| `current_function_` | `ctx_.current_function` |
| `RegisterVariable(...)` | `ctx_.RegisterVariable(...)` |
| `LookupVariable(...)` | `ctx_.LookupVariable(...)` |
| `IRScopeGuard guard(*this)` | `IRScopeGuard guard(ctx_)` |
| `IRScopeGuard scope_guard(*this)` | `IRScopeGuard scope_guard(ctx_)` |

注意：`Translate()` 中 `return std::move(module_)` 保持不变（这是所有权转移，不走 ctx_）。

---

## 五、修改文件清单

| 文件 | 操作 |
|------|------|
| `include/IRGenContext.h` | **新增** — Context 类定义 |
| `src/IRGenContext.cpp` | **新增** — 构造函数 + 符号表方法实现 |
| `include/IRScopeGuard.h` | 修改 — `IRGenVisitor&` → `IRGenContext&` |
| `src/IRScopeGuard.cpp` | 修改 — 同上 |
| `include/IRGenVisitor.h` | 修改 — 移除迁移成员，新增 `ctx_` |
| `src/IRGenVisitor.cpp` | 修改 — 机械替换字段引用 |
| `src/IRGenVisitorDecl.cpp` | 修改 — 机械替换 |
| `src/IRGenVisitorFunc.cpp` | 修改 — 机械替换 |
| `src/IRGenVisitorStmt.cpp` | 修改 — 机械替换 |
| `src/IRGenVisitorExpr.cpp` | 修改 — 机械替换 |

---

## 六、验证

- 编译通过（`cmake --build .`，0 errors）。
- 用 `testfile.txt`（含数组传参、for 循环、printf）运行，`llvm_ir.txt` 输出与重构前完全一致。

---

## 七、后续收益

现在如果要将数组初始化逻辑抽到独立的 `ArrayInitHelper` 类中，只需传一个 `IRGenContext&` 即可——它能访问 `module`、`builder`、`types`、符号表，但完全不知道 `IRGenVisitor` 的遍历状态。这就是解耦的意义。

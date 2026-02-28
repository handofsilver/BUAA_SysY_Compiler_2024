# TypeManager 重构：从 Module 中提取类型管理

**Date**: 2026-02-28

**Branch**: `llvm_ir`

**Status**: Phase 1-4 全部完成。Module 已不含任何类型管理逻辑。

---

## 一、问题：重构前 Module 承担了过多职责

重构前，`Module` 类是一个巨型类，同时承担了四种职责：

| 职责 | 对应成员 |
|------|----------|
| 全局变量管理 | `global_vars_` |
| 函数管理 | `functions_`, `function_types_` |
| 常量池 | `constants_`, `const_i32_cache_`, `const_i8_cache_`, `other_constants_` |
| **类型管理** | `integer_types_`, `pointer_types_`, `ptr_type_cache_`, `array_types_`, `void_type_`, `label_type_` |

其中类型管理存在两个具体问题：

1. **ArrayType 无缓存**：每次调用 `GetArrayType(i32, 10)` 都会 `make_unique` 一个新对象。如果对同一数组类型重复请求（如多个 `int arr[10]`），会产生多个不同的 `ArrayType*`，既浪费内存也无法用指针比较判断类型相等。
2. **违背单一职责**：Module 的核心职责是"持有全局变量和函数"，类型系统的生命周期管理不应由它负责。

### 重构前 Module 的类型管理代码（已删除）

```cpp
// --- Module.h (BEFORE, 已删除的成员) ---
std::vector<std::unique_ptr<IntegerType>> integer_types_;   // i32, i8, i1
std::vector<std::unique_ptr<PointerType>> pointer_types_;
std::unordered_map<Type*, PointerType*> ptr_type_cache_;
std::vector<std::unique_ptr<ArrayType>> array_types_;       // 无缓存！每次新建
std::unique_ptr<VoidType> void_type_;
std::unique_ptr<LabelType> label_type_;
```

```cpp
// --- Module.cpp (BEFORE, 已删除的实现) ---
// GetI32Type / GetI8Type / GetI1Type 共享一个 lazy-init 逻辑：
IntegerType* Module::GetI32Type() {
    if (integer_types_.empty()) {
        integer_types_.push_back(std::make_unique<IntegerType>(32));
        integer_types_.push_back(std::make_unique<IntegerType>(8));
        integer_types_.push_back(std::make_unique<IntegerType>(1));
    }
    return integer_types_[0].get();
}

// ArrayType 每次都 new，没有缓存复用：
ArrayType* Module::GetArrayType(Type* element_type, unsigned num_elements) {
    assert(element_type && num_elements > 0);
    array_types_.push_back(std::make_unique<ArrayType>(element_type, num_elements));
    return array_types_.back().get();
}
```

---

## 二、解决方案：TypeManager 享元单例

引入 `TypeManager` 类（Flyweight Pattern），承担所有 Type 对象的创建、缓存和生命周期管理。

### 2.1 所有权模型

```
TypeManager (singleton, process lifetime)
├── owned_types_: vector<unique_ptr<Type>>    ← 一切 Type 对象的唯一归宿
│   ├── IntegerType(32)   ← i32_type_ 指向这里
│   ├── IntegerType(8)    ← i8_type_ 指向这里
│   ├── IntegerType(1)    ← i1_type_ 指向这里
│   ├── VoidType          ← void_type_ 指向这里
│   ├── LabelType         ← label_type_ 指向这里
│   ├── PointerType(→i32) ← ptr_cache_[i32_type_] 指向这里
│   ├── ArrayType(i32,10) ← array_cache_[{i32,10}] 指向这里
│   └── ...
│
├── ptr_cache_: unordered_map<Type*, PointerType*>           ← 复合类型缓存
└── array_cache_: map<pair<Type*,unsigned>, ArrayType*>      ← 复合类型缓存
```

关键设计点：

- **统一生命周期容器** (`owned_types_`)：所有 Type 对象被 `unique_ptr` 持有，TypeManager 析构时自动释放。
- **原始类型预创建**：i1、i8、i32、void、label 在构造函数中立即创建，不需要 lazy-init 判断。
- **复合类型带缓存**：PointerType 和 ArrayType 按 key 缓存，命中则直接返回，未命中则创建后加入 `owned_types_` 和缓存。

### 2.2 TypeManager 文件

**`include/ir/TypeManager.h`** — 头文件，声明单例接口：

```cpp
class TypeManager {
public:
    static TypeManager& Get();              // 进程级单例 (Meyers singleton)

    IntegerType* GetI32Type() const;        // 原始类型直接返回缓存指针
    IntegerType* GetI8Type() const;
    IntegerType* GetI1Type() const;
    VoidType*    GetVoidType() const;
    LabelType*   GetLabelType() const;

    PointerType* GetPointerType(Type*);     // 复合类型，带缓存
    ArrayType*   GetArrayType(Type*, unsigned);

private:
    TypeManager();                          // 预创建原始类型
    std::vector<std::unique_ptr<Type>> owned_types_;
    // ... 缓存指针和 map ...
};
```

**`src/ir/TypeManager.cpp`** — 实现，核心是 `GetArrayType` 的缓存逻辑：

```cpp
ArrayType* TypeManager::GetArrayType(Type* element_type, unsigned num_elements) {
    assert(element_type && num_elements > 0);
    auto key = std::make_pair(element_type, num_elements);
    auto it = array_cache_.find(key);
    if (it != array_cache_.end()) {
        return it->second;                  // 缓存命中，直接返回
    }
    auto arr = std::make_unique<ArrayType>(element_type, num_elements);
    ArrayType* raw = arr.get();
    owned_types_.push_back(std::move(arr)); // 存入统一生命周期容器
    array_cache_[key] = raw;                // 记入缓存池
    return raw;
}
```

---

## 三、迁移记录

### Phase 1: 基础设施搭建

- 新建 `TypeManager.h/cpp`。
- 从 Module 中删除 6 个类型相关成员变量。
- Module 上保留 7 个类型 getter 方法作为临时转发层。

### Phase 2: 迁移 TypeMapping

- `TypeMapping.h/cpp` 中的 3 个函数签名删除了 `ir::Module*` 参数。
- 实现改为 `auto& tm = ir::TypeManager::Get(); tm.GetI32Type()` 等。
- `IRGenVisitorFunc.cpp` 调用处相应去掉了 `module_.get()` 参数。
- **动机**：TypeMapping 是无状态纯映射，不应依赖 Module 实例。

### Phase 3: 迁移 IRBuilder

- `IRBuilder.h` 中删除了 `Module* module_` 成员和 `SetModule()` 方法。
- `CreateRetVoid()` 改为 `TypeManager::Get().GetVoidType()`。
- `IRGenVisitor.cpp` 构造函数中删除了 `builder_->SetModule(module_.get())` 调用。
- `#include "ir/Module.h"` 替换为 `#include "ir/TypeManager.h"`。

### Phase 4: 迁移 IRGenVisitor + 清除 Module 转发层

- `IRGenVisitor.h` 新增 `ir::TypeManager& types_` 成员引用（默认初始化为 singleton）。
- 全部 ~40 处 `module_->GetXxxType()` 替换为 `types_.GetXxxType()`。
- Module 的 7 个类型转发方法声明和实现彻底删除。
- Module.cpp 内部的 `GetLibFunctionDescriptor` 等改为 `TypeManager::Get()` 调用。

**替换模式**：

| 旧调用 | 新调用 |
|--------|--------|
| `module_->GetI32Type()` | `types_.GetI32Type()` |
| `module_->GetI8Type()` | `types_.GetI8Type()` |
| `module_->GetI1Type()` | `types_.GetI1Type()` |
| `module_->GetVoidType()` | `types_.GetVoidType()` |
| `module_->GetPointerType(x)` | `types_.GetPointerType(x)` |
| `module_->GetArrayType(x, n)` | `types_.GetArrayType(x, n)` |

**未改动的 Module 方法**（非类型，仍通过 `module_->` 调用）：
`GetFunction`, `CreateFunction`, `CreateGlobalVar`, `GetInt32Constant`, `GetInt8Constant`, `CreateConstantArray`

---

## 四、重构后的职责边界

```
TypeManager (singleton)
  └── 类型的创建、缓存、生命周期

Module (per-compilation instance)
  ├── 全局变量 (global_vars_)
  ├── 函数 (functions_, function_types_)
  └── 常量池 (constants_, const_i32_cache_, const_i8_cache_, other_constants_)

IRBuilder (stateless w.r.t. types)
  └── 指令创建 (insert into BasicBlock)

TypeMapping (stateless free functions)
  └── BType/OpType → IR type/predicate 映射

IRGenVisitor
  ├── types_: TypeManager&    ← 所有类型查询
  ├── module_: Module          ← 常量、函数、全局变量
  └── builder_: IRBuilder      ← 指令创建
```

---

## 五、最终文件索引

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `include/ir/TypeManager.h` | **新增** | TypeManager 享元单例声明 |
| `src/ir/TypeManager.cpp` | **新增** | TypeManager 实现 |
| `include/ir/Module.h` | 修改 | 删除所有类型相关成员和方法 |
| `src/ir/Module.cpp` | 修改 | 删除类型方法实现；内部改用 TypeManager |
| `include/IRBuilder.h` | 修改 | 删除 `module_` 成员和 `SetModule()`；`CreateRetVoid` 用 TypeManager |
| `include/irgen/TypeMapping.h` | 修改 | 函数签名去掉 `Module*` 参数 |
| `src/irgen/TypeMapping.cpp` | 修改 | 实现改用 TypeManager |
| `include/IRGenVisitor.h` | 修改 | 新增 `types_` 成员 |
| `src/IRGenVisitor.cpp` | 修改 | ~20 处 `module_->` → `types_.` |
| `src/IRGenVisitorExpr.cpp` | 修改 | ~7 处 `module_->` → `types_.` |
| `src/IRGenVisitorStmt.cpp` | 修改 | ~8 处 `module_->` → `types_.` |
| `src/IRGenVisitorFunc.cpp` | 修改 | ~4 处改动（TypeMapping 参数 + 类型调用） |

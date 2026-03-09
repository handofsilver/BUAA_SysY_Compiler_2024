# TypeManager 重构：从 Module 中提取类型管理

**Date**: 2026-02-28

**Branch**: `llvm_ir`

**Status**: Phase 1 完成（基础设施搭建）；Phase 2/3 待执行。

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

### 2.2 新增文件

**`include/ir/TypeManager.h`** — 头文件，声明单例接口：

```cpp
class TypeManager {
public:
    static TypeManager& Get();              // 进程级单例

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

### 2.3 Module 的变化

**删除的成员变量**（6 个）：

```
integer_types_, pointer_types_, ptr_type_cache_, array_types_, void_type_, label_type_
```

**保留的成员变量**（仅常量池和 IR 对象）：

```
global_vars_, functions_, function_types_, constants_, const_i32_cache_, const_i8_cache_, other_constants_
```

**类型 getter 方法保留为转发层**（Module.cpp 中变为一行委托）：

```cpp
IntegerType* Module::GetI32Type() { return TypeManager::Get().GetI32Type(); }
// ...其余 6 个方法同理
```

> 为什么不直接删除这些方法？因为 IRGenVisitor 的 40+ 处调用全部写成
> `module_->GetI32Type()` 形式，本次约定不触碰 Visitor 文件。
> 转发层的存在是临时的，在后续步骤中将被清除。

---

## 三、下一步重构路线图

以下按推荐执行顺序排列。每一步都是独立可编译、可 review 的原子提交。

### Phase 2: 迁移 TypeMapping — 剥离 `ir::Module*` 参数

**目标**：`TypeMapping.h/cpp` 中的函数签名从 `f(BType, ir::Module*)` 改为 `f(BType)`，内部直接调用 `TypeManager::Get()`。

**影响范围**：
- `include/irgen/TypeMapping.h` — 函数签名删除 `ir::Module*` 参数
- `src/irgen/TypeMapping.cpp` — 实现改为 `TypeManager::Get().GetI32Type()` 等
- `src/IRGenVisitor.cpp` / `src/IRGenVisitorFunc.cpp` — 调用处删除 `module_` 参数

**动机**：TypeMapping 是纯映射工具，不应该依赖 Module 实例。类型信息是全局静态的，与具体 Module 无关。

### Phase 3: 迁移 IRBuilder — 去除 `module_` 成员

**目标**：IRBuilder 的 `CreateRetVoid()` 目前通过 `module_->GetVoidType()` 获取 void 类型。改为 `TypeManager::Get().GetVoidType()`，之后可以删除 `IRBuilder::SetModule()` 和 `module_` 成员。

**影响范围**：
- `include/IRBuilder.h` — 删除 `module_` 成员和 `SetModule()`
- `src/IRGenVisitor.cpp` — 删除 `builder_->SetModule(module_)` 的调用

### Phase 4: 迁移 IRGenVisitor — 消除 Module 类型转发层

**目标**：将 Visitor 中所有 `module_->GetXxxType()` 调用替换为 `TypeManager::Get().GetXxxType()`（约 40 处）。完成后，删除 Module 上的 7 个类型转发方法。

**影响范围**：
- `src/IRGenVisitor*.cpp` — ~40 处机械替换
- `include/ir/Module.h` + `src/ir/Module.cpp` — 删除 7 个转发方法

**当前调用分布**（供替换时参考）：

| 文件 | 调用数 | 主要方法 |
|------|--------|----------|
| `IRGenVisitor.cpp` | ~20 | `GetI32Type`, `GetI8Type`, `GetPointerType`, `GetArrayType` |
| `IRGenVisitorExpr.cpp` | ~6 | `GetI1Type`, `GetI32Type`, `GetVoidType`, `GetPointerType` |
| `IRGenVisitorStmt.cpp` | ~8 | `GetVoidType`, `GetI32Type` |
| `IRGenVisitorFunc.cpp` | ~1 | `GetI32Type` |

### Phase 5（可选）: FunctionType 缓存

**目标**：将 `FunctionType` 的创建也纳入 TypeManager，Module 中的 `function_types_` 迁移至 TypeManager。FunctionType 可以按 `(return_type, param_types)` 做缓存，避免为相同签名的库函数重复创建。

**优先级**：低。当前项目规模下 FunctionType 数量极少（~10 个），收益不大。

---

## 四、文件索引

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `include/ir/TypeManager.h` | **新增** | TypeManager 享元单例声明 |
| `src/ir/TypeManager.cpp` | **新增** | TypeManager 实现 |
| `include/ir/Module.h` | 修改 | 删除 6 个类型成员，保留转发方法声明 |
| `src/ir/Module.cpp` | 修改 | 类型方法实现替换为 TypeManager 委托 |

# 函数指针/数组形参实参传递修正记录

本文档记录“以数组名或指针作为实参调用函数时，应传地址而非首元素值”的 IR 生成问题、原因与修正方式。

## 一、现象

- 调用接受数组/指针形参的函数（如 `flatten_matrix_op(arr, 2, 3, 15, f)`）时，生成的程序运行**段错误**。
- 用 clang 将生成的 `merged.ll` 编成可执行文件并运行，同样崩溃；排除 lli 未链接 libc 导致的问题后，可确认是**生成 IR 的语义错误**。

## 二、原因

在 SysY/C 中，**数组类型形参**等价于**指针形参**，调用时实参应传递**数组首地址**，不能传递“对首地址做 load 得到的 i32”。

### 错误 IR 示例

在 `sort_and_grade(ptr %0, ptr %1, i32 %2)` 中，`%0` 是第一个形参（数组/指针）。对调用 `flatten_matrix_op(arr, 2, 3, 15, f)` 原先生成了：

```llvm
%10 = load i32, ptr %0, align 4
%12 = call i32 @flatten_matrix_op(i32 %10, i32 2, i32 3, i32 15, i8 %11)
```

即把 **i32 %10**（数组第一个元素的值，如 85）当作第一个实参传入。而 `flatten_matrix_op` 的语义是第一个参数为 `int mat[]`（指针），其内部会做 `getelementptr inbounds i32, ptr %0, ...`。此时 `%0` 被错误地当成了 85，导致对非法地址访问，引发段错误。

### 为何 VisitLVal 中“数组名传地址”没生效？

在 `VisitLVal` 中已有逻辑：当 LVal 无下标且 **pointee 为 ArrayType** 时，直接 `temp_value_ = value`（传地址）。但**函数形参**在 LLVM 中通常表示为 **ptr to i32**（或 ptr to i8），而不是 ptr to [N x i32]。因此对“形参名”做 LookupVariable 得到的是“指向 i32 的指针”，`GetPointeeType(value)` 得到的是 **IntegerType**，不是 ArrayType，会走“对标量 load”分支，从而错误地传了首元素的值。

## 三、修正思路

在**收集函数调用实参**时，若**形参类型是指针**，则对应实参表达式应得到**地址**，不能对地址做 load。因此需要：

1. 在遍历实参表达式之前，先拿到被调函数的**形参类型列表**。
2. 对每个实参：若该位置的形参是指针类型，则置“本实参需要传指针”的标志，再对该表达式求值；在 **VisitLVal** 中，若该标志为真且 LVal 无下标（数组名或指针变量），则直接使用地址 `value`，**不**做 load。
3. 其余类型转换与调用生成逻辑不变。

这样，`flatten_matrix_op(arr, ...)` 中的 `arr` 会以 ptr 传入，与形参 `int mat[]` 的语义一致。

## 四、实现要点

### 4.1 新增标志（IRGenVisitor.h）

```cpp
/**
 * When true, the next LVal (array/ptr) used as function argument should pass
 * the address (for pointer parameters), not the loaded value.
 */
bool func_arg_want_pointer_ = false;
```

### 4.2 VisitFuncCall（IRGenVisitorExpr.cpp）

- 先取 `callee` 与 `param_types`（形参类型列表）。
- 不再统一 `func_r_params->Accept(*this)` 再收集 `call_args_`；改为**按实参下标循环**：
  - 对第 `i` 个实参：若 `i < param_types.size()` 且 `param_types[i]` 为 **PointerType**，则置 `func_arg_want_pointer_ = true`；
  - 对该实参表达式 `Accept(*this)`；
  - 置回 `func_arg_want_pointer_ = false`；
  - 将 `temp_value_` 加入 `call_args_`。
- 后续对 `call_args_` 做 `ConvertToTargetType` 与 `CreateCall` 的逻辑不变。

### 4.3 VisitLVal（IRGenVisitorExpr.cpp）

在“无下标 LVal、非 lval_mode”分支中增加：

- 若 **func_arg_want_pointer_** 为真，则 **temp_value_ = value**（传地址），直接 return，不做 load。
- 否则保持原逻辑：pointee 为 ArrayType 时传地址，否则 load。

这样既保留“局部数组名/全局数组名（pointee 为 ArrayType）传地址”的行为，又修正“形参名（pointee 为 i32/i8）在作为实参传给指针形参时被错误 load”的问题。

## 五、涉及文件

| 文件 | 修改内容 |
|------|----------|
| `include/IRGenVisitor.h` | 新增成员 `func_arg_want_pointer_` 及注释 |
| `src/IRGenVisitorExpr.cpp` | `VisitFuncCall` 按形参类型循环收集实参并设置标志；`VisitLVal` 在标志为真时传地址不 load |

## 六、与 type_conversion.md 的关系

- **docs/ai_collab_notes/type_conversion.md** 第五节“数组作为实参（传地址不 load）”描述的是：**符号表项类型为“指向数组的指针”**（pointee 为 ArrayType）时，数组名作为值使用直接传地址。该逻辑对**局部/全局数组**正确。
- 本修正针对的是**形参为指针**（LLVM 中为 ptr to 标量）时，**实参侧**在求值阶段不能按“标量”做 load，而要根据**形参类型**决定是否传指针。二者互补，共同保证“数组/指针实参传地址”的语义正确。

## 七、运行方式说明（test_llvm.sh）

本地脚本 `testcases/test_llvm.sh` 将编译器生成的 `llvm_ir.txt` 与 `runtime_io.c` 经 llvm-link 得到 `merged.ll` 后，建议使用 **clang** 将 `merged.ll` 编成可执行文件再运行，而不是用 **lli** 直接解释 `merged.ll`。原因：runtime 依赖 `@stdin`、`@printf`、`@__isoc99_scanf` 等 libc 符号，lli 不链接 libc，运行到 getint/getc 等会因符号未解析而段错误。用 clang 编译并链接后，行为与课程评测（如 SysY_Test_2024 的 run_eval.py）一致。

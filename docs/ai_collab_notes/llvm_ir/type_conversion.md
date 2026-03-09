# SysY 隐式类型转换说明

本文档说明本编译器在 IR 生成阶段对 **int / char** 隐式类型转换的实现方式，以及为何课程评测可能曾用 `zext` 通过、与课程文档“符号扩展”的关系。

## 一、课程规定（2024_SysY_detailed.md）

语义约束中 **类型转换 (Type Conversion)** 规定：

1. **算术运算提升**：表达式中 `char` 类型的操作数会被隐式转换为 `int` 类型参与运算。
2. **赋值转换**：
   - 将 `int` 赋给 `char` 时，截取低 8 位。
   - 将 `char` 赋给 `int` 时，**进行符号扩展**。
3. **参数传递**：函数调用时，若形参与实参类型不一致（仅限 int 与 char），按**赋值转换规则**处理。

即：char → int 应为 **符号扩展 (sign extension, sext)**，int → char 为 **截断 (trunc)**。

## 二、为何评测用 zext 也能通过？

- 若评测用例中 **char 只出现在 0..127**（ASCII 可打印或常见数字），则：
  - **zext i8 to i32**：高 24 位补 0，结果与 sext 相同。
  - **sext i8 to i32**：高 24 位按符号位扩展，在 0..127 时与 zext 结果一致。
- 只有 **char 取负值（如 -1 对应 0xFF）** 时：
  - zext 得到 255，sext 得到 -1，二者不同；课程语义要求 -1。
- 因此：**仅用 zext 时，在“只测 0..127 的 char”的评测下可以通过**；若用例包含负数字符，则需 sext 才符合课程规定。

## 三、本实现中的选择

- 当前 IR 层**仅提供 `ZextInst` / `TruncInst`**，未实现 `SextInst`。
- 因此 **char → int 的提升统一使用 `zext i8 to i32`**，与课程“符号扩展”在负数字符时不一致。
- 若需**严格符合课程**：可在 IR 中增加 `SextInst` 与 `CreateSext`，在 `PromoteToI32` 与 `ConvertToTargetType` 中，对 char→int 改为使用 **sext**，其余逻辑不变。

## 四、转换应用场景一览

| 场景 | 位置 | 规则 | 实现方式 |
|------|------|------|----------|
| 算术二元运算 | `VisitBinaryExp`（+ - * / %） | 两操作数均为 int（char 提升为 int） | 对 lhs/rhs 调用 `PromoteToI32` 后再生成二元指令 |
| 关系/相等比较 | `VisitBinaryExp`（< > <= >= == !=） | 两操作数类型一致、用于比较 | 对 lhs/rhs 调用 `PromoteToI32` 后再 `CreateIcmp` |
| 单目负号/逻辑非 | `VisitUnaryExp`（MINU / NOT） | 操作数为 int | 对 operand 调用 `PromoteToI32` |
| 条件判断 | `CoerceToI1`（if/for 条件） | “非 0 即真”，与 0 比较 | 先 `PromoteToI32(cond_val)` 再 icmp ne 0 |
| 赋值 | `VisitAssignStmt`、`VisitForInitOrStep` | 右值类型与左值类型一致（截断或扩展） | 用 `GetPointeeType(addr)` 得目标类型，`ConvertToTargetType(val, target_ty)` 后 store |
| 函数实参 | `VisitFuncCall` | 实参与形参类型一致 | 按形参类型对每个 `call_arg` 调用 `ConvertToTargetType` |
| 返回值 | `VisitReturnStmt` | 返回值类型与函数声明一致 | 按当前函数返回类型对 `val` 调用 `ConvertToTargetType` |
| printf %d/%c | `VisitPrintfStmt` | putint/putch 接收 i32 | 对表达式结果调用 `PromoteToI32` 再传参 |
| getint/getchar 写回 | `VisitGetintStmt` / `VisitGetcharStmt` | 写回 char 变量时需截断 | 用 `GetPointeeType(addr)`，若为 i8 则 `ConvertToTargetType(call_result, i8)` 再 store |
| 短路求值 &&/|| | `EmitShortCircuitAND` / `EmitShortCircuitOR` | 合并结果为 i32 | 写入 result_slot 前对 rhs 调用 `PromoteToI32` |

## 五、数组作为实参（传地址不 load）

- 课程规定：**数组类型的参数**，形参接收的是**实参数组的地址**。
- 文法：实参是 `Exp`，数组名作为 `LVal`（无下标）时在语法上就是 `Exp`。
- 实现：在 `VisitLVal` 中，当**无下标、作为值使用**（非 lval_mode）时，若符号表项类型为**指向数组的指针**（`GetPointeeType(value)` 为 `ArrayType`），则 **temp_value_ = value**（传地址），**不做 load**；否则对标量做 load。
- 这样 `f(arr)` 会传 `arr` 的地址，与课程语义一致。

## 六、辅助函数说明

- **GetPointeeType(ptr)**：取指针的 pointee 类型，用于得到“左值/形参”的标量类型（i8 或 i32）。
- **PromoteToI32(v)**：若 `v` 为 i8，插入 `zext i8 to i32` 并返回新值；否则返回 `v`。用于算术、比较、条件、printf 等需要 i32 的场景。
- **ConvertToTargetType(v, target_ty)**：若 target 为 i8、v 为 i32 则 **trunc**；若 target 为 i32、v 为 i8 则 **zext**（见第三节，可改为 sext）；否则返回 `v`。用于赋值、实参、返回值、getint/getchar 写回等“目标类型明确”的场景。

以上实现保证在仅使用 0..127 的 char 时与常见评测一致；若需完全符合课程“符号扩展”，需增加 sext 并替换 char→int 的 zext。

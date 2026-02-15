# SysY 编译器 LLVM IR 指令总结

本文档根据课程文档（2024_SysY 文法、LLVM 课程指南、代码生成要求）整理：**当前 llvm_ir 阶段所需指令的分类、格式、生成时机与逻辑规则**。不包含具体实现代码，仅作设计与实现参考。

---

## 一、指令总览与分类

| 类别 | 指令 | 用途简述 |
|------|------|----------|
| 模块级 | 全局变量/常量、函数声明、函数定义 | 编译单元顶层结构 |
| 内存 | `alloca`、`load`、`store` | 局部/参数存储与读写 |
| 算术 | `add`、`sub`、`mul`、`sdiv`、`srem` | 加减乘除模（有符号） |
| 比较 | `icmp`（slt/sgt/sle/sge/eq/ne） | 关系与相等比较 |
| 单目 | 用 `sub`/`icmp` 表示 `-`、`!` | 负号、逻辑非 |
| 控制流 | `br`、`ret` | 分支与返回 |
| 调用 | `call` | 函数调用与 I/O |
| 地址 | `getelementptr` | 数组元素地址计算 |

---

## 二、模块级结构（非“指令”，但是 IR 顶层形式）

### 2.1 全局变量 / 全局常量

- **格式（变量）**：`@名字 = dso_local global i32 初值, align 4`（无初值时全局变量置 0）。  
  若为数组：`@名字 = dso_local global [N x i32] 初值列表, align 4`。  
  **例**：`int a = 10;` → `@a = dso_local global i32 10, align 4`；`int arr[3] = {1, 2, 3};` → `@arr = dso_local global [3 x i32] [i32 1, i32 2, i32 3], align 4`。
- **格式（常量）**：用 `constant` 替代 `global`，且必须有编译期可求值的初值。  
  **例**：`const int x = 42;` → `@x = dso_local constant i32 42, align 4`。
- **生成时机（文法/AST）**：  
  - `CompUnit` → `Decl`（`ConstDecl` / `VarDecl`）在**函数外**时，对应全局常量/全局变量。  
  - `ConstDef` / `VarDef` 在编译单元顶层，且无 `[ ConstExp ]` 时为标量；有 `[ ConstExp ]` 时为一维数组。

### 2.2 函数声明（外部 I/O）

- **格式**：  
  - `declare i32 @getint()`  
  - `declare i32 @getchar()`  
  - `declare void @putint(i32)`  
  - `declare void @putch(i32)`  
  - `declare void @putstr(i8*)`  
  未用到的可省略。  
  **例**：若程序里用了 `getint()` 和 `printf("%d", x)`，则在模块头部写：`declare i32 @getint()` 与 `declare void @putint(i32)` 即可。
- **生成时机**：在输出 LLVM IR 时，根据实际使用的 I/O（如将 `printf` 降级为 putint/putch/putstr）在模块头部显式声明。

### 2.3 函数定义

- **格式**：`define dso_local 返回类型 @函数名(形参列表) #属性 { 函数体 }`  
  - 返回类型：`void`、`i32`、`i8`（SysY 的 int/char）。  
  - 形参：普通参数为 `i32 %0`、`i8 %1` 等；数组形参为 `ptr`（或 `i32*`）类型。  
  **例**：`int main() { return 0; }` → `define dso_local i32 @main() #0 { ... ret i32 0 }`；`int add(int a, int b) { return a+b; }` → `define dso_local i32 @add(i32 %0, i32 %1) #0 { ... }`。
- **生成时机（文法/AST）**：  
  - `FuncDef`（普通函数）、`MainFuncDef`（`int main()`）对应一个 `define`。  
  - `FuncType` 决定返回类型；`FuncFParams` / 无参 决定形参列表。

---

## 三、函数内指令：格式与生成时机

### 3.1 alloca（分配局部/形参存储）

- **格式**：  
  - 标量：`%寄存器 = alloca i32, align 4`（或 `i8`）。  
  - 一维数组：`%寄存器 = alloca [N x i32], align 4`（或 `[N x i8]`）。  
  - 形参（数组）：函数内为数组形参分配一个指针存储时，可为 `ptr` 的 alloca。  
  **例**：`int x;` → `%1 = alloca i32, align 4`；`int arr[5];` → `%2 = alloca [5 x i32], align 4`；形参 `int a` 在入口处常先 `%3 = alloca i32, align 4`，再把实参 store 进去。
- **生成时机**：  
  - **局部变量/常量**：`BlockItem` → `Decl` → `VarDef` / `ConstDef` 在**函数内**时，每个变量或常量对应一个 alloca（常量也可在只读区，但局部通常用 alloca 简化实现）。  
  - **形参**：进入函数时，为每个 `FuncFParam` 分配 alloca，用于存放传入的值或指针（方便后续统一用 load/store 访问）。

**逻辑规则**：每个“在栈上的”局部变量/形参对应一次 alloca；数组按 `[N x 元素类型]` 分配；先分配形参再按声明顺序分配局部量。

---

### 3.2 store（写入内存）

- **格式**：`store i32 %值, ptr %指针, align 4`（类型可为 i8、i32 等，与 alloca/指针类型一致）。  
  **例**：`a = 3;`（a 对应 %1 的 alloca）→ `store i32 3, ptr %1, align 4`；形参初始化：`store i32 %0, ptr %2, align 4`（把第一个参数写入为形参分配的 %2）。
- **生成时机**：  
  - **赋值语句**：`Stmt` → `LVal '=' Exp ';'`（以及 `ForStmt` → `LVal '=' Exp`）。  
  - **形参初始化**：函数入口处，把 `FuncFParams` 的实参值（或数组指针）store 到对应形参的 alloca。  
  - **局部变量/常量带初值**：`VarDef` / `ConstDef` 带 `InitVal` / `ConstInitVal` 时，将求值结果 store 到该变量/常量的 alloca。  
  - **全局变量初值**：在模块级用 global 的初值列表表达，不在函数内 store（除非是运行时赋初值的实现方式）。

**逻辑规则**：左值为标量时，左值对应一个 alloca 或 GEP 结果，Exp 求值得到 `%值`，再 store 到该地址；左值为数组元素时，先对 LVal 做 GEP 得到地址，再 store。

---

### 3.3 load（从内存读取）

- **格式**：`%寄存器 = load i32, ptr %指针, align 4`（类型与指针元素类型一致）。  
  **例**：表达式里用到变量 `a`（a 的 alloca 为 %1）→ `%4 = load i32, ptr %1, align 4`，后续用 %4 参与运算；数组元素 `arr[i]` 先 GEP 得地址 %5，再 `%6 = load i32, ptr %5, align 4`。
- **生成时机**：  
  - **左值作为值使用**：当 `LVal` 出现在需要“值”的语境（表达式、return 的 Exp、实参等）时。  
  - 对应文法：`PrimaryExp` → `LVal`、`Exp` 中任何使用变量/数组元素值的地方。  
  - **数组元素**：先通过 `getelementptr` 得到元素地址，再对该地址 load。

**逻辑规则**：凡是“读变量/读数组元素”且需要得到一个 SSA 值的地方，对对应地址做一次 load；同一表达式内可复用同一 load 结果（按需优化）。

---

### 3.4 算术二元指令：add、sub、mul、sdiv、srem

- **格式**：  
  - `%结果 = add nsw i32 %左, %右`  
  - `%结果 = sub nsw i32 %左, %右`  
  - `%结果 = mul nsw i32 %左, %右`  
  - `%结果 = sdiv i32 %左, %右`  
  - `%结果 = srem i32 %左, %右`  
  操作数通常为 i32（char 在运算时按语义提升为 int，即 i32）。  
  **例**：`a + b`（已 load 为 %1, %2）→ `%3 = add nsw i32 %1, %2`；`x * y` → `%4 = mul nsw i32 %x, %y`；`p % q` → `%5 = srem i32 %p, %q`。
- **生成时机（文法/AST）**：  
  - **AddExp**：`AddExp ('+' | '-') MulExp` → 对 `+` 生成 `add`，对 `-` 生成 `sub`。  
  - **MulExp**：`MulExp ('*' | '/' | '%') UnaryExp` → 对 `*` 生成 `mul`，对 `/` 生成 `sdiv`，对 `%` 生成 `srem`。  
  - 递归下降时：先对子表达式求值得到 `%左`、`%右`，再生成对应一条二元指令，结果作为当前表达式的值。

**逻辑规则**：严格按文法优先级与结合性（先 MulExp 再 AddExp）；类型一致为 i32；若源语言有 char 参与运算，先提升再运算。

---

### 3.5 比较指令：icmp

- **格式**：`%结果 = icmp 谓词 i32 %左, %右`  
  谓词与 SysY 对应：  
  - `<` → `slt`；`>` → `sgt`；`<=` → `sle`；`>=` → `sge`；`==` → `eq`；`!=` → `ne`。  
  结果为 i1，在条件分支中作为 `br i1 %结果, ...` 的条件。  
  **例**：`a < b` → `%1 = icmp slt i32 %a, %b`；`x == 0` → `%2 = icmp eq i32 %x, 0`；在 if 中接着 `br i1 %1, label %then, label %else`。
- **生成时机（文法/AST）**：  
  - **RelExp**：`RelExp ('<' | '>' | '<=' | '>=') AddExp` → 生成对应 `icmp`，结果为 i1。  
  - **EqExp**：`EqExp ('==' | '!=') RelExp` → 生成 `icmp eq` / `icmp ne`。  
  - **Cond** 中用于 if/for 条件：Cond → LOrExp，最终会得到 i1（可能经过逻辑与/或的短路求值），用于 `br i1 ...`。

**逻辑规则**：关系/相等表达式只产生 i1，不直接产生 0/1 整数；若语义要求“真为 1、假为 0”，可在需要处用 `zext i1 to i32` 或通过 phi/select 赋 0/1。

---

### 3.6 单目运算：负号 `-`、逻辑非 `!`

- **负号（UnaryOp `-`）**：  
  - 语义：`0 - 操作数`。  
  - 格式/实现：`%结果 = sub nsw i32 0, %操作数`。  
  - **生成时机**：`UnaryExp` → `UnaryOp UnaryExp`，当 `UnaryOp` 为 `-` 时。  
  **例**：`-x`（x 已求值为 %1）→ `%2 = sub nsw i32 0, %1`。
- **逻辑非（UnaryOp `!`）**：  
  - 语义：仅出现在 **Cond**（条件表达式）中，非 0 为真、0 为假；结果为真/假。  
  - 格式/实现：`%结果 = icmp eq i32 %操作数, 0`，得到 i1（真表示“原值为假”）。若后续需要 0/1 整数，再 zext 或等价转换。  
  - **生成时机**：`Cond` → `LOrExp` → … → `UnaryExp` → `'!' UnaryExp`。  
  **例**：`if (!flag)`（flag 值为 %1）→ `%2 = icmp eq i32 %1, 0`，再 `br i1 %2, label %then, label %else`（%2 为真表示 flag 为 0，即“取非”为真）。

**逻辑规则**：`+` 单目不生成指令，直接传递子表达式值；`-` 和 `!` 各生成一条指令，操作数先递归求值。

---

### 3.7 ret（返回）

- **格式**：  
  - 有返回值：`ret i32 %值`（或 `ret i8 %值`）。  
  - 无返回值：`ret void`。  
  **例**：`return 0;` → `ret i32 0`；`return a + b;`（结果在 %1）→ `ret i32 %1`；void 函数 `return;` → `ret void`。
- **生成时机（文法/AST）**：  
  - `Stmt` → `'return' [Exp] ';'`。  
  - 有 `Exp` 时：先对 Exp 求值得到 `%值`，再 `ret 类型 %值`。  
  - 无 `Exp` 时（void 函数）：`ret void`。

**逻辑规则**：每个 return 语句对应一条 ret；函数末尾应保证所有路径都有 ret（语义约束：有返回值函数每分支都有带 Exp 的 return）。

---

### 3.8 br（分支）

- **格式**：  
  - 无条件：`br label %目标块`。  
  - 条件分支：`br i1 %条件, label %真分支, label %假分支`。  
  **例**：if 结尾跳转到合并块 → `br label %merge`；if 条件判断 → `br i1 %cond, label %if.then, label %if.else`；for 步进后跳回条件块 → `br label %for.cond`；`break;` → `br label %for.end`。
- **生成时机（文法/AST）**：  
  - **if**：`'if' '(' Cond ')' Stmt [ 'else' Stmt ]`。  
  - 对 Cond 求值得到 i1，生成 `br i1 %条件, label %then, label %else`（无 else 时假分支指向 if 后继块）。  
  - **for**：`'for' '(' [ForStmt] ';' [Cond] ';' [ForStmt] ')' Stmt`。  
  - 需要四个块：条件判断、循环体、步进、后继。Cond 缺省时视为恒真；生成条件 br 与无条件 br 连接各块。  
  - **break/continue**：对应无条件 `br` 到循环后继块（break）或步进/条件块（continue）。  
  - 块末尾：当前 BasicBlock 结束时，若后继唯一则 `br label %后继`。

**逻辑规则**：以 BasicBlock 为单位；每个分支点一条 br；条件仅来自 Cond（i1）；循环内 break/continue 只允许在语义允许的循环块内使用。

---

### 3.9 call（函数调用）

- **格式**：  
  - 有返回值：`%寄存器 = call i32 @函数名(i32 %0, i32 %1, ...)`（类型与实参一致）。  
  - 无返回值：`call void @函数名(...)`。  
  - 实参为数组时传指针：传 `ptr`（或 GEP 得到的指针）。  
  **例**：`getint()` → `%1 = call i32 @getint()`；`putint(n);` → `call void @putint(i32 %n)`；`add(1, 2)` → `%2 = call i32 @add(i32 1, i32 2)`；传数组 `foo(arr)`（arr 首址 %arr）→ `call void @foo(ptr %arr)`。
- **生成时机（文法/AST）**：  
  - **普通函数调用**：`UnaryExp` → `Ident '(' [FuncRParams] ')'`。  
  - 先对每个实参 `Exp` 求值，再按形参顺序排列，生成一条 call；若函数返回非 void，结果存到一个 SSA 寄存器。  
  - **I/O 语句**：  
  - `LVal '=' getint() ';'` → 生成 `%t = call i32 @getint()`，再 store 到 LVal。  
  - `LVal '=' getchar() ';'` → 生成 `%t = call i32 @getchar()`，再 store 到 LVal。  
  - `printf '(' StringConst { ',' Exp } ')' ';'` → 根据格式串与 Exp 列表，拆成多次 `call void @putint(i32)` / `call void @putch(i32)` / `call void @putstr(i8*)`（或等价的声明），并保证格式字符与表达式个数匹配。

**逻辑规则**：实参个数、类型与形参一致；数组按指针传递；返回值仅在有返回类型时使用；printf 在 IR 层降级为 putint/putch/putstr。

---

### 3.10 getelementptr（数组元素地址）

- **格式**：  
  - 局部/全局数组：`%ptr = getelementptr inbounds [N x i32], ptr %数组基址, i32 0, i32 %下标`（下标可为常量或 SSA 值）。  
  - 形参数组（ptr 已是一维指针）：`%ptr = getelementptr inbounds i32, ptr %形参, i32 %下标`。  
  得到的是元素指针，后续用 load/store 访问。  
  **例**：局部 `arr[i]`（arr 为 %1，i 为 %2）→ `%3 = getelementptr inbounds [10 x i32], ptr %1, i32 0, i32 %2`，再 `%4 = load i32, ptr %3, align 4`；形参 `int a[]` 访问 `a[k]` → `%5 = getelementptr inbounds i32, ptr %0, i32 %k`。
- **生成时机（文法/AST）**：  
  - **LVal** → `Ident '[' Exp ']'`（数组元素）：先确定 `Ident` 对应的基址（alloca 或全局变量或形参），再对 `Exp` 求值得到下标，生成 getelementptr，得到地址用于 load（取值）或 store（赋值）。  
  - **实参为数组**：传参时传的是“数组首地址”，即全局名、alloca 结果或 GEP 基址；若需传某个元素地址，则先 GEP 再作为实参。

**逻辑规则**：一维数组：基址 + 一个下标；类型与声明一致（i32 或 i8）；inbounds 便于优化；下标从 0 开始与语义一致。

---

## 四、与文法/AST 的对应关系简表

| 文法 / AST 概念 | 生成的 IR 形式 |
|-----------------|----------------|
| CompUnit 顶层 ConstDecl / VarDecl | 全局 constant / global（标量或 [N x 类型]） |
| FuncDef / MainFuncDef | define + 函数体 |
| Block 内 VarDef / ConstDef（局部） | alloca；若有初值则 store |
| FuncFParam | alloca（或仅用参数寄存器），入口 store 实参 |
| LVal '=' Exp | 若 LVal 为数组元素则 GEP，再 store Exp 的结果 |
| PrimaryExp → LVal（作为值） | 若为数组元素则 GEP，再 load |
| AddExp (+ / -) | add / sub |
| MulExp (* / / / %) | mul / sdiv / srem |
| RelExp (< / > / <= / >=) | icmp slt/sgt/sle/sge |
| EqExp (== / !=) | icmp eq/ne |
| UnaryOp '-' | sub nsw i32 0, %操作数 |
| UnaryOp '!'（仅 Cond） | icmp eq i32 %操作数, 0（得 i1） |
| return Exp ; / return ; | ret 类型 %值 / ret void |
| if (Cond) Stmt [ else Stmt ] | br i1 + 多个块 |
| for ( ; Cond ; ) Stmt | 条件块 + 体块 + 步进块 + br |
| break ; / continue ; | br 到对应块 |
| UnaryExp → Ident ( FuncRParams ) | call；数组实参传 ptr |
| LVal = getint() / getchar() | call + store |
| printf ( StringConst , Exp... ) | 拆成 putint/putch/putstr 的 call |

---

## 五、类型与约定（简要）

- **标量类型**：`i32`（int）、`i8`（char）；条件为 `i1`。  
- **数组**：`[N x i32]` 或 `[N x i8]`；数组名/形参在 IR 中常以 `ptr`（指向首元素）表示。  
- **算术**：统一用有符号指令（add nsw, sdiv, srem, icmp slt/sgt/sle/sge）。  
- **对齐**：简单实现中常用 `align 4`（i32）或 `align 1`（i8）。  
- **评测**：LLVM 版本 12.0.0；需在 IR 头部声明用到的 getint/getchar/putint/putch/putstr。

---

## 六、控制流与 SSA（逻辑规则，不涉及实现）

- 每个函数由若干 **BasicBlock** 组成，块内顺序执行，块间由 **br**（及 **ret**）连接。  
- 同一 BasicBlock 内，每条指令产生一个 SSA 值（或为 void），只使用其前驱指令或前面块经 phi 合并的值。  
- **if/for** 会产生多个块；条件为 i1；循环中 break/continue 对应跳到固定块，不能出现在非循环块中（语义约束）。  
- 若某变量在多个分支中被赋值且在后续被使用，需要在合并点用 **phi** 合并不同前驱的值；简单实现可先为每个赋值生成新“虚拟寄存器”，在合并块用 phi 选择。

以上内容均从课程文档与 SysY 文法推导而来，可作为 llvm_ir 阶段设计与实现的参考，不包含具体代码实现。

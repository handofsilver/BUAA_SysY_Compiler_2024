# SysY 文法最小样例集

本文档根据 `docs/course_info/2024_SysY_grammar.md` 与 `2024_SysY_detailed.md` 整理：每条样例只包含最少的语法现象，便于肉眼理解 LLVM IR 与 MIPS 生成结果；整个集合覆盖文法中所有需要覆盖的语法规则。

**使用方式**：可将每个样例保存为独立 `.sy` 文件，用本编译器生成 `llvm_ir.txt` 与 `mips.txt`，用于对照学习或回归测试。

---

## 一、程序结构 (CompUnit)

### 1.1 仅主函数（无全局声明、无其他函数）

```c
int main() {
    return 0;
}
```

**覆盖**：`CompUnit → {Decl} {FuncDef} MainFuncDef`（Decl 与 FuncDef 均为 0 次）、`MainFuncDef`、空 `Block`。

---

### 1.2 仅有全局声明 + 主函数

```c
int x = 1;
int main() {
    return 0;
}
```

**覆盖**：`CompUnit` 中存在 `Decl`（VarDecl）。

---

### 1.3 仅有其他函数 + 主函数

```c
int f() { return 1; }
int main() {
    return 0;
}
```

**覆盖**：`CompUnit` 中存在 `FuncDef`。

---

## 二、声明与定义 (Decl / ConstDecl / VarDecl)

### 2.1 常量声明（单个 ConstDef，花括号重复 0 次）

```c
int main() {
    const int a = 10;
    return 0;
}
```

**覆盖**：`Decl → ConstDecl`，`ConstDecl` 中 `ConstDef` 仅一个。

---

### 2.2 常量声明（多个 ConstDef，花括号重复多次）

```c
int main() {
    const int a = 1, b = 2, c = 3;
    return 0;
}
```

**覆盖**：`ConstDecl` 中 `ConstDef { ',' ConstDef }` 重复多次。

---

### 2.3 变量声明（单个 VarDef，花括号重复 0 次）

```c
int main() {
    int x;
    return 0;
}
```

**覆盖**：`Decl → VarDecl`，`VarDecl` 中 `VarDef` 仅一个。

---

### 2.4 变量声明（多个 VarDef，花括号重复多次）

```c
int main() {
    int a, b, c = 3;
    return 0;
}
```

**覆盖**：`VarDecl` 中 `VarDef { ',' VarDef }` 重复多次。

---

### 2.5 基本类型 char

```c
int main() {
    char c = 'a';
    return 0;
}
```

**覆盖**：`BType → 'char'`（常量/变量中均可，此处用变量）。

---

## 三、常量定义 (ConstDef / ConstInitVal)

### 3.1 常量标量 + 常表达式初值

```c
int main() {
    const int n = 5;
    return 0;
}
```

**覆盖**：`ConstDef → Ident '=' ConstInitVal`，`ConstInitVal → ConstExp`。

---

### 3.2 常量一维数组 + 列表初值

```c
int main() {
    const int arr[3] = {1, 2, 3};
    return 0;
}
```

**覆盖**：`ConstDef → Ident '[' ConstExp ']' '=' ConstInitVal`，`ConstInitVal → '{' ConstExp { ',' ConstExp } '}'`。

---

### 3.3 常量初值 StringConst（char 数组）

```c
int main() {
    const char s[6] = "hello";
    return 0;
}
```

**覆盖**：`ConstInitVal → StringConst`（仅用于 char 数组）。

---

## 四、变量定义 (VarDef / InitVal)

### 4.1 变量无初值

```c
int main() {
    int x;
    return 0;
}
```

**覆盖**：`VarDef → Ident`（无 `'=' InitVal`）。

---

### 4.2 变量有初值（表达式）

```c
int main() {
    int x = 1 + 2;
    return 0;
}
```

**覆盖**：`VarDef → Ident '=' InitVal`，`InitVal → Exp`。

---

### 4.3 一维数组无初值

```c
int main() {
    int arr[3];
    return 0;
}
```

**覆盖**：`VarDef → Ident '[' ConstExp ']'`（无初值）。

---

### 4.4 一维数组有初值（列表）

```c
int main() {
    int arr[3] = {10, 20, 30};
    return 0;
}
```

**覆盖**：`VarDef → Ident '[' ConstExp ']' '=' InitVal`，`InitVal → '{' Exp { ',' Exp } '}'`。

---

### 4.5 变量初值 StringConst（char 数组）

```c
int main() {
    char s[6] = "world";
    return 0;
}
```

**覆盖**：`InitVal → StringConst`。

---

## 五、函数 (FuncDef / FuncType / FuncFParams / FuncFParam)

### 5.1 无形参函数

```c
int f() {
    return 1;
}
int main() {
    return 0;
}
```

**覆盖**：`FuncDef → FuncType Ident '(' ')' Block`（`[FuncFParams]` 为空），`FuncType → 'int'`。

---

### 5.2 有形参函数（单形参）

```c
int f(int x) {
    return x;
}
int main() {
    return 0;
}
```

**覆盖**：`FuncDef` 中 `[FuncFParams]` 存在，`FuncFParam → BType Ident`（标量）。

---

### 5.3 函数类型 void

```c
void v() {
    return;
}
int main() {
    return 0;
}
```

**覆盖**：`FuncType → 'void'`，`return` 无 `Exp`。

---

### 5.4 函数类型 char

```c
char c() {
    return 'x';
}
int main() {
    return 0;
}
```

**覆盖**：`FuncType → 'char'`。

---

### 5.5 多形参（FuncFParams 重复多次）

```c
int add(int a, int b) {
    return a + b;
}
int main() {
    return 0;
}
```

**覆盖**：`FuncFParams → FuncFParam { ',' FuncFParam }` 中花括号重复多次。

---

### 5.6 数组形参

```c
int first(int a[]) {
    return a[0];
}
int main() {
    int arr[2] = {1, 2};
    return first(arr);
}
```

**覆盖**：`FuncFParam → BType Ident '[' ']'`，数组传参（实参为数组名）。

---

## 六、语句块 (Block / BlockItem)

### 6.1 空语句块（BlockItem 重复 0 次）

```c
int main() {
    {}
    return 0;
}
```

**覆盖**：`Block → '{' { BlockItem } '}'` 中花括号内重复 0 次。

---

### 6.2 语句块中含声明 (BlockItem → Decl)

```c
int main() {
    {
        int x = 0;
    }
    return 0;
}
```

**覆盖**：`BlockItem → Decl`，以及 Block 内有多条 BlockItem 时含 Decl。

---

### 6.3 语句块中含语句 (BlockItem → Stmt)

```c
int main() {
    {
        return 0;
    }
}
```

**覆盖**：`BlockItem → Stmt`（此处 Stmt 为 `return 0;`）。

---

## 七、语句 (Stmt)

### 7.1 赋值语句

```c
int main() {
    int x;
    x = 42;
    return 0;
}
```

**覆盖**：`Stmt → LVal '=' Exp ';'`。

---

### 7.2 表达式语句（有 Exp）

```c
int main() {
    1 + 2;
    return 0;
}
```

**覆盖**：`Stmt → [Exp] ';'` 且 Exp 存在（求值后丢弃）。

---

### 7.3 表达式语句（无 Exp，空语句）

```c
int main() {
    ;
    return 0;
}
```

**覆盖**：`Stmt → [Exp] ';'` 且 Exp 缺省。

---

### 7.4 语句块作为语句

```c
int main() {
    int x = 0;
    {
        x = 1;
    }
    return 0;
}
```

**覆盖**：`Stmt → Block`。

---

### 7.5 if 无 else

```c
int main() {
    int x = 1;
    if (x) x = 0;
    return 0;
}
```

**覆盖**：`Stmt → 'if' '(' Cond ')' Stmt`（无 `'else' Stmt`）。

---

### 7.6 if-else

```c
int main() {
    int x = 1;
    if (x) x = 0; else x = 2;
    return 0;
}
```

**覆盖**：`Stmt → 'if' '(' Cond ')' Stmt 'else' Stmt`。

---

### 7.7 for 完整（无缺省）

```c
int main() {
    int i;
    int s = 0;
    for (i = 0; i < 3; i = i + 1) s = s + 1;
    return 0;
}
```

**覆盖**：`Stmt → 'for' '(' ForStmt ';' Cond ';' ForStmt ')' Stmt`，三个部分均存在。

---

### 7.8 for 缺省其一（无 Cond，等价 while(1) 需 break 退出）

```c
int main() {
    int i = 0;
    for (i = 0; ; i = i + 1) {
        if (i >= 1) break;
    }
    return 0;
}
```

**覆盖**：`for` 中 `[Cond]` 缺省。

---

### 7.9 for 缺省两个（仅 Cond）

```c
int main() {
    int i = 0;
    for (; i < 1; ) {
        i = i + 1;
    }
    return 0;
}
```

**覆盖**：`for` 中第一个 `[ForStmt]` 与第三个 `[ForStmt]` 缺省。

---

### 7.10 for 全部缺省（死循环，用 break 退出）

```c
int main() {
    int n = 0;
    for (;;) {
        n = n + 1;
        if (n >= 1) break;
    }
    return 0;
}
```

**覆盖**：`for` 中 `[ForStmt]`、`[Cond]`、`[ForStmt]` 全部缺省。

---

### 7.11 break

```c
int main() {
    int i;
    for (i = 0; i < 10; i = i + 1) {
        if (i >= 1) break;
    }
    return 0;
}
```

**覆盖**：`Stmt → 'break' ';'`（必须在循环内）。

---

### 7.12 continue

```c
int main() {
    int i;
    for (i = 0; i < 3; i = i + 1) {
        if (i == 1) continue;
    }
    return 0;
}
```

**覆盖**：`Stmt → 'continue' ';'`（必须在循环内）。

---

### 7.13 return 带表达式

```c
int f() {
    return 42;
}
int main() {
    return 0;
}
```

**覆盖**：`Stmt → 'return' Exp ';'`。

---

### 7.14 return 无表达式（void）

已在 5.3 中覆盖：`Stmt → 'return' ';'`。

---

### 7.15 getint

```c
int main() {
    int x;
    x = getint();
    return 0;
}
```

**覆盖**：`Stmt → LVal '=' 'getint' '(' ')' ';'`。

---

### 7.16 getchar

```c
int main() {
    int x;
    x = getchar();
    return 0;
}
```

**覆盖**：`Stmt → LVal '=' 'getchar' '(' ')' ';'`。

---

### 7.17 printf 无表达式

```c
int main() {
    printf("hello\n");
    return 0;
}
```

**覆盖**：`Stmt → 'printf' '(' StringConst ')' ';'`（`{ ',' Exp }` 重复 0 次）。

---

### 7.18 printf 带表达式

```c
int main() {
    int x = 42;
    printf("%d", x);
    return 0;
}
```

**覆盖**：`Stmt → 'printf' '(' StringConst { ',' Exp } ')' ';'`（含至少一个 Exp）。

---

## 八、左值 (LVal)

### 8.1 普通变量（Ident）

```c
int main() {
    int a = 1;
    a = 2;
    return 0;
}
```

**覆盖**：`LVal → Ident`（无 `'[' Exp ']'`）。

---

### 8.2 数组元素

```c
int main() {
    int arr[3] = {1, 2, 3};
    arr[1] = 10;
    return arr[0];
}
```

**覆盖**：`LVal → Ident '[' Exp ']'`（一维数组下标访问）。

---

## 九、基本表达式 (PrimaryExp)

### 9.1 括号表达式

```c
int main() {
    int x = (1 + 2) * 3;
    return 0;
}
```

**覆盖**：`PrimaryExp → '(' Exp ')'`。

---

### 9.2 PrimaryExp → LVal、Number、Character

已在多处覆盖：LVal（变量/数组元素）、Number（如 `1`、`42`）、Character（如 `'a'`）。

---

## 十、一元表达式与函数调用 (UnaryExp / UnaryOp / FuncRParams)

### 10.1 函数调用无实参

```c
int f() {
    return 1;
}
int main() {
    int x = f();
    return 0;
}
```

**覆盖**：`UnaryExp → Ident '(' ')'`，`FuncRParams` 缺省（实参 0 个）。

---

### 10.2 函数调用有实参（多个）

```c
int add(int a, int b) {
    return a + b;
}
int main() {
    int x = add(1, 2);
    return 0;
}
```

**覆盖**：`UnaryExp → Ident '(' FuncRParams ')'`，`FuncRParams → Exp { ',' Exp }` 重复多次。

---

### 10.3 单目运算符 '+'

```c
int main() {
    int x = 1;
    int y = +x;
    return 0;
}
```

**覆盖**：`UnaryOp → '+'`，`UnaryExp → UnaryOp UnaryExp`。

---

### 10.4 单目运算符 '-'

```c
int main() {
    int x = 5;
    int y = -x;
    return 0;
}
```

**覆盖**：`UnaryOp → '-'`。

---

### 10.5 单目运算符 '!'（仅条件表达式）

```c
int main() {
    int x = 0;
    if (!x) x = 1;
    return 0;
}
```

**覆盖**：`UnaryOp → '!'`（出现在 `Cond → LOrExp` 中）。

---

### 10.6 数组做实参（传地址）

已在 5.6 中覆盖：`first(arr)`，实参为数组名，对应 `FuncRParams` 中 Exp 为数组传参。

---

## 十一、乘除模 (MulExp)

### 11.1 乘

```c
int main() {
    int x = 3 * 4;
    return 0;
}
```

**覆盖**：`MulExp → MulExp '*' UnaryExp`。

---

### 11.2 除

```c
int main() {
    int x = 8 / 2;
    return 0;
}
```

**覆盖**：`MulExp → MulExp '/' UnaryExp`。

---

### 11.3 模

```c
int main() {
    int x = 7 % 3;
    return 0;
}
```

**覆盖**：`MulExp → MulExp '%' UnaryExp`。

---

## 十二、加减 (AddExp)

### 12.1 加

```c
int main() {
    int x = 1 + 2;
    return 0;
}
```

**覆盖**：`AddExp → AddExp '+' MulExp`。

---

### 12.2 减

```c
int main() {
    int x = 10 - 3;
    return 0;
}
```

**覆盖**：`AddExp → AddExp '-' MulExp`。

---

## 十三、关系与相等 (RelExp / EqExp)

### 13.1 小于

```c
int main() {
    int x = (1 < 2);
    return 0;
}
```

**覆盖**：`RelExp → RelExp '<' AddExp`。

---

### 13.2 大于

```c
int main() {
    int x = (5 > 3);
    return 0;
}
```

**覆盖**：`RelExp → RelExp '>' AddExp`。

---

### 13.3 小于等于

```c
int main() {
    int x = (2 <= 2);
    return 0;
}
```

**覆盖**：`RelExp → RelExp '<=' AddExp`。

---

### 13.4 大于等于

```c
int main() {
    int x = (3 >= 2);
    return 0;
}
```

**覆盖**：`RelExp → RelExp '>=' AddExp`。

---

### 13.5 等于

```c
int main() {
    int x = (1 == 1);
    return 0;
}
```

**覆盖**：`EqExp → EqExp '==' RelExp`。

---

### 13.6 不等于

```c
int main() {
    int x = (1 != 2);
    return 0;
}
```

**覆盖**：`EqExp → EqExp '!=' RelExp`。

---

## 十四、逻辑与 / 或 (LAndExp / LOrExp)

### 14.1 逻辑与

```c
int main() {
    int x = 1;
    int y = 1;
    if (x && y) x = 0;
    return 0;
}
```

**覆盖**：`LAndExp → LAndExp '&&' EqExp`。

---

### 14.2 逻辑或

```c
int main() {
    int x = 0;
    int y = 1;
    if (x || y) x = 1;
    return 0;
}
```

**覆盖**：`LOrExp → LOrExp '||' LAndExp`。

---

## 十五、常量表达式 (ConstExp)

ConstExp 在数组长度、常量初值等处使用，且其中 Ident 必须为常量。下面用常量参与运算作为数组长度。

### 15.1 常量表达式（用于数组长度）

```c
int main() {
    const int n = 4;
    int arr[n];
    return 0;
}
```

**覆盖**：`ConstExp → AddExp`，且 ConstExp 中使用的 Ident 为常量（此处 `n` 为常量，用于 `ConstExp` 作为数组长度）。

---

## 十六、覆盖索引速查

| 文法规则 / 覆盖点 | 最小样例编号 |
|------------------|--------------|
| CompUnit 无 Decl 无 FuncDef | 1.1 |
| CompUnit 有 Decl | 1.2 |
| CompUnit 有 FuncDef | 1.3 |
| ConstDecl 单/多 ConstDef | 2.1, 2.2 |
| VarDecl 单/多 VarDef | 2.3, 2.4 |
| BType int/char | 2.5, 3.3, 4.5 等 |
| ConstDef 标量/一维数组 | 3.1, 3.2 |
| ConstInitVal ConstExp / 列表 / StringConst | 3.1, 3.2, 3.3 |
| VarDef 无初值/有初值/数组无初值/数组有初值 | 4.1–4.5 |
| InitVal Exp / 列表 / StringConst | 4.2, 4.4, 4.5 |
| FuncDef 无形参/有形参 | 5.1, 5.2 |
| FuncType void / int / char | 5.3, 5.1, 5.4 |
| FuncFParams 多形参、数组形参 | 5.5, 5.6 |
| Block 空/含 Decl/含 Stmt | 6.1–6.3 |
| BlockItem Decl / Stmt | 6.2, 6.3 |
| Stmt 赋值/表达式/空/Block/if/if-else/for/break/continue/return/getint/getchar/printf | 7.1–7.18 |
| for 无缺省/缺省 1/2/3 部分 | 7.7–7.10 |
| LVal Ident / Ident [Exp] | 8.1, 8.2 |
| PrimaryExp (Exp)/LVal/Number/Character | 9.1 及前文 |
| UnaryExp PrimaryExp/调用/UnaryOp | 10.1–10.5 |
| UnaryOp + / - / ! | 10.3–10.5 |
| FuncRParams 0 个/多个、数组实参 | 10.1, 10.2, 5.6 |
| MulExp * / / / % | 11.1–11.3 |
| AddExp + / - | 12.1, 12.2 |
| RelExp < / > / <= / >= | 13.1–13.4 |
| EqExp == / != | 13.5, 13.6 |
| LAndExp &&、LOrExp \|\| | 14.1, 14.2 |
| ConstExp（常量参与） | 15.1 |

---

*文档版本：根据 2024 SysY 文法与详细说明整理，便于 IR/MIPS 阶段对照与回归。*

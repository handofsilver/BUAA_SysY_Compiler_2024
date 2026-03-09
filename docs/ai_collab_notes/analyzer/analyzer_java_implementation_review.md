# Java 语义分析实现深度评估

**说明**：本评估由 Cursor 中的 Auto（Agent Router）完成，基于对您提供的 Java 源码的阅读与推理。

> 本文档对 BUAA_Compiler_2024_JAVA_GT 中「语义分析 + 符号表管理」的实现进行梳理与评估，说明关键环节的定义与应用，并系统指出设计缺陷，供 C++ 重构阶段参考。

---

## 一、关键环节的定义与应用

### 1.1 符号（Symbol）的定义与构造

**位置**：`semantics/Symbol.java`

符号采用**不可变**设计，所有字段 `final`，通过多个私有构造函数 + 静态工厂方法按种类创建，避免误用（如给函数符号传 `ConstInitVal`）。

**核心字段**：

| 字段 | 含义 |
|------|------|
| `type` | `SymbolType` 枚举（ConstInt/Int/IntFunc/…） |
| `name` | 标识符字符串 |
| `scopeId` | 作用域序号（与作业要求一致） |
| `arraySize` | 数组时非 null，为 `ConstExp`（常量表达式） |
| `funcFParams` | 仅函数符号有效，形参列表 `FuncFParams` |
| `constInitVal` / `initVal` | 常量/变量的初值（AST 节点） |

**工厂方法示例**：

```java
// 函数符号：仅 type/name/scopeId/funcFParams 有效
public static Symbol createFuncSymbol(SymbolType type, String name, int scopeId, FuncFParams funcFParams);

// 常量标量/数组、变量标量/数组：各自约束 type 后调用对应私有构造
public static Symbol createConstVarSymbol(...);
public static Symbol createArraySymbol(...);
// 等
```

**设计意图**：用静态工厂 + 私有构造保证「函数 / 常量 / 变量 / 数组」之间不会混用字段；类型名通过 `SymbolType.toString()` 与作业要求的 ConstInt/Int/IntFunc 等一致。

**问题摘要**：Symbol 直接依赖语法节点类型（`ConstExp`、`FuncFParams`、`ConstInitVal`、`InitVal`），导致语义层与语法 AST 强耦合，不利于后续中间代码或独立语义阶段使用。

---

### 1.2 符号表（SymbolTable）与作用域链

**位置**：`semantics/SymbolTable.java`

每个作用域对应一张 `SymbolTable`，通过**单向链表**组成作用域链（仅 `prevTable`，查找时沿链向上）。

**结构**：

```java
private final int id;                    // 作用域序号
private final SymbolTable prevTable;      // 外层作用域
private SymbolTable nextTable;            // 用于“复用”子作用域（见下）
private final HashMap<String, Symbol> dictionary;
```

- **注册**：`register(name, symbol)` → 仅当前表 `dictionary.put`，不查外层。
- **查找**：由 `SymbolManager.lookupSymbol(table, name)` 实现：本表有则返回，否则 `prevTable != null ? lookupSymbol(prevTable, name) : null`，即沿链向上、先内后外。

**设计意图**：用链表实现词法作用域；`nextTable` 用于在进入块时复用“已有”的子表（见 1.3 节），以节省对象。

**问题摘要**：`nextTable` 的语义与“进入/离开作用域”的栈式语义混在一起，且与全局的 `symbolLists` 序号对应关系容易出错，见后文缺陷分析。

---

### 1.3 作用域管理（SymbolManager）

**位置**：`core/SymbolManager.java`

**全局状态**：静态变量维护“当前作用域”和“作用域计数”：

```java
private static int scopeCount = 1;
private static SymbolTable curTable = new SymbolTable(scopeCount, null);  // 全局仅此一张表
```

**关键操作**：

1. **进入函数形参作用域**（`startAddingFuncFParams`）
   - `scopeCount++`
   - 新建 `SymbolTable(scopeCount, curTable)`，设为 `curTable.nextTable`，`curTable` 指向该新表
   - `Handler.addSymbolTable()`，为输出用 `symbolLists` 增加一个列表

2. **结束形参、回到全局**（`endAddingFuncFParams`）
   - `scopeCount--`
   - `popScope()`：`curTable = curTable.getPrevTable()`

3. **进入语句块**（`pushScope`）
   - `scopeCount++`
   - `Handler.addSymbolTable()`（先为“新作用域”在输出里占位）
   - 若当前表已有 `nextTable`，则断开 `curTable.setNextTable(null)` 并 `curTable = nextTable`（复用该子表）；
   - 否则新建 `SymbolTable(scopeCount, curTable)` 并再次 `Handler.addSymbolTable()`，再令 `curTable` 指向新表。

4. **离开语句块**（`popScope`）
   - `curTable = curTable.getPrevTable()`，不修改 `scopeCount`。

**符号注册**（`registerSymbol(symbol, lineNum)`）：

- 若 `curTable.exist(symbol.getName())` 则构造 `RedefinitionException`（其构造内会 `Handler.addErrorInfo`），然后 catch 掉，不向表里写入。
- 否则 `curTable.register(name, symbol)` 且 `Handler.addSymbol(symbol.getScopeId(), symbol)`，用于按作用域序号输出。

**符号使用/查找**（`useSymbol(name, lineNum)`）：

- `lookupSymbol(curTable, name)` 沿 `prevTable` 链查找。
- 若为 null 则构造 `UndefinedIdentifierException`（同样在构造内写 `Handler.addErrorInfo`），再 catch 掉。
- 无论是否找到都返回查到的 `Symbol`（可能为 null），供上层做类型检查等。

**设计意图**：
- 符号注册与错误收集通过“抛异常 + 立即 catch”触发，避免在 Parser 里到处写 `if (redefined) Handler.addErrorInfo(...)`。
- 块作用域通过“先占位 symbolLists，再视情况复用或新建 SymbolTable”试图既满足输出顺序又减少表对象数量。

**问题摘要**：
- 异常仅作“带副作用的信号”，违反“异常用于异常控制流”的常规用法，可读性差。
- `pushScope` 中 `scopeCount`、`symbolLists` 与“复用 nextTable”之间的对应关系复杂且易错（见 2.2）。
- 全静态、无实例的 SymbolManager 不利于测试和多线程，也与“一个编译单元一个符号管理器”的语义不符。

---

### 1.4 符号在 Parser 中的注册时机

**位置**：`core/Parser.java` 中各类 `parse*` 方法

- **常量/变量**：在 `parseConstDef` / `parseVarDef` / `parseFuncFParam` 中，在构造完对应 AST 节点后，根据 BType/是否数组/是否有初值 构造 `Symbol`，立即 `SymbolManager.registerSymbol(symbol, lineNum)`，行号来自 `lexer.getLineNum()`。
- **函数**：在 `parseFuncDef` 中，在解析完形参列表并 `checkRightParenthesisAndPass()` 之后、`parseBlock` 之前，用当前 `SymbolManager.getScopeId()` 和已解析的 `FuncFParams` 构造函数符号并 `registerSymbol`。

因此，**函数名注册在全局作用域（scopeId=1）**；**形参注册在“函数形参作用域”（先 `startAddingFuncFParams` 再解析各 `FuncFParam`）**；**块内变量在 `pushScope` 之后的块内**注册，符合「函数的参数属于函数内部的作用域」的题意。

**问题摘要**：
- 语义动作（注册、查表、报错）与语法分析写在同一方法里，没有单独的“语义访问阶段”，不利于扩展和错误恢复策略（如多错误收集）。
- 行号统一用“当前 token 行号”，对多行声明或右括号缺失等，可能与题目期望的“错误行号”有细微差异，取决于题目约定。

---

### 1.5 符号的使用与错误检查

**使用点**大致有三类：

1. **左值（赋值、getint/getchar、for 初/终）**
   - 通过 `parseLValAndCheckConst()`：先 `parseLVal()` 得到 AST，再 `SymbolManager.useSymbol(ident, lineNum)` 做未定义检查，并在 catch 中检查 `symbol.isConstType()` 报常量被赋值的错误（h 类）。

2. **表达式中的标识符（PrimaryExp → LVal）**
   - `parsePrimaryExp()` 在分支「LVal」里：`parseLVal()` 后 `SymbolManager.useSymbol(lVal.getIdent(), lineNum)`，只做未定义检查。

3. **函数调用（UnaryExp → Ident '(' ... ')'）**
   - 先 `SymbolManager.useSymbol(ident, lineNum)` 得到 `symbol`；
   - 再解析实参列表 `FuncRParams`；
   - 比较实参个数与 `symbol.getFuncFParams().getSize()`（d 类）；
   - 调用 `funcRParams.checkType(symbol.getFuncFParams())` 做实参/形参类型是否一致（e 类）。

**实参类型如何得到**：
在 `parseFuncRParams()` 中，每个实参在 `parseExp()` **之前** 调用 `lexer.GuessExpSymbolType()`：Lexer 暂存状态后，通过 `next()` 推进到该 Exp 的“首 token”（IDENFR/INTCON/CHRCON），根据该 token 及符号表推断类型（整数/字符/数组元素/函数返回值等），再恢复 `curPos/lineIndex/curToken`，返回类型。Parser 再用该类型列表与形参列表逐项比较（数组/非数组、int/char 等）。

**设计意图**：
- 在“解析实参列表”时就拿到每个实参的“表达式类型”，从而在 Parser 内完成 d/e 检查，无需单独一遍语义遍历。
- 未定义、常量赋值、break/continue 不在循环、void 中 return 值、缺 return 等，都在对应解析分支里用“抛异常 + catch 并忽略”的方式只写错误、不中断解析。

**问题摘要**：
- `GuessExpSymbolType()` 内部会调用 `SymbolManager.useSymbol()`，会产生“未定义”等错误并写入 `Handler`，但 Lexer 只恢复了自己的部分状态，未恢复 `Handler`；若同一表达式在后续 `parseExp()` 中再次查符号，可能重复报同一未定义错误。
- 类型推断依赖“当前 token 即表达式首 token”的约定，对复杂表达式只看了第一个 primary 的类型，与“表达式整体类型”在多数情况下一致，但把“类型推断”塞进 Lexer 破坏了“词法仅负责 token”的职责边界。

---

### 1.6 错误收集与输出（Handler）

**位置**：`core/Handler.java`

- **错误列表**：`ArrayList<ErrorInfo> errorList`；各异常类在构造时调用 `Handler.addErrorInfo(new ErrorInfo(ErrorCode.xxx, lineNum))`，不向外再抛（或抛了立刻被 Parser 里 catch 掉）。
- **符号输出**：`ArrayList<ArrayList<Symbol>> symbolLists`；每个作用域一个子列表，`addSymbolTable()` 增加一个子列表，`addSymbol(scopeId, symbol)` 向 `symbolLists.get(scopeId - 1)` 追加。
- **输出逻辑**：`print()` 若 `errorList` 非空则写 `error.txt`（行号 + 错误码），否则按 `symbolLists` 顺序写 `symbol.txt`（作用域序号、名字、类型名）。
- **回溯支持**：`save()` / `restore()` 将 `unitStack`、`errorList` 复制到/从 `save*` 恢复，用于 `parseStmtOther` 中“先试解析为 Exp，失败再按 LVal = …”的回退，避免误吞 token。

**设计意图**：
- 一处集中管理“错误列表”和“按作用域收集的符号”，便于统一输出格式。
- 通过 save/restore 在歧义分支中回退，保证错误列表和语法栈与“尝试分支”一致。

**问题摘要**：
- Handler 同时管“错误”“符号输出”“语法栈”“回溯”，职责过多；且与 SymbolManager、Parser、Lexer 静态耦合，难以单测和替换。
- 作业要求“可关闭符号输出”，当前实现没有开关，需改代码或通过配置控制。

---

## 二、设计缺陷的系统评估

### 2.1 架构与职责划分

- **语义与语法强耦合**：符号注册、查表、类型检查、错误码写入全部在 Parser 的 `parse*` 中完成，没有独立的“语义分析阶段”或 Visitor 遍历 AST。后果：难以做“多遍分析”“错误恢复”“仅语义检查不解析”，也不利于后续中间代码生成时复用同一套符号与类型信息。
- **Symbol 与 AST 强耦合**：Symbol 直接持有 `ConstExp`、`FuncFParams`、`ConstInitVal`、`InitVal` 等语法节点。若后续要做常量折叠、中间表示，需要从 Symbol 再反查 AST，或复制信息，不利于“语义信息自包含”。
- **全局静态状态**：SymbolManager、Handler 全静态，Parser、Lexer 通过静态方法与之交互。后果：无法为“一次编译”构造独立的符号管理与错误收集器，测试和并行编译不友好。
- **Lexer 承担类型推断**：`GuessExpSymbolType()` 在 Lexer 中实现，且内部调用 `SymbolManager.useSymbol()`，打破“词法只产 token”的边界，并引入重复报错与状态恢复不完整的问题（见 1.5、2.4）。

---

### 2.2 作用域与 symbolLists 的对应关系

- **pushScope 的两种路径**：
  - 路径 A：`nextTable != null` 时只做 `curTable = nextTable`，不新建表，但前面已经执行了 `scopeCount++` 和 `Handler.addSymbolTable()`，即多了一个“仅用于输出的空列表”，且该列表的索引与当前 `curTable.id` 不一定一致（当前 curTable 是复用的，id 可能是之前某次分配的）。
  - 路径 B：`nextTable == null` 时新建表并又调用一次 `Handler.addSymbolTable()`，导致**同一逻辑作用域对应两次** `addSymbolTable()`，即 symbolLists 中多出一个空槽位。
- **scopeCount 与 id 复用**：`endAddingFuncFParams` 里 `scopeCount--`，而 `popScope` 不修改 `scopeCount`。因此进入第二个函数时，`startAddingFuncFParams` 再次 `scopeCount++` 得到与第一个函数形参相同的 scopeId；新建的 `SymbolTable(scopeCount, curTable)` 会与之前某张表 id 相同。此时 `addSymbol(scopeId, symbol)` 会往**同一个** `symbolLists.get(scopeId-1)` 里追加，导致**不同函数的形参/函数体符号混在同一作用域序号下**，与“按作用域序号依次输出、同一作用域内按声明顺序”的题意不符，存在潜在错误或评测边界问题。
- **nextTable 的语义**：本意是“同一外层下多个并列块复用子表”，但和“作用域序号全局递增”的约定混在一起，实现复杂且易错；若改为严格的“进入块就压栈、建新表、分配新 scopeId”，逻辑会更清晰，输出与 symbolLists 一一对应。

---

### 2.3 异常使用方式

- **异常仅作“带副作用的信号”**：各语义错误在构造异常时即写入 `Handler.addErrorInfo(...)`，然后被上层 `catch (XxxException ignored) {}` 吞掉，返回值或后续逻辑仍按“无错误”继续。这种写法：
  - 把“错误报告”和“控制流”绑在一起，可读性差；
  - 难以区分“已处理错误”与“真正的异常”；
  - 不符合“异常表示异常情况”的常见用法。
- **建议**：语义错误用返回值或显式错误列表（如 `List<ErrorInfo> errors`）收集，由调用方统一 `addErrorInfo`，不通过异常构造的副作用写错误。

---

### 2.4 类型推断与重复报错

- **GuessExpSymbolType 的副作用**：在 `parseFuncRParams` 中先对每个实参调用 `lexer.GuessExpSymbolType()`，其内部会 `next()` 到该 Exp 的首 token，若为标识符则调用 `SymbolManager.useSymbol()`。若未定义，会在这里报一次错；之后 `parseExp()` 会再次解析同一表达式并再次 `useSymbol()`，可能再报一次同一行的未定义错误，导致**重复错误项**。
- **Lexer 状态恢复不完整**：`GuessExpSymbolType` 只恢复了 `curPos, lineIndex, curToken`，未恢复 `lastToken` 等，在严格意义上 Lexer 状态并非完全可重入；若将来在别处依赖 `lastToken`，可能出现隐蔽 bug。

---

### 2.5 其他实现细节

- **Parser 中 UnaryOp**：`parseUnaryOp()` 里先 `lexer.next()` 消费掉 `+`/`-`/`!`，再 `lexer.peek().value()` 取的是**消费后的当前 token**（即下一个 token）的 value 赋给 UnaryOp，因此运算符实际被错误地记成下一 token 的内容，属于实现 bug。
- **FuncRParams.checkType 逻辑**：数组与基类型匹配、char[]/int[] 与形参 bType 的对应关系依赖多处布尔组合，可读性一般，且缺少对 `params.size()` 与 `types.size()` 的显式断言，若长度不一致会越界。
- **main 不纳入符号表**：作业要求 main 不输出到 symbol.txt，当前实现未把 main 注册到符号表，因此不会出现在 symbolLists 中，满足要求；若后续中间代码需要 main 的入口信息，需在别处单独维护。

---

## 三、总结与对 C++ 重构的建议

- **关键环节**：符号用“类型 + 作用域 + 名字 + 种类相关字段”表达，通过工厂方法创建；符号表为按作用域链组织的 HashMap；作用域由 SymbolManager 的 push/pop 与“形参/块”的解析顺序配合；符号使用在 LVal、PrimaryExp、UnaryExp 等处查表并做未定义/常量赋值/函数实参检查；错误与符号输出集中在 Handler，通过异常构造的副作用写入错误列表。
- **主要设计缺陷**：语义与语法强耦合、Symbol 与 AST 强耦合、全局静态 SymbolManager/Handler、作用域序号与 symbolLists 对应关系复杂且存在 id 复用与重复 addSymbolTable、异常当“错误信号”使用、Lexer 做类型推断导致职责混乱与重复报错、以及部分实现细节（UnaryOp、checkType 边界）不够稳健。

**对 C++ 重构的建议**：
1. 将“符号表 + 作用域栈”做成独立模块（如 `SymbolTable`/`ScopeManager`），按“进入块/函数就压栈并分配新 scopeId”的清晰规则维护，避免 nextTable 复用与 symbolLists 错位。
2. 语义信息（类型、是否常量、形参列表等）与 AST 解耦，Symbol 只存语义层需要的内容，必要时通过 AST 节点指针/引用关联，而不是在 Symbol 里存整颗子树。
3. 采用“先解析得到 AST，再对 AST 做语义遍历（Visitor 或类似）”的两阶段结构，便于多错误收集、错误恢复和后续代码生成。
4. 错误收集通过显式列表或返回值，不在异常构造里写 Handler。
5. 表达式类型推断放在语义阶段、基于 AST 的表达式节点计算，而不是在 Lexer 里通过 lookahead + useSymbol 猜测。
6. 若需“按作用域序号 + 声明顺序”输出符号，可在语义遍历时按 scope 建列表，或由符号表模块直接提供“按序迭代”的接口，避免与作用域栈脱节的独立 symbolLists。

---

*文档基于对 BUAA_Compiler_2024_JAVA_GT 源码的阅读整理，重点为语义分析与符号表相关部分。*

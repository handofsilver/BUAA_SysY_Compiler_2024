# Java LLVM IR 代码生成实现深度评估

**说明**：本评估基于对 BUAA_Compiler_2024_JAVA_GT 中「LLVM IR 代码生成」相关源码的阅读，**通过大量具体代码块**说明各环节的实现方式与潜在意图，并指出设计问题，使未阅读 Java 源码的人也能快速理解问题所在。供 C++ 重构阶段参考。

---

## 一、关键环节的定义与应用（结合具体代码）

### 1.1 入口与流水线

**意图**：解析+语义通过后，仅当无错误才做代码生成；代码生成只读 AST 与符号表，输出到 `llvm_ir.txt`。

**实际代码**：

```java
// Compiler.java
if (Handler.hasError()) {
    Handler.writeErrorInfo();
} else {
    SymbolManager.startGeneratingIr();   // 切换为「代码生成模式」，见 1.6
    Module module = new ModuleBuilder(compUnit).generateModule();
    Handler.writeLLVMIR(module);
}
```

**要点**：
- 代码生成**强依赖**「先解析、再语义、再代码生成」的固定顺序。
- `startGeneratingIr()` 会改变 `SymbolManager` 的 `pushScope`/`popScope` 行为（见 1.6），与解析阶段形成**隐式契约**，文档中没有任何显式说明。

---

### 1.2 Symbol 同时承载「语义」与「IR 值」——全篇耦合的根源

**意图**：语义阶段用 Symbol 表示名字、类型、作用域；代码生成阶段希望「查到这个名字对应的 IR 值」时，直接复用同一对象，避免再建一套映射。

**实际代码**：

```java
// middle/symbol/Symbol.java（节选）
public final class Symbol {
    private final SymbolType type;
    private final String name;
    private final int scopeId;
    private final ConstExp arraySize;
    private final FuncFParams funcFParams;
    // ... 语义阶段用的字段 ...

    private Value llvmValue;   // 代码生成阶段写入：全局变量 / Param / AllocaInstr 等

    public void setLlvmValue(Value llvmValue) { this.llvmValue = llvmValue; }
    public Value getLlvmValue() { return llvmValue; }
}
```

**含义**：
- 语义分析时 Symbol 只存「类型、作用域、形参列表」等；**代码生成时**各处会 `symbol.setLlvmValue(xxx)`，把 GlobalVariable、Param、AllocaInstr 等**写回**同一个 Symbol。
- 后续只要「用名字生成 load/store/gep」，就 `useSymbol(name) -> symbol.getLlvmValue()`，拿到的是**当时绑定的 IR 值**。

**问题**：
- 没看过代码的人很难想到「符号表里的 Symbol 会被代码生成改写」；语义层与 IR 层**共用同一对象**，边界模糊，无法单独测试「仅语义」或「仅 IR」。
- 若将来做 MIPS 等其它后端，又会往 Symbol 上挂另一种值，Symbol 职责会继续膨胀。

---

### 1.3 全局变量生成：在构造函数里查表并改写 Symbol

**意图**：生成全局变量的同时，让「这个名字」在后续函数体内查符号表时，直接得到对应的 GlobalVariable，便于生成 load/store。

**实际代码**：

```java
// middle/llvmir/value/GlobalVariable.java
public GlobalVariable(String name, Constant initValue, boolean isConst) {
    super(initValue.getType().toPointerType());
    this.dataType = initValue.getType().toPointerType().getPointeeType();
    this.setName("@" + name);
    this.linkage = "dso_local";
    this.isConst = isConst;
    this.initValue = initValue;

    // 下面两行：在「构造函数内部」查全局符号表并改写 Symbol
    Symbol symbol = SymbolManager.useSymbol(name, false);
    symbol.setLlvmValue(this);   // 把当前 GlobalVariable 绑到 Symbol 上
}
```

**含义**：
- 一旦 `new GlobalVariable("x", ...)` 被调用，全局名为 `"x"` 的 Symbol 的 `llvmValue` 就被设为这个全局变量；**构造 = 构造对象 + 副作用**。
- 依赖：**必须先完成解析与符号注册**，且 `generateModule()` 里遍历 `compUnit.getDecls()` 的顺序与解析时声明顺序一致，否则 `useSymbol` 可能拿到错误作用域。

**问题**：
- 构造函数内做「查表 + 修改外部状态」，违反常见的「构造只做初始化」的约定，单测和阅读成本高；没看源码的人很难从「GlobalVariable」类名猜到它会改 SymbolManager。

---

### 1.4 函数与形参：Function 存的是 Symbol，入口块里再把 Param 换成 Alloca

**意图**：形参在 LLVM 里先作为 Param 传入，在函数入口块里统一 alloca + store，之后函数体内都用 alloca 的 load/store 访问，符号表里把「形参」替换成「alloca」，这样 LVal 查到的就是 alloca。

**实际代码 1——FunctionBuilder 里给形参绑 Param，且 Function 存的是 Symbol：**

```java
// middle/llvmir/builder/FunctionBuilder.java
public Function generateFunction() {
    SymbolManager.pushScope();

    ArrayList<Symbol> paramList = new ArrayList<>();
    for (FuncFParam param : params) {
        Symbol symbol = SymbolManager.useSymbol(param.getIdent(), false);  // 从当前作用域查形参名
        paramList.add(symbol);
        if (param.isArray()) {
            symbol.setLlvmValue(new Param(pointerType, slotTracker.getNextSlot()));
        } else {
            symbol.setLlvmValue(new Param(integerType, slotTracker.getNextSlot()));
        }
    }
    FunctionType functionType = new FunctionType(retType, paramTypes);
    Function function = new Function(functionType, name, paramList, parentModule);  // 注意：params 是 Symbol 列表
    // ...
}
```

**实际代码 2——入口块里用 Param 生成 alloca + store，再改写 Symbol：**

```java
// middle/llvmir/builder/InstructionBuilder.java
public ArrayList<Instruction> generateInstructionsFromFunction(Function function) {
    ArrayList<Symbol> params = function.getParams();   // 拿到的是 Symbol，不是 Param
    for (int i = 0; i < params.size(); i++) {
        Symbol symbol = params.get(i);
        if (type instanceof IntegerType) {
            Value value = symbol.getLlvmValue();     // 此时是 Param
            AllocaInstr allocaInstr = new AllocaInstr(type, slotTracker.getNextSlot());
            instructions.add(allocaInstr);
            createStoreInstr(value, allocaInstr);    // store param -> alloca
            symbol.setLlvmValue(allocaInstr);         // 用 alloca 覆盖 Param
        }
    }
    return instructions;
}
```

**含义**：
- IR 层的 Function 本应只关心「参数类型 + 参数 Value」；这里却存 `ArrayList<Symbol>`，**语义对象直接进 IR**。
- 形参的「生命周期」是：先 Param → 入口块里 alloca + store → Symbol 被改成 alloca；后面 LVal 查到这个 Symbol 时就要区分「还是 Param」还是「已经是 alloca」（见 1.7 LVal 的 `instanceof Param`）。

**问题**：
- Function 持有 Symbol 导致 IR 与语义强耦合；入口块逻辑依赖「从 Symbol 取 Param、再写回 alloca」，数据流分散在两处，难以一眼看懂。

---

### 1.5 作用域在代码生成阶段的「重放」——最脆弱的设计

**意图**：解析时已经按「进入/离开块」建好了作用域链（SymbolTable 的 prev/next）；代码生成时不想再建一遍，希望「按同样顺序」再走一遍作用域，于是用队列把解析时遇到的表记下来，代码生成时只按下标取表。

**实际代码**：

```java
// middle/SymbolManager.java（节选）
private static boolean generatingIr = false;
private static int index = 0;
private static ArrayList<SymbolTable> accessQueue = new ArrayList<>() {{ add(curTable); }};

public static void startGeneratingIr() {
    generatingIr = true;   // 注意：index 没有在这里显式置 0，依赖类加载或调用前状态
}

private static SymbolTable getTableFromQueue() {
    return accessQueue.get(++index);   // 每次调用 index 自增，取下一张表
}

public static void pushScope() {
    if (generatingIr) {
        curTable = getTableFromQueue();   // 不建新表，只从队列取
        return;
    }
    // 解析阶段：建新表或复用 nextTable，并 addTableToQueue(curTable)
    scopeCount++;
    // ...
    addTableToQueue(curTable);
}

public static void popScope() {
    if (generatingIr) {
        curTable = getTableFromQueue();   // 同样是 index++ 取表
        return;
    }
    curTable = curTable.getPrevTable();
    addTableToQueue(curTable);
}
```

**含义**：
- **解析阶段**：每次 pushScope 或 popScope 都会把「当时的 curTable」加入 `accessQueue`，所以队列里是按「进入/离开」顺序排列的一张张表。
- **代码生成阶段**：pushScope/popScope 不再建表，只做 `curTable = accessQueue.get(++index)`；也就是说，**代码生成时进入/离开块的次数和顺序必须和解析时完全一致**，否则 index 错位，curTable 就错了，变量会绑到错误作用域甚至越界。

**问题**：
- 解析与代码生成之间**没有任何类型或接口**约束「push/pop 顺序必须一致」，完全靠「两边用相同方式遍历 AST」的隐式约定；一旦重构（例如改成 Visitor 遍历），很容易少一次 push 或多一次 pop，bug 难查。
- 没读过源码的人很难从「SymbolManager」这个名字联想到「代码生成时作用域是在重放解析时的队列」。

---

### 1.6 控制流：依赖「当前块」可变状态，嵌套 if 必须用 curBasicBlock 而不是 ifBlock

**意图**：if/else、for 需要新建多个 BasicBlock，并在正确块末尾加 br；「当前正在填哪一块」和「break/continue 跳哪」用成员变量在 Builder 之间传递。

**实际代码——if 分支结尾的 br 必须加到 curBasicBlock 上：**

```java
// middle/llvmir/builder/BasicBlockBuilder.java
private BasicBlock generateBasicBlockFromIfStmt(Stmt stmt, BasicBlock entryBlock) {
    BasicBlock nextBlock = new BasicBlock(...);
    BasicBlock tempBlock = curBasicBlock;
    BasicBlock ifBlock = new BasicBlock(...);
    InstructionBuilder ifInstrBuilder = new InstructionBuilder(ifBlock, this, slotTracker, ifStmt, parentFunction);
    curBasicBlock = ifBlock;    // 进入 if 分支
    ifBlock.addAllInstruction(ifInstrBuilder.generateInstructions());  // 生成 if 内语句，其中可能嵌套 if/for，会再次改 curBasicBlock
    /*
     * 原注释：这里必须用 curBasicBlock.add(br)，不能写 ifBlock.add(br)。
     * 原因：若有嵌套 if，generateInstructions() 里会 handleIfStmt，把 curBasicBlock 改成内层的 nextBlock，
     * 所以这里加 br 应该加在内层 nextBlock 末尾，而不是外层的 ifBlock。
     */
    curBasicBlock.addInstruction(new BrInstr(nextBlock));
    saveBlock(ifBlock);
    curBasicBlock = tempBlock;  // 恢复
    // ...
}
```

**含义**：
- 「在哪个块末尾加 br」不能写死成 `ifBlock`，因为**递归生成 if 体内语句时**，内层 if 会把 `curBasicBlock` 改成内层的 nextBlock；所以这里故意用**当前的** `curBasicBlock`，这样嵌套时 br 会加在正确块上。
- 逻辑正确性完全依赖「curBasicBlock 在递归过程中的变化」与「恢复 tempBlock」的时机，可读性差，维护者必须理解整条调用链。

**问题**：
- 没看注释的人很难理解「为什么不是 ifBlock.addInstruction」；控制流生成**高度依赖可变成员**，难以用「显式参数」或不变式描述。

---

### 1.7 条件表达式 Cond：展平 LOrExp/LAndExp，用多块 + 条件 br 串联

**意图**：Cond 是 `LOrExp`（内部是多个 `LAndExp`，每个又是多个 `EqExp`）；要生成短路求值，需要为每个子表达式建块、算 cond、再根据 true/false 跳到不同块。

**实际代码**：

```java
// middle/llvmir/builder/BasicBlockBuilder.java
private void generateBasicBlockFromCond(Cond cond, BasicBlock entryBlock, BasicBlock trueBlock, BasicBlock falseBlock) {
    if (cond == null) {
        entryBlock.addInstruction(new BrInstr(trueBlock));
        return;
    }
    ArrayList<LAndExp> lAndExps = cond.getLOrExp().getLAndExps();
    ArrayList<Value> condList = new ArrayList<>();
    ArrayList<BasicBlock> blockList = new ArrayList<>();
    BasicBlock curBlock = entryBlock;
    int count = 0;
    for (LAndExp lAndExp : lAndExps) {
        for (EqExp eqExp : lAndExp.getEqExps()) {
            count++;
            InstructionBuilder instructionBuilder = new InstructionBuilder(curBlock, this, slotTracker, parentFunction);
            Value val = instructionBuilder.generateInstructionFromEqExp(eqExp, true);
            curBlock.addAllInstruction(instructionBuilder.generateInstructions());
            blockList.add(curBlock);
            condList.add(val);
            curBlock = new BasicBlock(...);   // 下一个子表达式用新块
        }
    }
    int loc = 0;
    for (int i = 0; i < lAndExps.size(); i++) {
        // 根据 loc、eqExps.size()、count 计算 trueDest/falseDest，在每个块末尾加 BrInstr(brCond, trueDest, falseDest)
        // ...
    }
    for (int i = 1; i < blockList.size(); i++) {
        saveBlock(blockList.get(i));   // 新块在循环末尾才加入 blocks 列表
    }
}
```

**含义**：
- 第一轮循环：对每个 EqExp 建一个 InstructionBuilder、生成该子表达式的指令和 cond Value，记入 `blockList`/`condList`，然后 `curBlock` 指向新块。
- 第二轮循环：按 LAndExp/LOrExp 的短路语义，用 `loc`、`count`、`eqExps.size()` 算每个块 br 的 trueDest/falseDest，往块里加条件 br。
- 新块除了 entryBlock 外，都在后面 `saveBlock(blockList.get(i))` 才加入 `blocks`，所以**块在列表里的顺序**和**控制流顺序**并不一致。

**问题**：
- `loc`、`count`、`blockList`、`condList` 的对应关系不直观，没读过 AST 结构（LOrExp/LAndExp/EqExp）的人很难跟上；扩展或修 bug 成本高。

---

### 1.8 LVal 生成：用 Symbol 的 llvmValue，并对 Param 做特殊判断

**意图**：左值要得到「地址」（用于 store），右值要得到「值」（常数或 load）；查符号表得到 Symbol 后，用 `symbol.getLlvmValue()` 区分全局变量、alloca、Param、数组等。

**实际代码**：

```java
// middle/llvmir/builder/InstructionBuilder.java
private Value generateInstructionFromLVal(LVal lVal, boolean isLeft) {
    Symbol symbol = SymbolManager.useSymbol(lVal.getIdent(), true);
    // 数组：gep + 若右值则 load
    if (symbol.isCharArr() || symbol.isIntArr()) {
        // ... GetElementPtrInstr，isLeft 时返回 gep，否则 load ...
    }
    if (isLeft) {
        return symbol.getLlvmValue();   // 左值直接返回地址（alloca 或全局变量）
    }
    IntegerType type = symbol.isInt() ? IntegerType.get32() : IntegerType.get8();
    if (symbol.getLlvmValue() instanceof Param) {   // 原注释：写的有点丑，可以优化
        return symbol.getLlvmValue();   // Param 不 load，因为入口块已经 alloca+store，但这里仍返回 Param 用于只读
    }
    LoadInstr loadInstr = new LoadInstr(type, symbol.getLlvmValue(), slotTracker.getNextSlot());
    instructions.add(loadInstr);
    return loadInstr;
}
```

**含义**：
- 标量右值：若当前 Symbol 的 llvmValue 仍是 **Param**（即还没被入口块替换成 alloca 的时机不存在于正常流程，但类型上可能存在），就原样返回 Param；否则对 alloca/全局变量做 load。
- 这里用 `instanceof Param` 区分「形参」与「局部/全局」，和「入口块里把 Param 换成 alloca」的设计绑在一起，可读性差。

**问题**：
- 没看过「入口块先 alloca 再 setLlvmValue(alloca)」的读者，无法理解为什么要有 `instanceof Param`；LVal 逻辑与函数入口块逻辑**跨多处耦合**。

---

### 1.9 printf 时动态往 Module 里塞全局字符串

**意图**：遇到 `printf("hello%d", x)` 时，需要生成全局字符串常量并 putstr；实现选择在「生成这条语句时」直接 new GlobalVariable 并 add 到 Module。

**实际代码**：

```java
// middle/llvmir/builder/InstructionBuilder.java
private void createAndPrintGlobalStr(String str) {
    GlobalVariable globalVariable = new GlobalVariable(str);   // 仅用于字符串内容，不绑定 Symbol
    parentFunction.getParentModule().addGlobalVar(globalVariable);   // 生成指令的同时修改 Module
    // ... 生成 gep + call putstr ...
    instructions.add(getElementPtrInstr);
    instructions.add(callPutStr);
}
```

**含义**：
- 生成**某一条** printf 相关指令时，会**顺带**往 Module 的 globalVars 里追加一个全局变量；Module 的结构是在「遍历语句」的过程中被逐步改动的，而不是「先收集所有全局量再统一输出」。

**问题**：
- 「生成指令」与「修改 Module 结构」混在一起，不利于分层和单测；没看代码的人不会想到「InstructionBuilder 会改 Module」。

---

### 1.10 Module / Function 输出中的写死与潜在错误

**意图**：按「declare → 全局变量 → 函数」顺序输出 IR。

**实际代码 1——declare 写死 5 条：**

```java
// middle/llvmir/value/Module.java
@Override
public ArrayList<String> toIrStr() {
    ArrayList<String> ir = new ArrayList<>();
    ir.add("declare i32 @getint()");
    ir.add("declare i32 @getchar()");
    ir.add("declare void @putint(i32)");
    ir.add("declare void @putch(i32)");
    ir.add("declare void @putstr(i8*)");
    ir.add("\n");
    // 再输出 globalVars、functions...
}
```

题目要求「没有实际使用的函数可以不声明」，这里**未做 use 分析**，会多声明未使用的 IO 函数。

**实际代码 2——void 函数末尾多一条 ret：**

```java
// middle/llvmir/value/Function.java
@Override
public ArrayList<String> toIrStr() {
    // ...
    for (BasicBlock basicBlock : basicBlocks) {
        ir.addAll(basicBlock.toIrStr());
    }
    if ((funcType.getRetType() == VoidType.get())) {
        ir.add("\tret void");   // 无条件多加一行 ret void
    }
    ir.add("}\n");
    return ir;
}
```

若每个 void 函数的最后一块里**已经有** `ret void`，这里会再输出一条 `ret void`，且这条在「所有 basicBlock 的 toIrStr 之后、`}` 之前」，即**不在任何基本块内**，违反 LLVM IR 的「每条指令必须属于某基本块」的约束，可能造成无效 IR 或评测异常。

---

## 二、设计缺陷的系统评估（对应到具体代码）

### 2.1 语义与代码生成的强耦合

| 现象 | 对应代码 | 后果 |
|------|----------|------|
| Symbol 同时承载语义与 IR | `Symbol.llvmValue` + 各处 `setLlvmValue` / `getLlvmValue` | 语义层与 IR 层边界模糊，无法单独测「仅语义」或「仅 IR」；多后端时 Symbol 职责膨胀 |
| 构造内副作用 | `GlobalVariable` 构造里 `useSymbol` + `setLlvmValue(this)` | 可测试性差，违反「构造只做初始化」的常见约定 |
| IR 层持有语义对象 | `Function.params` 为 `ArrayList<Symbol>` | IR 应只关心 Value，这里却把语义对象放进 IR，后续 alloca 逻辑又要从 Symbol 取 Param 再写回 |
| 生成指令时改 Module | `createAndPrintGlobalStr` 里 `getParentModule().addGlobalVar(...)` | 「当前块指令」与「Module 结构」耦合，不利于分层 |

### 2.2 作用域「重放」与顺序依赖

| 现象 | 对应代码 | 后果 |
|------|----------|------|
| 代码生成不建新表，只按下标取表 | `pushScope`/`popScope` 在 `generatingIr` 下执行 `curTable = getTableFromQueue()`，即 `accessQueue.get(++index)` | 代码生成时 push/pop 的**次数和顺序**必须与解析时完全一致，否则 index 错位、变量绑错作用域或越界 |
| 隐式契约 | 两阶段没有任何接口或类型约束「顺序一致」 | 重构（如改 Visitor 遍历）时极易破坏，bug 难查 |
| 查符号时还看 llvmValue | `lookupSymbolWithCheck` 里 `!checkScope \|\| table.getScopeId()==1 \|\| symbol.getLlvmValue()!=null` 才返回 | 用「是否已绑定 IR 值」参与作用域决策，与「代码生成逐步写 llvmValue」强相关，可读性差 |

### 2.3 Builder 职责混杂与「当前块」可变状态

| 现象 | 对应代码 | 后果 |
|------|----------|------|
| InstructionBuilder 多种构造 | 有「只带 Stmt」「只带 Decl」「只带 Function」「无 stmt/decl」等构造 | 同一类既生成「单条语句/声明」又生成「入口块形参 alloca」，还通过 `basicBlockBuilder.handle*` 把控制流交给 BasicBlockBuilder，职责不清 |
| 当前块与 break/continue 用成员传递 | `BasicBlockBuilder.curBasicBlock`、`breakBlock`、`continueBlock` 在 if/for/块中反复改写 | 嵌套时必须用 curBasicBlock 而不是 ifBlock 加 br（见 1.6），逻辑依赖递归中的状态变化，难以用显式参数表达 |
| instructions 列表归属 | 每个 InstructionBuilder 一个 `instructions`，最后由调用方 `curBasicBlock.addAllInstruction(builder.generateInstructions())` | 在 generateBasicBlockFromCond 里多个 Builder 对应多个块，若误用「另一个 Builder 的 instructions」或「错误的 curBasicBlock」难以发现 |

### 2.4 控制流与 Cond 展平

| 现象 | 对应代码 | 后果 |
|------|----------|------|
| Cond 展平逻辑复杂 | `generateBasicBlockFromCond` 里 `blockList`、`condList`、`loc`、`count` 与 LOrExp/LAndExp/EqExp 的对应 | 不熟悉 AST 结构的人难以理解；块在 `blocks` 中的顺序与控制流顺序不一致（后面才 saveBlock） |
| 嵌套 if 依赖 curBasicBlock | 必须用 `curBasicBlock.addInstruction(new BrInstr(nextBlock))` 而不能写 `ifBlock.add...` | 注释承认「a bit tricky」，维护成本高 |

### 2.5 其它实现细节与潜在错误

| 现象 | 对应代码 | 后果 |
|------|----------|------|
| declare 写死 | `Module.toIrStr()` 固定 5 条 declare | 未按「实际使用」声明，与题目「可不声明未使用函数」不符 |
| void 函数多一条 ret | `Function.toIrStr()` 在 basicBlocks 后无条件 `ir.add("\tret void")` | 若块内已有 ret void，会多出一条「块外」ret，可能产生无效 IR |
| 全局变量初值强转 | `GlobalVariableBuilder` 里 `(ConstInitVal) varDef.getInitVal()` | 非常量初值时 ClassCastException |
| 形参特殊判断 | `generateInstructionFromLVal` 里 `symbol.getLlvmValue() instanceof Param` | 与入口块「Param→alloca 并 setLlvmValue」的设计绑定，可读性差 |

---

## 三、总结与对 C++ 重构的建议

### 3.1 关键环节回顾（用代码位置串起来）

- **入口**：`Compiler` 无错时 `startGeneratingIr()` → `ModuleBuilder(compUnit).generateModule()` → `Handler.writeLLVMIR(module)`。
- **Symbol 与 IR**：全局变量在 `GlobalVariable` 构造里 `useSymbol` + `setLlvmValue(this)`；形参在 `FunctionBuilder` 里 `setLlvmValue(Param)`，入口块在 `InstructionBuilder.generateInstructionsFromFunction` 里 alloca + store 再 `setLlvmValue(alloca)`；局部变量/常量在生成 Decl 时 alloca 并 `setLlvmValue(alloca)`；LVal/表达式通过 `symbol.getLlvmValue()` 取地址或值，并对 `Param` 做特殊分支。
- **作用域**：代码生成阶段不建新表，`SymbolManager.pushScope`/`popScope` 在 `generatingIr==true` 时只做 `curTable = accessQueue.get(++index)`，依赖与解析阶段完全一致的 push/pop 顺序。
- **控制流**：`BasicBlockBuilder` 维护 `curBasicBlock`/`breakBlock`/`continueBlock`，if/for 中建新块、改当前块、在**当前** curBasicBlock 末尾加 br（嵌套时依赖递归对 curBasicBlock 的修改）；Cond 在 `generateBasicBlockFromCond` 里展平 LOrExp/LAndExp，用 blockList/condList/loc/count 生成条件 br。

### 3.2 主要设计缺陷汇总

- 语义与代码生成强耦合：Symbol 承载 llvmValue，构造与静态方法中大量副作用，IR 层持有 Symbol，无独立「纯 IR」构建阶段。
- 作用域重放脆弱：accessQueue + index，与解析阶段顺序强绑定，隐式契约易在重构中被破坏。
- Builder 职责混杂、可变状态多：InstructionBuilder 与 BasicBlockBuilder 互相调用，curBasicBlock/break/continue 依赖递归中的修改，instructions 与「当前块」的对应易错。
- 控制流与 Cond 展平复杂，且 void 的 ret、declare 写死、createAndPrintGlobalStr 改 Module 等细节存在潜在错误或不符合题目要求。

### 3.3 对 C++ 重构的建议

1. **分离「语义符号」与「IR 值」**：语义层 Symbol 只保留类型、作用域、是否常量等；代码生成维护「名字 → IR Value」的映射（如每函数/每块一个 map 或 ScopedMap），在进入/离开块时 push/pop，**不往 Symbol 上挂 LLVM Value**。
2. **代码生成自管作用域**：不依赖「重放 accessQueue」；遍历 AST 时根据进入/离开块、进入函数**显式**维护自己的作用域栈，从语义层只读地查符号信息，写只写在代码生成自己的上下文中。
3. **显式传递「当前块」与控制流目标**：将当前 BasicBlock、break/continue 目标作为 CodeGenContext 或 Visitor 参数在递归中传递，避免全局或 Builder 成员可变状态；或采用「先建 CFG（块+边），再在块内填指令」的两阶段。
4. **IR 构建与输出解耦**：declare 按实际使用的内置函数生成；先完整构建 Module（含字符串常量等），再统一 toIrStr/打印，避免在生成某条指令时修改 Module。
5. **代码生成作为独立遍历**：输入 AST + 只读语义信息，输出 Module；不与 Parser/语义分析共享可变状态。
6. **类型提升与 store/return 复用**：zext/trunc 与类型匹配封装成少量工具（如 `coerceTo(Value*, Type*)`），在 store、return、实参等处复用。
7. **Cond 与块顺序**：可为 Cond 先建显式结构（如 (cond, trueBlock, falseBlock) 列表）再生成块与 br，使控制流和块顺序更清晰、可预期。

---

*文档基于对 BUAA_Compiler_2024_JAVA_GT 源码的阅读，通过具体代码块说明实现与问题，便于未读 Java 代码者快速理解。*

# **语法分析**

## **问题描述**

请根据本课程平台的`课程信息-课件下载`页面给定的文法文档，并结合指导书，设计并实现语法分析程序，从源程序中识别出相应的语法成分，输入输出及处理要求如下：

- 对于**正确的源程序**，用递归子程序法对文法中定义的语法成分进行分析。
  - 按词法分析识别单词的顺序，按行输出每个单词的信息。
  - 在文法中出现（除了<BlockItem>, <Decl>, <BType> 之外）的语法分析成分分析结束前，另起一行输出当前语法成分的名字，形如“<Stmt>”（注：未要求输出的语法成分仍需要进行分析，但无需输出）
- 对于**错误的源程序**，需要识别出错误，并输出**错误所在的行号**和**错误的类别码**。

单词的类别码统一定义如下：

| 单词名称        | 类别码     | 单词名称 | 类别码    | 单词名称 | 类别码 | 单词名称 | 类别码  |
| :-------------- | :--------- | :------- | :-------- | :------- | :----- | :------- | :------ |
| **Ident**       | IDENFR     | else     | ELSETK    | void     | VOIDTK | ;        | SEMICN  |
| **IntConst**    | INTCON     | !        | NOT       | *        | MULT   | ,        | COMMA   |
| **StringConst** | STRCON     | &&       | AND       | /        | DIV    | (        | LPARENT |
| **CharConst**   | CHRCON     | \|\|     | OR        | %        | MOD    | )        | RPARENT |
| main            | MAINTK     | for      | FORTK     | <        | LSS    | [        | LBRACK  |
| const           | CONSTTK    | getint   | GETINTTK  | <=       | LEQ    | ]        | RBRACK  |
| int             | INTTK      | getchar  | GETCHARTK | >        | GRE    | {        | LBRACE  |
| char            | CHARTK     | printf   | PRINTFTK  | >=       | GEQ    | }        | RBRACE  |
| break           | BREAKTK    | return   | RETURNTK  | ==       | EQL    |          |         |
| continue        | CONTINUETK | +        | PLUS      | !=       | NEQ    |          |         |
| if              | IFTK       | -        | MINU      | =        | ASSIGN |          |         |



## **输入形式**

testfile.txt 中的测试程序，有的是正确的源程序，有的是有错误的源程序。

## **输出形式**

对于**正确的源程序**，将 **单词类别码** 和 **单词的字符/字符串形式** 或 **语法成分名字** 按读入顺序输出至 parser.txt。

```
单词类别码 单词的字符/字符串形式（中间仅用一个空格间隔）
```

或

```
语法成份名字
```

对于**错误的源程序**，将 **错误所在的行号**和**错误的类别码** 按行号从小到大输出至 error.txt，行号从1开始编号。

```
错误的行号 错误的类别码（中间仅用一个空格间隔）
```

## **注意事项**

1. **终结符 token 需要输出，但是是输出其词法成分**，语法分析比起词法分析需要额外输出的是**若干个词法的 token 组成的非终结符成分，并通过 `<非终结符>` 的形式给出**。
2. 请自行设计数据结构记录语法分析通过递归下降子程序法生成的语法树，这个语法树将在后续的实验中重复使用。
3. 当前要求的正确源程序的输出只是为了便于评测，完整的编译器最后无需出现这些信息，请设计为方便打开/关闭这些输出的方案。
4. 本次作业中对于每个错误样例，只会出现词法分析和语法分析部分的错误。
5. **即使是一个错误的源程序，也应该完成词法分析和语法分析的全部任务，这样才能进行语义分析部分的错误处理。**

## **语法分析可能出现的错误**

```
编译单元 CompUnit → {Decl} {FuncDef} MainFuncDef 

声明 Decl → ConstDecl | VarDecl 

常量声明 ConstDecl → 'const' BType ConstDef { ',' ConstDef } ';' // i

基本类型 BType → 'int' | 'char' 

常量定义 ConstDef → Ident [ '[' ConstExp ']' ] '=' ConstInitVal // k

常量初值 ConstInitVal → ConstExp | '{' [ ConstExp { ',' ConstExp } ] '}' | StringConst

变量声明 VarDecl → BType VarDef { ',' VarDef } ';' // i

变量定义 VarDef → Ident [ '[' ConstExp ']' ] | Ident [ '[' ConstExp ']' ] '=' InitVal // k

变量初值 InitVal → Exp | '{' [ Exp { ',' Exp } ] '}' | StringConst 

函数定义 FuncDef → FuncType Ident '(' [FuncFParams] ')' Block // j

主函数定义 MainFuncDef → 'int' 'main' '(' ')' Block // j

函数类型 FuncType → 'void' | 'int' | 'char'

函数形参表 FuncFParams → FuncFParam { ',' FuncFParam } 

函数形参 FuncFParam → BType Ident ['[' ']'] // k

语句块 Block → '{' { BlockItem } '}' 

语句块项 BlockItem → Decl | Stmt 

语句 Stmt → LVal '=' Exp ';' // i
| [Exp] ';' // i
| Block
| 'if' '(' Cond ')' Stmt [ 'else' Stmt ] // j
| 'for' '(' [ForStmt] ';' [Cond] ';' [ForStmt] ')' Stmt 
| 'break' ';' | 'continue' ';' // i
| 'return' [Exp] ';' // i
| LVal '=' 'getint''('')'';' // i j
| LVal '=' 'getchar''('')'';' // i j
| 'printf''('StringConst {','Exp}')'';' // i j

语句 ForStmt → LVal '=' Exp 

表达式 Exp → AddExp 
 
条件表达式 Cond → LOrExp 

左值表达式 LVal → Ident ['[' Exp ']'] // k

基本表达式 PrimaryExp → '(' Exp ')' | LVal | Number | Character// j
    
数值 Number → IntConst 
    
字符 Character → CharConst 

一元表达式 UnaryExp → PrimaryExp | Ident '(' [FuncRParams] ')' | UnaryOp UnaryExp // j

单目运算符 UnaryOp → '+' | '−' | '!' 注：'!'仅出现在条件表达式中 

函数实参表 FuncRParams → Exp { ',' Exp } 

乘除模表达式 MulExp → UnaryExp | MulExp ('*' | '/' | '%') UnaryExp 

加减表达式 AddExp → MulExp | AddExp ('+' | '−') MulExp 

关系表达式 RelExp → AddExp | RelExp ('<' | '>' | '<=' | '>=') AddExp 

相等性表达式 EqExp → RelExp | EqExp ('==' | '!=') RelExp 

逻辑与表达式 LAndExp → EqExp | LAndExp '&&' EqExp

逻辑或表达式 LOrExp → LAndExp | LOrExp '||' LAndExp

常量表达式 ConstExp → AddExp 注：使用的 Ident 必须是常量 
```

## **样例**

### **正确源程序样例**

样例输入：

```
int main(){
    int c;
    c= getint();
    printf("%d",c);
    return c;
}
```

样例输出：

```
INTTK int
MAINTK main
LPARENT (
RPARENT )
LBRACE {
INTTK int
IDENFR c
<VarDef>
SEMICN ;
<VarDecl>
IDENFR c
<LVal>
ASSIGN =
GETINTTK getint
LPARENT (
RPARENT )
SEMICN ;
<Stmt>
PRINTFTK printf
LPARENT (
STRCON "%d"
COMMA ,
IDENFR c
<LVal>
<PrimaryExp>
<UnaryExp>
<MulExp>
<AddExp>
<Exp>
RPARENT )
SEMICN ;
<Stmt>
RETURNTK return
IDENFR c
<LVal>
<PrimaryExp>
<UnaryExp>
<MulExp>
<AddExp>
<Exp>
SEMICN ;
<Stmt>
RBRACE }
<Block>
<MainFuncDef>
<CompUnit>
```

### **错误源程序样例**

样例输入：

```
int main(){
    char c;
    c = getchar()
    if(1 & 2){
        printf("output is %c\n", c);
    }
    return 0;
}
```

样例输出：

```
3 i
4 a
```

## **评分标准**

- 对于**正确的源程序**，按与预期结果不一致的行数扣分，每项扣5%。
- 对于**错误的源程序**，准确报出第一个错误得60%的分数，后续每个错误平分40%的分数。若只有一个错误，则准确报出该错误获得100%的分数。多报和错报每一个扣除该样例总分的10%，直至该样例总分为0。

## **参考资料**

- 教材 第 18 章 18.3.5
- 根据 PASCAL-S 文法的定义，阅读源代码，理解程序的框架，了解各语法成分对应的子程序以及子程序之间的调用关系和接口；对其中与实验作业文法中类似的语法成分，要重点阅读其代码，进行分析、理解，为今后的语义分析打下基础（详见 pascals-compiler.docx）
- 希冀平台 `课程信息-课件下载` 页面相关资料

## **资料下载**



- PL/0、PASCALS编译器源代码可以从希冀平台的 `课程信息-课件下载` 页面中获取。

## **辅助工具**

- 2024语法分析作业公共测试程序库

## **开发语言及环境**

- 用 C/C++/JAVA 实现。
- 机房安装的 C/C++ 开发环境是 CLion 2019.3.6；
- Java 的开发环境为 IDEA 2022.2.2 社区版。
- 集成开发环境的安装见 `在线教程` 中的编译技术实验教程。

## **文档要求**

完善参考编译器介绍、总体设计部分，完成语法分析阶段设计文档。

## **提交形式**

- 评测机所采用的编译学生代码的版本是：`CMake 3.28.3，C/C++ clang 12.0.0 C++17，Java jdk 17`。

- 上传请将所开发的词法分析程序的源文件（.cpp/.c/.h/.Java，不含工程文件）打包为 zip 后提交。

- **对于使用 Java 开发的编译器**，程序运行的入口为 Compiler.java 中的 main 方法。**提交时请直接Compiler.java打包在压缩包的最上层(最上层仅包含一个Compiler.java文件)，不要嵌套一层 src 文件夹**。平台 **不支持 Java 第三方包**。

- 对于使用 C++ 开发的编译器，可以使用 CMake 进行项目管理。如果使用 CMake，可以自由选择 Clang 支持的 C++ 标准，注意需要使用 `project(Compiler)` 设置项目名称，确保 CMake 编译后输出的可执行文件名称为 `Compiler`，提交时需要在压缩包最上层包含 `CMakeLists.txt`，并且不要提交 CMake 构建过程的临时文件。如果不使用 CMake，平台默认使用 C++ 17 标准。

- 注意在 MAC 下压缩会产生额外的文件到压缩包中，需删掉额外文件后提交。

- 提交时，你的压缩包**顶层**需要包含一个如下内容的json文件（**命名为config.json**）：

  ```
  {
      "programming language": "xxx",
      "object code": "yyy"
  }
  ```

  其中，xxx为你的编译器所使用的编程语言（java, c 或 cpp），yyy为你生成的目标代码类型（pcode, llvm 或 mips），所有字母均为小写。如果评测结果为ConfigurationError，说明你的config.json文件存在问题，无法被评测程序解析，请查看评测程序返回的详细信息进行修改。

  在本次作业中，如果你还没有确定你的目标代码，可以随意填写，不会与后续作业形成绑定。

- 使用 Java 提交的文件结构示例如下，除了 Compiler.java 和 config.json 外可以有任意数量的文件和文件夹：

  ```
  .
  ├── Compiler.java
  ├── config.json
  ├── frontend
   └── Lexer.java
  ```
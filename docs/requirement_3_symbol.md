# **语义分析**

## **问题描述**

请根据希冀平台的`课程信息-课件下载`页面给定的文法文档，并结合在线教程，设计并实现语义分析程序，程序要能够处理正确的源程序以及错误的源程序。

- 对于**正确的源程序**，需要从源程序中识别出定义的常量、变量、函数、形参，输出它们的作用域序号，单词的字符/字符串形式，类型名称。
- 对于**错误的源程序**，需要识别出错误，并输出**错误所在的行号**和**错误的类别码**。

语义分析中，不同类型的类型名称统一定义如下：

| 类型           | 类型名称       | 类型           | 类型名称  | 类型       | 类型名称 |
| -------------- | -------------- | -------------- | --------- | ---------- | -------- |
| char型常量     | ConstChar      | char型变量     | Char      | void型函数 | VoidFunc |
| int型常量      | ConstInt       | int型变量      | Int       | char型函数 | CharFunc |
| char型常量数组 | ConstCharArray | char型变量数组 | CharArray | int型函数  | IntFunc  |
| int型常量数组  | ConstIntArray  | int型变量数组  | IntArray  |            |          |

## **输入形式**

testfile.txt 中的测试程序，有的是正确的源程序，有的是有错误的源程序。

## **输出形式**

对于**正确的源程序**，将 **作用域序号**、**单词的字符/字符串形式** 和 **类型名称** 按顺序输出至 symbol.txt。其中全局变量的作用域序号为1。

```
作用域序号 单词的字符/字符串形式 类型名称（中间仅用一个空格间隔）
```

对于**错误的源程序**，将 **错误所在的行号**和**错误的类别码** 按行号从小到大输出至 error.txt，行号从1开始编号。

```
错误的行号 错误的类别码（中间仅用一个空格间隔）
```

## **注意事项**

1. 本次作业需要建立符号表，**除了要求的输出内容，其余符号表信息请大家自行设计，但是额外的信息请不要输出**。
2. **设计符号表请考虑后续中间代码生成过程中对符号表的使用**。
3. 当前要求的正确源程序的输出只是为了便于评测，完整的编译器最后无需出现这些信息，请设计为方便打开/关闭这些输出的方案。
4. 本次作业中对于每个错误样例，可能出现所有类型的错误。
5. **关于作用域序号，即进入该作用域之前进入的作用域数量加1**。进入全局作用域时进入的作用域数量为0，因此全局作用域序号为1。
6. 关于符号表的输出，按照作用域序号大小依次输出，先输出序号较小的作用域中的全部符号。对于同一个作用域，按照符号被声明的先后顺序输出。
7. **函数的参数属于函数内部的作用域**。
8. `main` 是保留的关键字，不纳入符号表中。

## **语义分析中可能出现的错误**

```
编译单元 CompUnit → {Decl} {FuncDef} MainFuncDef 

声明 Decl → ConstDecl | VarDecl 

常量声明 ConstDecl → 'const' BType ConstDef { ',' ConstDef } ';' 

基本类型 BType → 'int' | 'char' 

常量定义 ConstDef → Ident [ '[' ConstExp ']' ] '=' ConstInitVal // b

常量初值 ConstInitVal → ConstExp | '{' [ ConstExp { ',' ConstExp } ] '}' | StringConst

变量声明 VarDecl → BType VarDef { ',' VarDef } ';' 

变量定义 VarDef → Ident [ '[' ConstExp ']' ] | Ident [ '[' ConstExp ']' ] '=' InitVal // b

变量初值 InitVal → Exp | '{' [ Exp { ',' Exp } ] '}' | StringConst 

函数定义 FuncDef → FuncType Ident '(' [FuncFParams] ')' Block // b g

主函数定义 MainFuncDef → 'int' 'main' '(' ')' Block // g

函数类型 FuncType → 'void' | 'int' | 'char'

函数形参表 FuncFParams → FuncFParam { ',' FuncFParam } 

函数形参 FuncFParam → BType Ident ['[' ']'] // b

语句块 Block → '{' { BlockItem } '}' 

语句块项 BlockItem → Decl | Stmt 

语句 Stmt → LVal '=' Exp ';' // h
| [Exp] ';' 
| Block
| 'if' '(' Cond ')' Stmt [ 'else' Stmt ] 
| 'for' '(' [ForStmt] ';' [Cond] ';' [ForStmt] ')' Stmt // h
| 'break' ';' | 'continue' ';' // m
| 'return' [Exp] ';' // f
| LVal '=' 'getint''('')'';' // h
| LVal '=' 'getchar''('')'';' // h
| 'printf''('StringConst {','Exp}')'';' // l

语句 ForStmt → LVal '=' Exp // h

表达式 Exp → AddExp 
 
条件表达式 Cond → LOrExp 

左值表达式 LVal → Ident ['[' Exp ']'] // c

基本表达式 PrimaryExp → '(' Exp ')' | LVal | Number | Character
    
数值 Number → IntConst 
    
字符 Character → CharConst 

一元表达式 UnaryExp → PrimaryExp | Ident '(' [FuncRParams] ')' | UnaryOp UnaryExp // c d e

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
const int year = 2024, month = 9;
int day;

int getDay(){
    int day;
    day = getint();
    return day;
}

void putString(char s[], int length){
    int i = 0;
    for(;i < length; i = i + 1){
        printf("%c", s[i]);
    }
    printf("\n");
}

int strlen(char s[]){
    int i = 0;
    for(; s[i] != '\0'; i = i + 1) ;
    return i;
}

char charAt(char s[], int index){
    return s[index];
}

int main(){
    day = getDay();
    printf("Tody is %d-%d-%d\n", year, month, day);

    char s[12] = "hello world";
    {
        int length = strlen(s);
        if(length > 4){
            char tmp = charAt(s, 4);
        }
    }

    return 0;
}
```

样例输出：

```
1 year ConstInt
1 month ConstInt
1 day Int
1 getDay IntFunc
1 putString VoidFunc
1 strlen IntFunc
1 charAt CharFunc
2 day Int
3 s CharArray
3 length Int
3 i Int
5 s CharArray
5 i Int
6 s CharArray
6 index Int
7 s CharArray
8 length Int
9 tmp Char
```

### **错误源程序样例**

样例输入：

```
const int const1 = 1, const2 = -100;
int change1;
int gets1(int var1,int var2){
   const1 = 999;
   change1 = var1 + var2          return (change1);
}
int main(){
   change1 = 10;
   printf("Hello World$");
   return 0;
}
```

样例输出：

```
4 h
5 i
```

## **评分标准**

- 对于**正确的源程序**，按与预期结果不一致的行数扣分，每项扣5%。
- 对于**错误的源程序**，准确报出第一个错误得60%的分数，后续每个错误平分40%的分数。若只有一个错误，则准确报出该错误获得100%的分数。多报和错报每一个扣除该样例总分的10%，直至该样例总分为0。

## **参考资料**

- 教材 第 5 章 和 第 8 章。
- 教材 第18章 18.3.1 18.3.6。
- 根据PASCAL-S文法的定义，阅读编译器源代码，了解符号表的设计实现方案和错误处理实现方案；在此基础上，为自己的编译器添加符号表管理、错误处理功能，编译器源代码见 pascals-compiler.docx。

## **资料下载**

- PL/0、PASCALS编译器源代码可以从希冀平台的 `课程信息-课件下载` 页面中获取。

## **辅助工具**

- 2024语义分析作业公共测试程序库

## **开发语言及环境**

- 用 C/C++/JAVA 实现。
- 机房安装的 C/C++ 开发环境是 CLion 2019.3.6；
- Java 的开发环境为 IDEA 2022.2.2 社区版。
- 集成开发环境的安装见 `在线教程` 中的编译技术实验教程。

## **文档要求**

完成语义分析阶段设计文档 （在语法分析阶段设计文档基础上扩充完成） 。

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
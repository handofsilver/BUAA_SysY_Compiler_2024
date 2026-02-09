# **词法分析**

## **问题描述**

请根据希冀平台的`课程信息-课件下载`页面给定的文法文档，并结合指导书，设计并实现词法分析程序，程序要能够处理正确的源程序以及错误的源程序。

- 对于**正确的源程序**，需要从源程序中识别出单词，记录其单词类别和单词值，输出**单词类别码**和**单词的字符/字符串形式**。
- 对于**错误的源程序**，需要识别出错误，并输出**错误所在的行号**和**错误的类别码**。

词法分析中，单词的类别码统一定义如下：

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

对于**正确的源程序**，将 **单词类别码** 和 **单词的字符/字符串形式** 按读入顺序输出至 lexer.txt。

```
单词类别码 单词的字符/字符串形式（中间仅用一个空格间隔）
```

对于**错误的源程序**，将 **错误所在的行号**和**错误的类别码** 按行号从小到大输出至 error.txt，行号从1开始编号。

```
错误的行号 错误的类别码（中间仅用一个空格间隔）
```

## **注意事项**

1. 读取的字符串要原样保留着便于输出，特别是数字，这里输出的并不是真正的单词值，其实是读入的字符串，单词值需另行记录。
2. **单词的类别和单词值以及其他关注的信息，在词法分析阶段获取后，后续的分析阶段会使用，请注意记录**。
3. **当前要求的正确源程序的输出只是为了便于评测，完整的编译器最后无需出现这些信息，请设计为方便打开/关闭这些输出的方案**。
4. 本次作业中对于每个错误样例，只会出现词法分析部分的错误。**每个错误样例中有且仅有1个错误**。
5. **即使是一个错误的源程序，也应该完成词法分析的全部任务，这样才能进行语法分析和语义分析部分的错误处理。**
6. **错误类别码见文法最后一部分**。

## **词法分析可能出现的错误**

```
逻辑与表达式 LAndExp → EqExp | LAndExp '&&' EqExp // a

逻辑或表达式 LOrExp → LAndExp | LOrExp '||' LAndExp // a
```

## **样例**

### **正确源程序样例**

样例输入：

```
const int array[2] = {1,2};

int main(){
    int c;
    c = getint();
    printf("output is %d",c);
    return c;
}
```

样例输出：

```
CONSTTK const
INTTK int
IDENFR array
LBRACK [
INTCON 2
RBRACK ]
ASSIGN =
LBRACE {
INTCON 1
COMMA ,
INTCON 2
RBRACE }
SEMICN ;
INTTK int
MAINTK main
LPARENT (
RPARENT )
LBRACE {
INTTK int
IDENFR c
SEMICN ;
IDENFR c
ASSIGN =
GETINTTK getint
LPARENT (
RPARENT )
SEMICN ;
PRINTFTK printf
LPARENT (
STRCON "output is %d"
COMMA ,
IDENFR c
RPARENT )
SEMICN ;
RETURNTK return
IDENFR c
SEMICN ;
RBRACE }
```

### **错误源程序样例**

样例输入：

```
int main(){
    if(1 & 2){
        printf("2024 Compiler\n");
    }
    return 0;
}
```

样例输出：

```
2 a
```

## **评分标准**

- 对于**正确的源程序**，按与预期结果不一致的项数（**每一行单词信息算一项**）扣分，每项扣 **5%**。
- 对于**错误的源程序**，正确报出错误得满分，错报或多报不得分。

## **参考资料**

- 教材 第17章 17.3 ；第18章 18.3.4
- pl0-compiler.doc 中 getsym 及相关子程序（过程）
- pascals-compiler.docx 中 insymbol 及相关子程序（过程）
- 希冀平台 `课程信息-课件下载` 页面相关资料

## **资料下载**



- PL/0、PASCALS编译器源代码可以从希冀平台的 `课程信息-课件下载` 页面中获取。

## **辅助工具**

- 2024词法分析作业公共测试程序库

## **开发语言及环境**

- 用 C/C++/JAVA 实现。
- 机房安装的 C/C++ 开发环境是 CLion 2019.3.6；
- Java 的开发环境为 IDEA 2022.2.2 社区版。
- 集成开发环境的安装见 `在线教程` 中的编译技术实验教程

## **文档要求**

完成参考编译器介绍、总体设计、词法分析阶段设计文档。

## **提交形式**

- 评测机所采用的编译学生代码的版本是：`CMake 3.28.3，C/C++ clang 12.0.0 C++17，Java jdk 17`。

- 上传请将所开发的词法分析程序的源文件（.cpp/.c/.h/.Java，不含工程文件）打包为 zip 后提交。

- **对于使用 Java 开发的编译器**，程序运行的入口为 Compiler.java 中的 main 方法。**提交时请直接Compiler.java打包在压缩包的最上层，不要嵌套一层 src 文件夹**。平台 **不支持 Java 第三方包**。

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
  └── frontend
           └── Lexer.java
  ```


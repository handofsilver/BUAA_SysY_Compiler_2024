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
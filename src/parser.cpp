#include "Parser.h"
#include "AST.h"
#include "Lexer.h"

Parser::Parser(Lexer& lexer) : lexer_(lexer) {}

/** Entry point: CompUnit → {Decl} {FuncDef} MainFuncDef. */
std::unique_ptr<CompUnit> Parser::ParseCompUnit() {
    std::vector<std::unique_ptr<Decl>> decls;
    std::vector<std::unique_ptr<FuncDef>> func_defs;
    std::unique_ptr<MainFuncDef> main_func_def;

    while (CurIs(TokenType::CONSTTK) ||
           ((CurIs(TokenType::INTTK) || CurIs(TokenType::CHARTK)) &&
            LookaheadIs(TokenType::IDENFR) && !Lookahead2Is(TokenType::LPARENT))) {
        decls.push_back(ParseDecl());
    }

    while ((CurIs(TokenType::INTTK) || CurIs(TokenType::CHARTK) || CurIs(TokenType::VOIDTK)) &&
           LookaheadIs(TokenType::IDENFR) && Lookahead2Is(TokenType::LPARENT)) {
        func_defs.push_back(ParseFuncDef());
    }

    main_func_def = ParseMainFuncDef();

    return std::make_unique<CompUnit>(std::move(decls), std::move(func_defs),
                                      std::move(main_func_def));
}

// -------------------------------------------------------------------------
// Declarations
// -------------------------------------------------------------------------
/** Decl → ConstDecl | VarDecl. */
std::unique_ptr<Decl> Parser::ParseDecl() {
    if (CurIs(TokenType::CONSTTK)) {
        return ParseConstDecl();
    } else if (CurIs(TokenType::INTTK) || CurIs(TokenType::CHARTK)) {
        return ParseVarDecl();
    } else {
        RecordError(Cur()->line_num, "?");
        return nullptr;
    }
}

/** ConstDecl → 'const' BType ConstDef { ',' ConstDef } ';'. */
std::unique_ptr<ConstDecl> Parser::ParseConstDecl() {
    Advance();
    BType b_type = ParseBType();
    std::vector<std::unique_ptr<ConstDef>> const_defs;
    const_defs.push_back(ParseConstDef());
    while (CurIs(TokenType::COMMA)) {
        Advance();
        const_defs.push_back(ParseConstDef());
    }
    ExpectSemicolon();
    return std::make_unique<ConstDecl>(b_type, const_defs);
}

/** VarDecl → BType VarDef { ',' VarDef } ';'. */
std::unique_ptr<VarDecl> Parser::ParseVarDecl() {
    BType b_type = ParseBType();
    std::vector<std::unique_ptr<VarDef>> var_defs;
    var_defs.push_back(ParseVarDef());
    while (CurIs(TokenType::COMMA)) {
        Advance();
        var_defs.push_back(ParseVarDef());
    }
    ExpectSemicolon();
    return std::make_unique<VarDecl>(b_type, var_defs);
}

// -------------------------------------------------------------------------
// Function definitions
// -------------------------------------------------------------------------
/** FuncDef → FuncType Ident '(' [FuncFParams] ')' Block. */
std::unique_ptr<FuncDef> Parser::ParseFuncDef() {
    BType b_type = ParseFuncType();
    std::string ident = Cur()->value;
    Advance();
    Advance();
    std::vector<std::unique_ptr<FuncFParam>> func_f_params = ParseFuncFParams();
    ExpectRightParen();
    std::unique_ptr<Block> block = ParseBlock();
    return std::make_unique<FuncDef>(b_type, ident, std::move(func_f_params), std::move(block));
}

/** MainFuncDef → 'int' 'main' '(' ')' Block. */
std::unique_ptr<MainFuncDef> Parser::ParseMainFuncDef() {
    Advance();
    Advance();
    Advance();
    ExpectRightParen();

    std::unique_ptr<Block> block = ParseBlock();
    return std::make_unique<MainFuncDef>(std::move(block));
}

// -------------------------------------------------------------------------
// Block and block items
// -------------------------------------------------------------------------
/** Block → '{' { BlockItem } '}'. */
std::unique_ptr<Block> Parser::ParseBlock() {
    Advance();
    std::vector<std::unique_ptr<BlockItem>> block_items;
    while (!CurIs(TokenType::RBRACE)) {
        block_items.push_back(ParseBlockItem());
    }
    Advance();
    return std::make_unique<Block>(std::move(block_items));
}

/** BlockItem → Decl | Stmt. */
std::unique_ptr<BlockItem> Parser::ParseBlockItem() {
    if (CurIs(TokenType::CONSTTK) || CurIs(TokenType::INTTK) || CurIs(TokenType::CHARTK)) {
        return ParseDecl();
    } else {
        return ParseStmt();
    }
}

// -------------------------------------------------------------------------
// Statements
// -------------------------------------------------------------------------
/** Dispatches to the appropriate Stmt production. */
std::unique_ptr<Stmt> Parser::ParseStmt() {
    if (CurIs(TokenType::LBRACE)) {
        return ParseBlockStmt();
    } else if (CurIs(TokenType::IFTK)) {
        return ParseIfStmt();
    } else if (CurIs(TokenType::FORTK)) {
        return ParseForStmt();
    } else if (CurIs(TokenType::BREAKTK)) {
        return ParseBreakStmt();
    } else if (CurIs(TokenType::CONTINUETK)) {
        return ParseContinueStmt();
    } else if (CurIs(TokenType::RETURNTK)) {
        return ParseReturnStmt();
    } else if (CurIs(TokenType::PRINTFTK)) {
        return ParsePrintfStmt();
    } else {
        return ParseOtherStmt();
    }
}

/** ForInitOrStep → LVal '=' Exp (used in for-loop init/step). */
std::unique_ptr<ForInitOrStep> Parser::ParseForInitOrStep() {
    std::unique_ptr<LVal> lval = ParseLVal();
    Advance();
    std::unique_ptr<Exp> exp = ParseExp();
    return std::make_unique<ForInitOrStep>(std::move(lval), std::move(exp));
}

/** BlockStmt → Block */
std::unique_ptr<BlockStmt> Parser::ParseBlockStmt() {
    return std::make_unique<BlockStmt>(ParseBlock());
}

/** IfStmt → 'if' '(' Cond ')' Stmt [ 'else' Stmt ] */
std::unique_ptr<IfStmt> Parser::ParseIfStmt() {
    Advance();
    Advance();
    std::unique_ptr<Exp> cond = ParseCond();
    ExpectRightParen();
    std::unique_ptr<Stmt> then_stmt = ParseStmt();

    if (CurIs(TokenType::ELSETK)) {
        Advance();
        std::unique_ptr<Stmt> else_stmt = ParseStmt();
        return std::make_unique<IfStmt>(std::move(cond), std::move(then_stmt),
                                        std::make_optional(std::move(else_stmt)));
    } else {
        return std::make_unique<IfStmt>(std::move(cond), std::move(then_stmt), std::nullopt);
    }
}

/** ForStmt → 'for' '(' [ForInit] ';' [Cond] ';' [ForStep] ')' Stmt */
std::unique_ptr<ForStmt> Parser::ParseForStmt() {
    Advance();
    Advance();
    std::optional<std::unique_ptr<ForInitOrStep>> init = ParseForInitOrStep();
    ExpectSemicolon();
    std::optional<std::unique_ptr<Exp>> cond = ParseCond();
    ExpectSemicolon();
    std::optional<std::unique_ptr<ForInitOrStep>> step = ParseForInitOrStep();
    ExpectRightParen();
    std::unique_ptr<Stmt> body = ParseStmt();
    return std::make_unique<ForStmt>(std::move(init), std::move(cond), std::move(step),
                                     std::move(body));
}

/** BreakStmt → 'break' ';' */
std::unique_ptr<BreakStmt> Parser::ParseBreakStmt() {
    Advance();
    ExpectSemicolon();
    return std::make_unique<BreakStmt>();
}

/** ContinueStmt → 'continue' ';' */
std::unique_ptr<ContinueStmt> Parser::ParseContinueStmt() {
    Advance();
    ExpectSemicolon();
    return std::make_unique<ContinueStmt>();
}

/** ReturnStmt → 'return' [Exp] ';' */
std::unique_ptr<ReturnStmt> Parser::ParseReturnStmt() {
    Advance();
    if (CurIs(TokenType::SEMICN)) {
        Advance();
        return std::make_unique<ReturnStmt>(std::nullopt);
    } else {
        std::unique_ptr<Exp> exp = ParseExp();
        ExpectSemicolon();
        return std::make_unique<ReturnStmt>(std::move(exp));
    }
}

/** PrintfStmt → 'printf''('StringConst {','Exp}')'';' */
std::unique_ptr<PrintfStmt> Parser::ParsePrintfStmt() {
    Advance();
    Advance();
    std::string format_string = Cur()->value;
    Advance();

    std::vector<std::unique_ptr<Exp>> exps;
    while (CurIs(TokenType::COMMA)) {
        Advance();
        exps.push_back(ParseExp());
    }
    ExpectRightParen();
    ExpectSemicolon();
    return std::make_unique<PrintfStmt>(format_string, std::move(exps));
}

/** ExpStmt → [Exp] ';' */
/** GetintStmt → LVal '=' 'getint''('')'';' */
/** GetcharStmt → LVal '=' 'getchar''('')'';' */
/** AssignStmt → LVal '=' Exp ';' */
std::unique_ptr<Stmt> Parser::ParseOtherStmt() {
    if (CurIs(TokenType::SEMICN)) {
        Advance();
        return std::make_unique<ExpStmt>(std::nullopt);
    }

    std::unique_ptr<LVal> lval = ParseLVal();
    if (CurIs(TokenType::ASSIGN)) {
        Advance();
        if (CurIs(TokenType::GETINTTK)) {
            Advance();
            Advance();
            ExpectRightParen();
            ExpectSemicolon();
            return std::make_unique<GetintStmt>(std::move(lval));
        } else if (CurIs(TokenType::GETCHARTK)) {
            Advance();
            Advance();
            ExpectRightParen();
            ExpectSemicolon();
            return std::make_unique<GetcharStmt>(std::move(lval));
        } else {
            std::unique_ptr<Exp> exp = ParseExp();
            ExpectSemicolon();
            return std::make_unique<AssignStmt>(std::move(lval), std::move(exp));
        }
    } else {
        // parsing rest of the expression
        if (CurIs(TokenType::SEMICN)) {
            Advance();
            return std::make_unique<ExpStmt>(std::nullopt);
        } else {
            OpType op = GetOperatorType(Cur()->value);
            Advance();
            std::unique_ptr<Exp> exp = ParseExp();
            ExpectSemicolon();

            std::unique_ptr<BinaryExp> binary_exp =
                std::make_unique<BinaryExp>(std::move(lval), std::move(exp), op);

            return std::make_unique<ExpStmt>(std::make_optional(std::move(binary_exp)));
        }
    }
}

// -------------------------------------------------------------------------
// Expressions (public API: Exp and primary layer)
// -------------------------------------------------------------------------
/** Exp → AddExp. Top-level expression. */
std::unique_ptr<Exp> Parser::ParseExp() {
    return ParseAddExp();
}

/** Cond → LOrExp. Used in if/for conditions. */
std::unique_ptr<Exp> Parser::ParseCond() {
    return ParseLOrExp();
}

/** LVal → Ident ['[' Exp ']']. */
std::unique_ptr<LVal> Parser::ParseLVal() {
    std::string ident = Cur()->value;
    Advance();
    if (CurIs(TokenType::LBRACK)) {
        Advance();
        std::unique_ptr<Exp> exp = ParseExp();
        ExpectRightBracket();
        return std::make_unique<LVal>(ident, std::move(exp));
    } else {
        return std::make_unique<LVal>(ident);
    }
}

/** PrimaryExp → '(' Exp ')' | LVal | Number | Character. */
std::unique_ptr<Exp> Parser::ParsePrimaryExp() {
    if (CurIs(TokenType::INTCON)) {
        return ParseNumber();
    } else if (CurIs(TokenType::CHRCON)) {
        return ParseCharacter();
    } else if (CurIs(TokenType::LPARENT)) {
        Advance();
        std::unique_ptr<Exp> exp = ParseExp();
        ExpectRightParen();
        return exp;
    } else if (CurIs(TokenType::IDENFR)) {
        return ParseLVal();
    } else {
        RecordError(Cur()->line_num, "?");
        return nullptr;
    }
}

/** Enable/disable emission of token and syntax lines to parser_out (e.g. parser.txt). */
void SetParserOutput(std::ostream* out);
void SetEmitParserOutput(bool enable);

// -------------------------------------------------------------------------
// Token helpers
// -------------------------------------------------------------------------

/** Record a syntax error (line, code) and optionally synchronize; parsing continues. */
void Parser::RecordError(int line, std::string code) {
    error_log_.push_back(std::make_pair(line, code));
}

bool Parser::LookaheadIs(TokenType t) {
    std::optional<Token> next = lexer_.PeekNext();
    return next.has_value() && next->type == t;
}

bool Parser::Lookahead2Is(TokenType t) {
    std::optional<Token> next2 = lexer_.PeekNext2();
    return next2.has_value() && next2->type == t;
}

/** Emit current token to parser_out_ if enabled. */
void Parser::EmitToken() {
    if (emit_parser_output_) {
        *parser_out_ << Cur()->value << std::endl;
    }
}

/** Emit a syntax component name like "<CompUnit>" if enabled. */
void Parser::EmitSyntax(std::string_view name) {
    if (emit_parser_output_) {
        *parser_out_ << name << std::endl;
    }
}

// -------------------------------------------------------------------------
// Expect / consume helpers (TODO: implement; record error if missing)
// -------------------------------------------------------------------------
/** Expect ';', consume if present, else record error 'i'. */
void Parser::ExpectSemicolon() {
    if (CurIs(TokenType::SEMICN)) {
        Advance();
    } else {
        RecordError(Cur()->line_num, "i");
    }
}

/** Expect ')', consume if present, else record error 'j'. */
void Parser::ExpectRightParen() {
    if (CurIs(TokenType::RPARENT)) {
        Advance();
    } else {
        RecordError(Cur()->line_num, "j");
    }
}

/** Expect ']', consume if present, else record error 'k'. */
void Parser::ExpectRightBracket() {
    if (CurIs(TokenType::RBRACK)) {
        Advance();
    } else {
        RecordError(Cur()->line_num, "k");
    }
}

// -------------------------------------------------------------------------
// Type and params
// -------------------------------------------------------------------------
/** BType → 'int' | 'char'. */
BType Parser::ParseBType() {
    if (CurIs(TokenType::INTTK)) {
        Advance();
        return BType::INT;
    } else if (CurIs(TokenType::CHARTK)) {
        Advance();
        return BType::CHAR;
    }
}

/** FuncType → 'void' | 'int' | 'char'. */
BType Parser::ParseFuncType() {
    if (CurIs(TokenType::VOIDTK)) {
        Advance();
        return BType::VOID;
    } else if (CurIs(TokenType::INTTK)) {
        Advance();
        return BType::INT;
    } else if (CurIs(TokenType::CHARTK)) {
        Advance();
        return BType::CHAR;
    }
}

/** FuncFParams → FuncFParam { ',' FuncFParam }. */
std::vector<std::unique_ptr<FuncFParam>> ParseFuncFParams();
/** FuncFParam → BType Ident ['[' ']']. */
std::unique_ptr<FuncFParam> ParseFuncFParam();

// -------------------------------------------------------------------------
// Def and init values
// -------------------------------------------------------------------------
/** ConstDef → Ident [ '[' ConstExp ']' ] '=' ConstInitVal. */
std::unique_ptr<ConstDef> ParseConstDef();
/** VarDef → Ident [ '[' ConstExp ']' ] [ '=' InitVal ]. */
std::unique_ptr<VarDef> ParseVarDef();
/** ConstInitVal → ConstExp | '{' ... '}' | StringConst. */
std::unique_ptr<ConstInitVal> ParseConstInitVal();
/** InitVal → Exp | '{' ... '}' | StringConst. */
std::unique_ptr<InitVal> ParseInitVal();
/** ConstExp → AddExp (constant context). */
std::unique_ptr<ConstExp> Parser::ParseConstExp() {
    return std::make_unique<ConstExp>(ParseAddExp());
}

// -------------------------------------------------------------------------
// Expression layers (all return unique_ptr<Exp> for AST uniformity)
// Grammar has separate non-terminals (AddExp, MulExp, ...) for precedence.
// We keep separate Parse* methods to implement precedence in recursive
// descent: ParseAddExp calls ParseMulExp and loops on '+'/'-'; ParseMulExp
// calls ParseUnaryExp and loops on '*'/'/'/'%'; etc. No separate AST
// node types for AddExp/MulExp—they become BinaryExp with the right OpType.
// -------------------------------------------------------------------------
/** AddExp → MulExp | AddExp ('+' | '−') MulExp. */
std::unique_ptr<Exp> Parser::ParseAddExp() {
    std::stack<std::unique_ptr<Exp>> exp_stack;
    std::stack<OpType> op_stack;
    exp_stack.push(ParseMulExp());

    while (CurIs(TokenType::PLUS) || CurIs(TokenType::MINU)) {
        OpType op_type = GetOperatorType(Cur()->value);
        op_stack.push(op_type);
        Advance();
        exp_stack.push(ParseMulExp());
    }

    return ConstructExpFromElements(exp_stack, op_stack);
}

/** MulExp → UnaryExp | MulExp ('*' | '/' | '%') UnaryExp. */
std::unique_ptr<Exp> Parser::ParseMulExp() {
    std::stack<std::unique_ptr<Exp>> exp_stack;
    std::stack<OpType> op_stack;
    exp_stack.push(ParseUnaryExp());

    while (CurIs(TokenType::MULT) || CurIs(TokenType::DIV) || CurIs(TokenType::MOD)) {
        OpType op_type = GetOperatorType(Cur()->value);
        op_stack.push(op_type);
        Advance();
        exp_stack.push(ParseUnaryExp());
    }

    return ConstructExpFromElements(exp_stack, op_stack);
}

std::unique_ptr<Exp> Parser::ConstructExpFromElements(std::stack<std::unique_ptr<Exp>>& exp_stack,
                                                      std::stack<OpType>& op_stack) {
    std::unique_ptr<Exp> rhs = std::move(exp_stack.top());
    exp_stack.pop();

    // if there is only one element in the stack, return it
    if (exp_stack.empty()) {
        return std::move(rhs);
    }

    // if there is more than one element in the stack, construct a binary expression
    OpType op = op_stack.top();
    op_stack.pop();

    std::unique_ptr<Exp> lhs = ConstructExpFromElements(exp_stack, op_stack);
    return std::make_unique<BinaryExp>(std::move(lhs), std::move(rhs), op);
}

/** UnaryExp → PrimaryExp | Ident '(' [FuncRParams] ')' | UnaryOp UnaryExp. */
std::unique_ptr<Exp> Parser::ParseUnaryExp() {
    if (CurIs(TokenType::IDENFR) && LookaheadIs(TokenType::LPARENT)) {
        std::string ident = Cur()->value;
        Advance();
        Advance();
        std::unique_ptr<FuncRParams> func_r_params = ParseFuncRParams();
        ExpectRightParen();
        return std::make_unique<FuncCall>(ident, std::move(func_r_params));
    } else if (CurIs(TokenType::PLUS) || CurIs(TokenType::MINU) || CurIs(TokenType::NOT)) {
        OpType op = ParseUnaryOp();
        std::unique_ptr<Exp> exp = ParseUnaryExp();
        return std::make_unique<UnaryExp>(std::move(exp), op);
    } else {
        return ParsePrimaryExp();
    }
}

/** RelExp → AddExp | RelExp ('<' | '>' | '<=' | '>=') AddExp. */
std::unique_ptr<Exp> Parser::ParseRelExp() {
    std::stack<std::unique_ptr<Exp>> exp_stack;
    std::stack<OpType> op_stack;
    exp_stack.push(ParseAddExp());

    while (CurIs(TokenType::LSS) || CurIs(TokenType::GRE) || CurIs(TokenType::LEQ) ||
           CurIs(TokenType::GEQ)) {
        OpType op_type = GetOperatorType(Cur()->value);
        op_stack.push(op_type);
        Advance();
        exp_stack.push(ParseAddExp());
    }

    return ConstructExpFromElements(exp_stack, op_stack);
}

/** EqExp → RelExp | EqExp ('==' | '!=') RelExp. */
std::unique_ptr<Exp> Parser::ParseEqExp() {
    std::stack<std::unique_ptr<Exp>> exp_stack;
    std::stack<OpType> op_stack;
    exp_stack.push(ParseRelExp());

    while (CurIs(TokenType::EQL) || CurIs(TokenType::NEQ)) {
        OpType op_type = GetOperatorType(Cur()->value);
        op_stack.push(op_type);
        Advance();
        exp_stack.push(ParseRelExp());
    }
    return ConstructExpFromElements(exp_stack, op_stack);
}
/** LAndExp → EqExp | LAndExp '&&' EqExp. */
std::unique_ptr<Exp> Parser::ParseLAndExp() {
    std::stack<std::unique_ptr<Exp>> exp_stack;
    std::stack<OpType> op_stack;
    exp_stack.push(ParseEqExp());

    while (CurIs(TokenType::AND)) {
        OpType op_type = GetOperatorType(Cur()->value);
        op_stack.push(op_type);
        Advance();
        exp_stack.push(ParseEqExp());
    }

    return ConstructExpFromElements(exp_stack, op_stack);
}

/** LOrExp → LAndExp | LOrExp '||' LAndExp. */
std::unique_ptr<Exp> Parser::ParseLOrExp() {
    std::stack<std::unique_ptr<Exp>> exp_stack;
    std::stack<OpType> op_stack;
    exp_stack.push(ParseLAndExp());

    while (CurIs(TokenType::OR)) {
        OpType op_type = GetOperatorType(Cur()->value);
        op_stack.push(op_type);
        Advance();
        exp_stack.push(ParseLAndExp());
    }

    return ConstructExpFromElements(exp_stack, op_stack);
}

/** Number → IntConst. Returns Number (Exp). */
std::unique_ptr<Exp> Parser::ParseNumber() {
    if (CurIs(TokenType::INTCON)) {
        int int_const = std::stoi(Cur()->value);
        Advance();
        return std::make_unique<Number>(int_const);
    } else {
        RecordError(Cur()->line_num, "?");
        return nullptr;
    }
}

/** Character → CharConst. Returns Character (Exp). */
std::unique_ptr<Exp> Parser::ParseCharacter() {
    if (CurIs(TokenType::CHRCON)) {
        char char_const = Cur()->value[0];
        Advance();
        return std::make_unique<Character>(char_const);
    } else {
        RecordError(Cur()->line_num, "?");
        return nullptr;
    }
}

/** UnaryOp → '+' | '−' | '!'. Consumes token and returns OpType. */
OpType Parser::ParseUnaryOp() {
    if (CurIs(TokenType::PLUS)) {
        Advance();
        return OpType::PLUS;
    } else if (CurIs(TokenType::MINU)) {
        Advance();
        return OpType::MINU;
    } else if (CurIs(TokenType::NOT)) {
        Advance();
        return OpType::NOT;
    } else {
        RecordError(Cur()->line_num, "?");
        return OpType::NONE;
    }
}

/** FuncRParams → Exp { ',' Exp }. */
std::unique_ptr<FuncRParams> Parser::ParseFuncRParams() {
    std::vector<std::unique_ptr<Exp>> exps;
    exps.push_back(ParseExp());
    while (CurIs(TokenType::COMMA)) {
        Advance();
        exps.push_back(ParseExp());
    }
    return std::make_unique<FuncRParams>(exps);
}
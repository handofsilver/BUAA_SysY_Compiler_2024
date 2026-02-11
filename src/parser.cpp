#include "Parser.h"
#include "AST.h"
#include "Lexer.h"
#include "TokenType.h"
#include <unordered_map>

OpType GetOperatorType(std::string_view op) {
    static const std::unordered_map<std::string, OpType> operator_map = {
        {"+",  OpType::ADD },
        {"-",  OpType::SUB },
        {"*",  OpType::MUL },
        {"/",  OpType::DIV },
        {"%",  OpType::MOD },
        {"<",  OpType::LT  },
        {">",  OpType::GT  },
        {"<=", OpType::LE  },
        {">=", OpType::GE  },
        {"==", OpType::EQ  },
        {"!=", OpType::NE  },
        {"&&", OpType::AND },
        {"||", OpType::OR  },
        {"!",  OpType::NOT },
        {"+",  OpType::PLUS},
        {"-",  OpType::MINU},
    };
    auto it = operator_map.find(std::string(op));
    if (it != operator_map.end()) {
        return it->second;
    }
    return OpType::NONE;
}

Parser::Parser(Lexer& lexer) : lexer_(lexer) {}

// -------------------------------------------------------------------------
// Expect and error recording
// -------------------------------------------------------------------------

void Parser::Advance() {
    if (emit_parser_output_ && parser_out_ && Cur().has_value()) {
        *parser_out_ << ToString(Cur()->type) << " " << Cur()->value << "\n";
    }
    if (Cur().has_value()) {
        last_consumed_line_ = Cur()->line_num;
    }
    lexer_.Next();
}

void Parser::RecordError(int line, const std::string& code) {
    if (code.empty() || code == "?") {
        return;
    }
    error_log_.push_back(std::make_pair(line, code));
}

void Parser::Expect(TokenType type, const std::string& error_code) {
    if (CurIs(type)) {
        Advance();
        return;
    }
    if (!error_code.empty()) {
        RecordError(last_consumed_line_, error_code);
    }
}

void Parser::ExpectSemicolon() {
    Expect(TokenType::SEMICN, "i");
}

void Parser::ExpectRightParen() {
    Expect(TokenType::RPARENT, "j");
}

void Parser::ExpectRightBracket() {
    Expect(TokenType::RBRACK, "k");
}

// -------------------------------------------------------------------------
// Token helpers
// -------------------------------------------------------------------------

bool Parser::LookaheadIs(TokenType t) {
    std::optional<Token> next = lexer_.PeekNext();
    return next.has_value() && next->type == t;
}

bool Parser::Lookahead2Is(TokenType t) {
    std::optional<Token> next2 = lexer_.PeekNext2();
    return next2.has_value() && next2->type == t;
}

void Parser::EmitToken() {
    if (emit_parser_output_ && parser_out_ && Cur().has_value()) {
        *parser_out_ << Cur()->value << std::endl;
    }
}

void Parser::EmitSyntax(std::string_view name) {
    if (emit_parser_output_) {
        *parser_out_ << name << std::endl;
    }
}

// -------------------------------------------------------------------------
// CompUnit
// -------------------------------------------------------------------------

/** Entry point: CompUnit -> {Decl} {FuncDef} MainFuncDef. */
std::unique_ptr<CompUnit> Parser::ParseCompUnit() {
    Advance(); // Prime: load first token (Lexer starts with cur_token_ empty)
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

    EmitSyntax("<CompUnit>");
    return std::make_unique<CompUnit>(std::move(decls), std::move(func_defs),
                                      std::move(main_func_def));
}

// -------------------------------------------------------------------------
// Declarations
// -------------------------------------------------------------------------

/** Decl -> ConstDecl | VarDecl. */
std::unique_ptr<Decl> Parser::ParseDecl() {
    if (CurIs(TokenType::CONSTTK)) {
        return ParseConstDecl();
    }
    if (CurIs(TokenType::INTTK) || CurIs(TokenType::CHARTK)) {
        return ParseVarDecl();
    }
    if (Cur().has_value()) {
        RecordError(Cur()->line_num, "?");
    }
    return nullptr;
}

/** ConstDecl -> 'const' BType ConstDef { ',' ConstDef } ';'. */
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
    EmitSyntax("<ConstDecl>");
    return std::make_unique<ConstDecl>(b_type, std::move(const_defs));
}

/** VarDecl -> BType VarDef { ',' VarDef } ';'. */
std::unique_ptr<VarDecl> Parser::ParseVarDecl() {
    BType b_type = ParseBType();
    std::vector<std::unique_ptr<VarDef>> var_defs;
    var_defs.push_back(ParseVarDef());
    while (CurIs(TokenType::COMMA)) {
        Advance();
        var_defs.push_back(ParseVarDef());
    }
    ExpectSemicolon();
    EmitSyntax("<VarDecl>");
    return std::make_unique<VarDecl>(b_type, std::move(var_defs));
}

// -------------------------------------------------------------------------
// Def and init values
// -------------------------------------------------------------------------

/** ConstDef -> Ident [ '[' ConstExp ']' ] '=' ConstInitVal. */
std::unique_ptr<ConstDef> Parser::ParseConstDef() {
    std::string ident = Cur()->value;
    Expect(TokenType::IDENFR, "");
    std::vector<std::unique_ptr<ConstExp>> dims;
    if (CurIs(TokenType::LBRACK)) {
        Advance();
        dims.push_back(ParseConstExp());
        ExpectRightBracket();
    }
    Expect(TokenType::ASSIGN, "");
    std::unique_ptr<ConstInitVal> const_init_val = ParseConstInitVal();
    EmitSyntax("<ConstDef>");
    return std::make_unique<ConstDef>(std::move(ident), std::move(dims), std::move(const_init_val));
}

/** VarDef -> Ident [ '[' ConstExp ']' ] [ '=' InitVal ]. */
std::unique_ptr<VarDef> Parser::ParseVarDef() {
    std::string ident = Cur()->value;
    Expect(TokenType::IDENFR, "");
    std::vector<std::unique_ptr<ConstExp>> dims;
    if (CurIs(TokenType::LBRACK)) {
        Advance();
        dims.push_back(ParseConstExp());
        ExpectRightBracket();
    }
    std::unique_ptr<InitVal> init_val;
    if (CurIs(TokenType::ASSIGN)) {
        Advance();
        init_val = ParseInitVal();
    }
    EmitSyntax("<VarDef>");
    return std::make_unique<VarDef>(std::move(ident), std::move(dims), std::move(init_val));
}

/** ConstInitVal -> ConstExp | '{' [ ConstExp { ',' ConstExp } ] '}' | StringConst. */
std::unique_ptr<ConstInitVal> Parser::ParseConstInitVal() {
    if (CurIs(TokenType::STRCON)) {
        std::string s = Cur()->value;
        Advance();
        EmitSyntax("<ConstInitVal>");
        return std::make_unique<ConstInitVal>(std::move(s));
    }
    if (CurIs(TokenType::LBRACE)) {
        Advance();
        std::vector<std::unique_ptr<ConstExp>> list;
        if (!CurIs(TokenType::RBRACE)) {
            list.push_back(ParseConstExp());
            while (CurIs(TokenType::COMMA)) {
                Advance();
                list.push_back(ParseConstExp());
            }
        }
        Expect(TokenType::RBRACE, "");
        EmitSyntax("<ConstInitVal>");
        return std::make_unique<ConstInitVal>(std::move(list));
    }
    EmitSyntax("<ConstInitVal>");
    return std::make_unique<ConstInitVal>(ParseConstExp());
}

/** InitVal -> Exp | '{' [ Exp { ',' Exp } ] '}' | StringConst. */
std::unique_ptr<InitVal> Parser::ParseInitVal() {
    if (CurIs(TokenType::STRCON)) {
        std::string s = Cur()->value;
        Advance();
        EmitSyntax("<InitVal>");
        return std::make_unique<InitVal>(std::move(s));
    }
    if (CurIs(TokenType::LBRACE)) {
        Advance();
        std::vector<std::unique_ptr<Exp>> list;
        if (!CurIs(TokenType::RBRACE)) {
            list.push_back(ParseExp());
            while (CurIs(TokenType::COMMA)) {
                Advance();
                list.push_back(ParseExp());
            }
        }
        Expect(TokenType::RBRACE, "");
        EmitSyntax("<InitVal>");
        return std::make_unique<InitVal>(std::move(list));
    }
    EmitSyntax("<InitVal>");
    return std::make_unique<InitVal>(ParseExp());
}

/** ConstExp -> AddExp (constant context). */
std::unique_ptr<ConstExp> Parser::ParseConstExp() {
    std::unique_ptr<Exp> inner = ParseAddExp();
    EmitSyntax("<ConstExp>");
    return std::make_unique<ConstExp>(std::move(inner));
}

// -------------------------------------------------------------------------
// Type and params
// -------------------------------------------------------------------------

/** BType -> 'int' | 'char'. */
BType Parser::ParseBType() {
    if (CurIs(TokenType::INTTK)) {
        Advance();
        return BType::INT;
    }
    if (CurIs(TokenType::CHARTK)) {
        Advance();
        return BType::CHAR;
    }
    if (Cur().has_value()) {
        RecordError(Cur()->line_num, "?");
    }
    return BType::INT;
}

/** FuncType -> 'void' | 'int' | 'char'. */
BType Parser::ParseFuncType() {
    if (CurIs(TokenType::VOIDTK)) {
        Advance();
        return BType::VOID;
    }
    if (CurIs(TokenType::INTTK)) {
        Advance();
        return BType::INT;
    }
    if (CurIs(TokenType::CHARTK)) {
        Advance();
        return BType::CHAR;
    }
    if (Cur().has_value()) {
        RecordError(Cur()->line_num, "?");
    }
    return BType::INT;
}

/** FuncFParams -> FuncFParam { ',' FuncFParam }. */
std::vector<std::unique_ptr<FuncFParam>> Parser::ParseFuncFParams() {
    std::vector<std::unique_ptr<FuncFParam>> params;
    if (CurIs(TokenType::RPARENT)) {
        EmitSyntax("<FuncFParams>");
        return params;
    }
    params.push_back(ParseFuncFParam());
    while (CurIs(TokenType::COMMA)) {
        Advance();
        params.push_back(ParseFuncFParam());
    }
    EmitSyntax("<FuncFParams>");
    return params;
}

/** FuncFParam -> BType Ident ['[' ']']. */
std::unique_ptr<FuncFParam> Parser::ParseFuncFParam() {
    BType btype = ParseBType();
    std::string ident = Cur()->value;
    Expect(TokenType::IDENFR, "");
    bool is_array = false;
    if (CurIs(TokenType::LBRACK)) {
        Advance();
        Expect(TokenType::RBRACK, "k");
        is_array = true;
    }
    EmitSyntax("<FuncFParam>");
    return std::make_unique<FuncFParam>(btype, std::move(ident), is_array);
}

// -------------------------------------------------------------------------
// Function definitions
// -------------------------------------------------------------------------

/** FuncDef -> FuncType Ident '(' [FuncFParams] ')' Block. */
std::unique_ptr<FuncDef> Parser::ParseFuncDef() {
    BType b_type = ParseFuncType();
    std::string ident = Cur()->value;
    Expect(TokenType::IDENFR, "");
    Expect(TokenType::LPARENT, "");
    std::vector<std::unique_ptr<FuncFParam>> func_f_params = ParseFuncFParams();
    ExpectRightParen();
    std::unique_ptr<Block> block = ParseBlock();
    EmitSyntax("<FuncDef>");
    return std::make_unique<FuncDef>(b_type, std::move(ident), std::move(func_f_params),
                                     std::move(block));
}

/** MainFuncDef -> 'int' 'main' '(' ')' Block. */
std::unique_ptr<MainFuncDef> Parser::ParseMainFuncDef() {
    Expect(TokenType::INTTK, "");
    Expect(TokenType::MAINTK, "");
    Expect(TokenType::LPARENT, "");
    ExpectRightParen();
    std::unique_ptr<Block> block = ParseBlock();
    EmitSyntax("<MainFuncDef>");
    return std::make_unique<MainFuncDef>(std::move(block));
}

// -------------------------------------------------------------------------
// Block and block items
// -------------------------------------------------------------------------

/** Block -> '{' { BlockItem } '}'. */
std::unique_ptr<Block> Parser::ParseBlock() {
    Expect(TokenType::LBRACE, "");
    std::vector<std::unique_ptr<BlockItem>> block_items;
    while (Cur().has_value() && !CurIs(TokenType::RBRACE)) {
        block_items.push_back(ParseBlockItem());
    }
    Expect(TokenType::RBRACE, "");
    EmitSyntax("<Block>");
    return std::make_unique<Block>(std::move(block_items));
}

/** BlockItem -> Decl | Stmt. */
std::unique_ptr<BlockItem> Parser::ParseBlockItem() {
    if (CurIs(TokenType::CONSTTK) || CurIs(TokenType::INTTK) || CurIs(TokenType::CHARTK)) {
        return ParseDecl();
    }
    return ParseStmt();
}

// -------------------------------------------------------------------------
// Statements
// -------------------------------------------------------------------------

/** Dispatches to the appropriate Stmt production. */
std::unique_ptr<Stmt> Parser::ParseStmt() {
    if (CurIs(TokenType::LBRACE)) {
        return ParseBlockStmt();
    }
    if (CurIs(TokenType::IFTK)) {
        return ParseIfStmt();
    }
    if (CurIs(TokenType::FORTK)) {
        return ParseForStmt();
    }
    if (CurIs(TokenType::BREAKTK)) {
        return ParseBreakStmt();
    }
    if (CurIs(TokenType::CONTINUETK)) {
        return ParseContinueStmt();
    }
    if (CurIs(TokenType::RETURNTK)) {
        return ParseReturnStmt();
    }
    if (CurIs(TokenType::PRINTFTK)) {
        return ParsePrintfStmt();
    }
    return ParseOtherStmt();
}

/** ForInitOrStep -> LVal '=' Exp (used in for-loop init/step). */
std::unique_ptr<ForInitOrStep> Parser::ParseForInitOrStep() {
    std::unique_ptr<LVal> lval = ParseLVal();
    Expect(TokenType::ASSIGN, "");
    std::unique_ptr<Exp> exp = ParseExp();
    return std::make_unique<ForInitOrStep>(std::move(lval), std::move(exp));
}

/** BlockStmt -> Block */
std::unique_ptr<BlockStmt> Parser::ParseBlockStmt() {
    std::unique_ptr<Block> block = ParseBlock();
    EmitSyntax("<Stmt>");
    return std::make_unique<BlockStmt>(std::move(block));
}

/** IfStmt -> 'if' '(' Cond ')' Stmt [ 'else' Stmt ] */
std::unique_ptr<IfStmt> Parser::ParseIfStmt() {
    Advance();
    Expect(TokenType::LPARENT, "");
    std::unique_ptr<Exp> cond = ParseCond();
    ExpectRightParen();
    std::unique_ptr<Stmt> then_stmt = ParseStmt();
    if (CurIs(TokenType::ELSETK)) {
        Advance();
        std::unique_ptr<Stmt> else_stmt = ParseStmt();
        EmitSyntax("<Stmt>");
        return std::make_unique<IfStmt>(std::move(cond), std::move(then_stmt),
                                        std::make_optional(std::move(else_stmt)));
    }
    EmitSyntax("<Stmt>");
    return std::make_unique<IfStmt>(std::move(cond), std::move(then_stmt), std::nullopt);
}

/** ForStmt -> 'for' '(' [ForInitOrStep] ';' [Cond] ';' [ForInitOrStep] ')' Stmt */
std::unique_ptr<ForStmt> Parser::ParseForStmt() {
    Advance();
    Expect(TokenType::LPARENT, "");
    std::optional<std::unique_ptr<ForInitOrStep>> init;
    if (!CurIs(TokenType::SEMICN)) {
        init = ParseForInitOrStep();
    }
    ExpectSemicolon();
    std::optional<std::unique_ptr<Exp>> cond;
    if (!CurIs(TokenType::SEMICN)) {
        cond = ParseCond();
    }
    ExpectSemicolon();
    std::optional<std::unique_ptr<ForInitOrStep>> step;
    if (!CurIs(TokenType::RPARENT)) {
        step = ParseForInitOrStep();
    }
    ExpectRightParen();
    std::unique_ptr<Stmt> body = ParseStmt();
    EmitSyntax("<Stmt>");
    return std::make_unique<ForStmt>(std::move(init), std::move(cond), std::move(step),
                                     std::move(body));
}

/** BreakStmt -> 'break' ';' */
std::unique_ptr<BreakStmt> Parser::ParseBreakStmt() {
    Advance();
    ExpectSemicolon();
    EmitSyntax("<Stmt>");
    return std::make_unique<BreakStmt>();
}

/** ContinueStmt -> 'continue' ';' */
std::unique_ptr<ContinueStmt> Parser::ParseContinueStmt() {
    Advance();
    ExpectSemicolon();
    EmitSyntax("<Stmt>");
    return std::make_unique<ContinueStmt>();
}

/** ReturnStmt -> 'return' [Exp] ';' */
std::unique_ptr<ReturnStmt> Parser::ParseReturnStmt() {
    Advance();
    if (CurIs(TokenType::SEMICN)) {
        Advance();
        EmitSyntax("<Stmt>");
        return std::make_unique<ReturnStmt>(std::nullopt);
    }
    std::unique_ptr<Exp> exp = ParseExp();
    ExpectSemicolon();
    EmitSyntax("<Stmt>");
    return std::make_unique<ReturnStmt>(std::move(exp));
}

/** PrintfStmt -> 'printf' '(' StringConst { ',' Exp } ')' ';' */
std::unique_ptr<PrintfStmt> Parser::ParsePrintfStmt() {
    Advance();
    Expect(TokenType::LPARENT, "");
    std::string format_string = Cur()->value;
    Expect(TokenType::STRCON, "");
    std::vector<std::unique_ptr<Exp>> exps;
    while (CurIs(TokenType::COMMA)) {
        Advance();
        exps.push_back(ParseExp());
    }
    ExpectRightParen();
    ExpectSemicolon();
    EmitSyntax("<Stmt>");
    return std::make_unique<PrintfStmt>(std::move(format_string), std::move(exps));
}

/** ExpStmt -> [Exp] ';'. GetintStmt / GetcharStmt / AssignStmt via LVal '=' ... */
std::unique_ptr<Stmt> Parser::ParseOtherStmt() {
    if (CurIs(TokenType::SEMICN)) {
        Advance();
        EmitSyntax("<Stmt>");
        return std::make_unique<ExpStmt>(std::nullopt);
    }

    std::unique_ptr<LVal> lval = ParseLVal();
    if (CurIs(TokenType::ASSIGN)) {
        Advance();
        if (CurIs(TokenType::GETINTTK)) {
            Advance();
            Expect(TokenType::LPARENT, "");
            ExpectRightParen();
            ExpectSemicolon();
            EmitSyntax("<Stmt>");
            return std::make_unique<GetintStmt>(std::move(lval));
        }
        if (CurIs(TokenType::GETCHARTK)) {
            Advance();
            Expect(TokenType::LPARENT, "");
            ExpectRightParen();
            ExpectSemicolon();
            EmitSyntax("<Stmt>");
            return std::make_unique<GetcharStmt>(std::move(lval));
        }
        std::unique_ptr<Exp> exp = ParseExp();
        ExpectSemicolon();
        EmitSyntax("<Stmt>");
        return std::make_unique<AssignStmt>(std::move(lval), std::move(exp));
    }

    if (CurIs(TokenType::SEMICN)) {
        Advance();
        EmitSyntax("<Stmt>");
        return std::make_unique<ExpStmt>(std::nullopt);
    }
    if (!Cur().has_value()) {
        RecordError(0, "?");
        ExpectSemicolon();
        EmitSyntax("<Stmt>");
        return std::make_unique<ExpStmt>(std::nullopt);
    }
    OpType op = GetOperatorType(Cur()->value);
    Advance();
    std::unique_ptr<Exp> exp = ParseExp();
    ExpectSemicolon();
    EmitSyntax("<Stmt>");
    return std::make_unique<ExpStmt>(
        std::make_optional(std::make_unique<BinaryExp>(std::move(lval), std::move(exp), op)));
}

// -------------------------------------------------------------------------
// Expressions (public API and primary layer)
// -------------------------------------------------------------------------

/** Exp -> AddExp. Top-level expression. */
std::unique_ptr<Exp> Parser::ParseExp() {
    std::unique_ptr<Exp> e = ParseAddExp();
    EmitSyntax("<Exp>");
    return e;
}

/** Cond -> LOrExp. Used in if/for conditions. */
std::unique_ptr<Exp> Parser::ParseCond() {
    std::unique_ptr<Exp> e = ParseLOrExp();
    EmitSyntax("<Cond>");
    return e;
}

/** LVal -> Ident ['[' Exp ']']. */
std::unique_ptr<LVal> Parser::ParseLVal() {
    if (!Cur().has_value()) {
        RecordError(0, "?");
        return std::make_unique<LVal>("");
    }
    std::string ident = Cur()->value;
    Advance();
    if (CurIs(TokenType::LBRACK)) {
        Advance();
        std::unique_ptr<Exp> exp = ParseExp();
        ExpectRightBracket();
        EmitSyntax("<LVal>");
        return std::make_unique<LVal>(std::move(ident), std::move(exp));
    }
    EmitSyntax("<LVal>");
    return std::make_unique<LVal>(std::move(ident));
}

/** PrimaryExp -> '(' Exp ')' | LVal | Number | Character. */
std::unique_ptr<Exp> Parser::ParsePrimaryExp() {
    if (CurIs(TokenType::INTCON)) {
        std::unique_ptr<Exp> e = ParseNumber();
        EmitSyntax("<PrimaryExp>");
        return e;
    }
    if (CurIs(TokenType::CHRCON)) {
        std::unique_ptr<Exp> e = ParseCharacter();
        EmitSyntax("<PrimaryExp>");
        return e;
    }
    if (CurIs(TokenType::LPARENT)) {
        Advance();
        std::unique_ptr<Exp> exp = ParseExp();
        ExpectRightParen();
        EmitSyntax("<PrimaryExp>");
        return exp;
    }
    if (CurIs(TokenType::IDENFR)) {
        std::unique_ptr<Exp> e = ParseLVal();
        EmitSyntax("<PrimaryExp>");
        return e;
    }
    if (Cur().has_value()) {
        RecordError(Cur()->line_num, "?");
    }
    return nullptr;
}

// -------------------------------------------------------------------------
// Expression layers (iterative left-associative wrapping)
// -------------------------------------------------------------------------

/** AddExp -> MulExp | AddExp ('+' | '-') MulExp. */
std::unique_ptr<Exp> Parser::ParseAddExp() {
    auto lhs = ParseMulExp();
    while (CurIs(TokenType::PLUS) || CurIs(TokenType::MINU)) {
        OpType op = GetOperatorType(Cur()->value);
        Advance();
        auto rhs = ParseMulExp();
        lhs = std::make_unique<BinaryExp>(std::move(lhs), std::move(rhs), op);
    }
    EmitSyntax("<AddExp>");
    return lhs;
}

/** MulExp -> UnaryExp | MulExp ('*' | '/' | '%') UnaryExp. */
std::unique_ptr<Exp> Parser::ParseMulExp() {
    auto lhs = ParseUnaryExp();
    while (CurIs(TokenType::MULT) || CurIs(TokenType::DIV) || CurIs(TokenType::MOD)) {
        OpType op = GetOperatorType(Cur()->value);
        Advance();
        auto rhs = ParseUnaryExp();
        lhs = std::make_unique<BinaryExp>(std::move(lhs), std::move(rhs), op);
    }
    EmitSyntax("<MulExp>");
    return lhs;
}

/** UnaryExp -> PrimaryExp | Ident '(' [FuncRParams] ')' | UnaryOp UnaryExp. */
std::unique_ptr<Exp> Parser::ParseUnaryExp() {
    if (CurIs(TokenType::IDENFR) && LookaheadIs(TokenType::LPARENT)) {
        std::string ident = Cur()->value;
        Advance();
        Advance();
        std::unique_ptr<FuncRParams> func_r_params = ParseFuncRParams();
        ExpectRightParen();
        EmitSyntax("<UnaryExp>");
        return std::make_unique<FuncCall>(std::move(ident), std::move(func_r_params));
    }
    if (CurIs(TokenType::PLUS) || CurIs(TokenType::MINU) || CurIs(TokenType::NOT)) {
        OpType op = ParseUnaryOp();
        std::unique_ptr<Exp> exp = ParseUnaryExp();
        EmitSyntax("<UnaryExp>");
        return std::make_unique<UnaryExp>(std::move(exp), op);
    }

    std::unique_ptr<Exp> exp = ParsePrimaryExp();
    EmitSyntax("<UnaryExp>");
    return exp;
}

/** RelExp -> AddExp | RelExp ('<' | '>' | '<=' | '>=') AddExp. */
std::unique_ptr<Exp> Parser::ParseRelExp() {
    auto lhs = ParseAddExp();
    while (CurIs(TokenType::LSS) || CurIs(TokenType::GRE) || CurIs(TokenType::LEQ) ||
           CurIs(TokenType::GEQ)) {
        OpType op = GetOperatorType(Cur()->value);
        Advance();
        auto rhs = ParseAddExp();
        lhs = std::make_unique<BinaryExp>(std::move(lhs), std::move(rhs), op);
    }
    EmitSyntax("<RelExp>");
    return lhs;
}

/** EqExp -> RelExp | EqExp ('==' | '!=') RelExp. */
std::unique_ptr<Exp> Parser::ParseEqExp() {
    auto lhs = ParseRelExp();
    while (CurIs(TokenType::EQL) || CurIs(TokenType::NEQ)) {
        OpType op = GetOperatorType(Cur()->value);
        Advance();
        auto rhs = ParseRelExp();
        lhs = std::make_unique<BinaryExp>(std::move(lhs), std::move(rhs), op);
    }
    EmitSyntax("<EqExp>");
    return lhs;
}

/** LAndExp -> EqExp | LAndExp '&&' EqExp. */
std::unique_ptr<Exp> Parser::ParseLAndExp() {
    auto lhs = ParseEqExp();
    while (CurIs(TokenType::AND)) {
        OpType op = GetOperatorType(Cur()->value);
        Advance();
        auto rhs = ParseEqExp();
        lhs = std::make_unique<BinaryExp>(std::move(lhs), std::move(rhs), op);
    }
    EmitSyntax("<LAndExp>");
    return lhs;
}

/** LOrExp -> LAndExp | LOrExp '||' LAndExp. */
std::unique_ptr<Exp> Parser::ParseLOrExp() {
    auto lhs = ParseLAndExp();
    while (CurIs(TokenType::OR)) {
        OpType op = GetOperatorType(Cur()->value);
        Advance();
        auto rhs = ParseLAndExp();
        lhs = std::make_unique<BinaryExp>(std::move(lhs), std::move(rhs), op);
    }
    EmitSyntax("<LOrExp>");
    return lhs;
}

/** Number -> IntConst. Returns Number (Exp). */
std::unique_ptr<Exp> Parser::ParseNumber() {
    if (CurIs(TokenType::INTCON)) {
        int int_const = std::stoi(Cur()->value);
        Advance();
        EmitSyntax("<Number>");
        return std::make_unique<Number>(int_const);
    }
    if (Cur().has_value()) {
        RecordError(Cur()->line_num, "?");
    }
    return nullptr;
}

/** Character -> CharConst. Returns Character (Exp). */
std::unique_ptr<Exp> Parser::ParseCharacter() {
    if (CurIs(TokenType::CHRCON)) {
        char char_const = Cur()->value[0];
        Advance();
        EmitSyntax("<Character>");
        return std::make_unique<Character>(char_const);
    }
    if (Cur().has_value()) {
        RecordError(Cur()->line_num, "?");
    }
    return nullptr;
}

/** UnaryOp -> '+' | '-' | '!'. Consumes token and returns OpType. */
OpType Parser::ParseUnaryOp() {
    if (CurIs(TokenType::PLUS)) {
        Advance();
        return OpType::PLUS;
    }
    if (CurIs(TokenType::MINU)) {
        Advance();
        return OpType::MINU;
    }
    if (CurIs(TokenType::NOT)) {
        Advance();
        return OpType::NOT;
    }
    if (Cur().has_value()) {
        RecordError(Cur()->line_num, "?");
    }
    return OpType::NONE;
}

/** FuncRParams -> Exp { ',' Exp }. Empty when immediately see ')'. */
std::unique_ptr<FuncRParams> Parser::ParseFuncRParams() {
    std::vector<std::unique_ptr<Exp>> exps;
    if (CurIs(TokenType::RPARENT)) {
        EmitSyntax("<FuncRParams>");
        return std::make_unique<FuncRParams>(std::move(exps));
    }
    exps.push_back(ParseExp());
    while (CurIs(TokenType::COMMA)) {
        Advance();
        exps.push_back(ParseExp());
    }
    EmitSyntax("<FuncRParams>");
    return std::make_unique<FuncRParams>(std::move(exps));
}

void Parser::SetParserOutput(std::ostream* out) {
    parser_out_ = out;
}

void Parser::SetEmitParserOutput(bool enable) {
    emit_parser_output_ = enable;
}

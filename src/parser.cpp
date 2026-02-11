#include "Parser.h"
#include "AST.h"
#include "Lexer.h"
Parser::Parser(Lexer& lexer) : lexer_(lexer) {}

/** Entry point: CompUnit → {Decl} {FuncDef} MainFuncDef. */
std::unique_ptr<CompUnit> Parser::ParseCompUnit() {
    std::vector<std::unique_ptr<Decl>> decls;
    std::vector<std::unique_ptr<FuncDef>> func_defs;
    std::unique_ptr<MainFuncDef> main_func_def;
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
        RecordError(Cur()->line_num, "XXX");
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
std::unique_ptr<FuncDef> ParseFuncDef();
/** MainFuncDef → 'int' 'main' '(' ')' Block. */
std::unique_ptr<MainFuncDef> ParseMainFuncDef();

// -------------------------------------------------------------------------
// Block and block items
// -------------------------------------------------------------------------
/** Block → '{' { BlockItem } '}'. */
std::unique_ptr<Block> ParseBlock();
/** BlockItem → Decl | Stmt. */
std::unique_ptr<BlockItem> ParseBlockItem();

// -------------------------------------------------------------------------
// Statements
// -------------------------------------------------------------------------
/** Dispatches to the appropriate Stmt production. */
std::unique_ptr<Stmt> ParseStmt();

// -------------------------------------------------------------------------
// Expressions (public API: Exp and primary layer)
// -------------------------------------------------------------------------
/** Exp → AddExp. Top-level expression. */
std::unique_ptr<Exp> ParseExp();
/** Cond → LOrExp. Used in if/for conditions. */
std::unique_ptr<Exp> ParseCond();
/** LVal → Ident ['[' Exp ']']. */
std::unique_ptr<LVal> ParseLVal();
/**
 * PrimaryExp → '(' Exp ')' | LVal | Number | Character.
 * Lowest precedence; used by UnaryExp.
 */
std::unique_ptr<Exp> ParsePrimaryExp();

// -------------------------------------------------------------------------
// Parser output and errors
// -------------------------------------------------------------------------
/** Syntax errors collected during parsing: (line_number, error_code). */
const std::vector<std::pair<int, std::string>>& GetErrorLog() const;
/** Enable/disable emission of token and syntax lines to parser_out (e.g. parser.txt). */
void SetParserOutput(std::ostream* out);
void SetEmitParserOutput(bool enable);

// -------------------------------------------------------------------------
// Token helpers (TODO: implement in Parser.cpp)
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
std::unique_ptr<Exp> ParseConstExp();

// -------------------------------------------------------------------------
// Expression layers (all return unique_ptr<Exp> for AST uniformity)
// Grammar has separate non-terminals (AddExp, MulExp, ...) for precedence.
// We keep separate Parse* methods to implement precedence in recursive
// descent: ParseAddExp calls ParseMulExp and loops on '+'/'-'; ParseMulExp
// calls ParseUnaryExp and loops on '*'/'/'/'%'; etc. No separate AST
// node types for AddExp/MulExp—they become BinaryExp with the right OpType.
// -------------------------------------------------------------------------
/** AddExp → MulExp | AddExp ('+' | '−') MulExp. */
std::unique_ptr<Exp> ParseAddExp();
/** MulExp → UnaryExp | MulExp ('*' | '/' | '%') UnaryExp. */
std::unique_ptr<Exp> ParseMulExp();
/** UnaryExp → PrimaryExp | Ident '(' [FuncRParams] ')' | UnaryOp UnaryExp. */
std::unique_ptr<Exp> ParseUnaryExp();
/** RelExp → AddExp | RelExp ('<' | '>' | '<=' | '>=') AddExp. */
std::unique_ptr<Exp> ParseRelExp();
/** EqExp → RelExp | EqExp ('==' | '!=') RelExp. */
std::unique_ptr<Exp> ParseEqExp();
/** LAndExp → EqExp | LAndExp '&&' EqExp. */
std::unique_ptr<Exp> ParseLAndExp();
/** LOrExp → LAndExp | LOrExp '||' LAndExp. */
std::unique_ptr<Exp> ParseLOrExp();

/** Number → IntConst. Returns Number (Exp). */
std::unique_ptr<Exp> ParseNumber();
/** Character → CharConst. Returns Character (Exp). */
std::unique_ptr<Exp> ParseCharacter();
/** UnaryOp → '+' | '−' | '!'. Consumes token and returns OpType. */
OpType ParseUnaryOp();
/** FuncRParams → Exp { ',' Exp }. */
std::unique_ptr<FuncRParams> ParseFuncRParams();

/** ForStmt → LVal '=' Exp (used in for-loop init/step). */
std::unique_ptr<Stmt> ParseForStmt();
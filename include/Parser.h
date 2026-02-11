#pragma once

#include "AST.h"
#include "Lexer.h"
#include "TokenType.h"
#include <memory>
#include <ostream>
#include <stack>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/**
 * Recursive-descent parser for SysY.
 * Holds a reference to the Lexer; does not own it.
 *
 * Error handling: We do not use exception-based syntax error handling.
 * Instead, the parser records (line, error_code) in an internal log and
 * continues parsing when possible (e.g. after a missing ';'). This satisfies
 * the requirement that "even for an erroneous program, complete syntax
 * analysis should be performed" and allows outputting all errors to
 * error.txt. No inheritance from an error-handling base is needed; the
 * driver can merge Lexer::GetErrorLog() and Parser::GetErrorLog() when
 * writing error.txt.
 *
 * Syntax error codes (see docs/2024_SysY_detailed.md "文法符号与错误类型对应"):
 *   i = missing ';',  j = missing ')',  k = missing ']'.
 *   (a = illegal symbol is lexical.) Defensive branches may use a fallback code.
 *
 * Lookahead (no backtracking): The original Java reference used lookAhead/lookDoubleAhead plus
 * save/restore and Handler.save/restore in parseStmtOther to "try Exp, then backtrack
 * to LVal = ...". In C++, we avoid that by having Lexer::PeekNext() / PeekNext2() return
 * the next token(s) without consuming (implemented by save state -> Next() -> capture ->
 * restore). The parser then uses LookaheadIs(t) / Lookahead2Is(t) to decide the
 * production (e.g. Ident + next=='(', '=', or other) and never needs to backtrack.
 */
class Parser {
public:
    /** Takes a non-owning reference to the lexer. Caller must keep lexer alive. */
    explicit Parser(Lexer& lexer);

    /** Entry point: CompUnit → {Decl} {FuncDef} MainFuncDef. */
    std::unique_ptr<CompUnit> ParseCompUnit();

    // -------------------------------------------------------------------------
    // Declarations
    // -------------------------------------------------------------------------
    /** Decl → ConstDecl | VarDecl. */
    std::unique_ptr<Decl> ParseDecl();
    /** ConstDecl → 'const' BType ConstDef { ',' ConstDef } ';'. */
    std::unique_ptr<ConstDecl> ParseConstDecl();
    /** VarDecl → BType VarDef { ',' VarDef } ';'. */
    std::unique_ptr<VarDecl> ParseVarDecl();

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
    /**
     * Dispatches to the appropriate Stmt production.
     *
     * Implementation note (ParseStmtOther):
     * - CurIs(PLUS/MINU/NOT/INTCON/CHRCON/LPARENT) -> expression statement; ParseExp(),
     * ExpectSemicolon().
     * - CurIs(IDENFR) and LookaheadIs(LPARENT) -> expression (function call); ParseExp(),
     * ExpectSemicolon().
     * - CurIs(IDENFR) and LookaheadIs(ASSIGN) -> LVal '=' Exp | getint | getchar; ParseLVal(),
     * consume '=', etc.
     * - CurIs(IDENFR) and other (e.g. LBRACK, so "a[10]" or "a[10]=2"): one-token lookahead cannot
     * distinguish
     *   "[Exp] ';'" (e.g. a[10];) from "LVal '=' Exp ';'" (e.g. a[10]=2;). Parse LVal first; then:
     *   - if CurIs(ASSIGN) -> assignment (or getint/getchar);
     *   - if CurIs(SEMICN) -> expression statement (the LVal is the whole expression);
     *   - else (e.g. PLUS) -> expression starting with that LVal, parse rest of Exp (e.g.
     * ParseAddExpTail or re-enter expression layer so that the already-consumed LVal is the first
     * PrimaryExp), then ExpectSemicolon().
     */
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
    const std::vector<std::pair<int, std::string>>& GetErrorLog() const {
        return error_log_;
    }

    /** Enable/disable emission of token and syntax lines to parser_out (e.g. parser.txt). */
    void SetParserOutput(std::ostream* out);
    void SetEmitParserOutput(bool enable);

private:
    Lexer& lexer_;
    std::vector<std::pair<int, std::string>> error_log_;
    std::ostream* parser_out_{nullptr};
    bool emit_parser_output_{false};

    // -------------------------------------------------------------------------
    // Token helpers (TODO: implement in Parser.cpp)
    // -------------------------------------------------------------------------
    /** Returns current token if available. */
    const std::optional<Token>& Cur() const {
        return lexer_.GetCurrentToken();
    }

    /** Returns true if current token has type t. Safe when Cur() is empty (returns false). */
    bool CurIs(TokenType t) const {
        return Cur().has_value() && Cur()->type == t;
    }

    /**
     * Returns true if the next token (without consuming) has type t.
     * Use to avoid backtracking: e.g. Ident + next=='(', next=='=', next==other.
     */
    bool LookaheadIs(TokenType t);

    /**
     * Returns true if the token two ahead has type t (e.g. CompUnit: int ident ( ).
     */
    bool Lookahead2Is(TokenType t);

    /** Advance to next token. */
    void Advance() {
        lexer_.Next();
    }

    /** Record a syntax error (line, code) and optionally synchronize; parsing continues. */
    void RecordError(int line, std::string code);

    /** Emit current token to parser_out_ if enabled. */
    void EmitToken();

    /** Emit a syntax component name like "<CompUnit>" if enabled. */
    void EmitSyntax(std::string_view name);

    // -------------------------------------------------------------------------
    // Expect / consume helpers (TODO: implement; record error if missing)
    // -------------------------------------------------------------------------
    /** Expect ';', consume if present, else record error 'i'. */
    void ExpectSemicolon();
    /** Expect ')', consume if present, else record error 'j'. */
    void ExpectRightParen();
    /** Expect ']', consume if present, else record error 'k'. */
    void ExpectRightBracket();

    // -------------------------------------------------------------------------
    // Type and params
    // -------------------------------------------------------------------------
    /** BType → 'int' | 'char'. */
    BType ParseBType();
    /** FuncType → 'void' | 'int' | 'char'. */
    BType ParseFuncType();
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
    std::unique_ptr<ConstExp> ParseConstExp();

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

    /** ForInitOrStep → LVal '=' Exp (used in for-loop init/step). */
    std::unique_ptr<ForInitOrStep> ParseForInitOrStep();

    // -------------------------------------------------------------------------
    // Statements
    // -------------------------------------------------------------------------
    /** ForStmt → 'for' '(' [ForInitOrStep] ';' [Cond] ';' [ForInitOrStep] ')' Stmt */
    std::unique_ptr<ForStmt> ParseForStmt();
    /** IfStmt → 'if' '(' Cond ')' Stmt [ 'else' Stmt ] */
    std::unique_ptr<IfStmt> ParseIfStmt();
    /** BlockStmt → Block */
    std::unique_ptr<BlockStmt> ParseBlockStmt();
    /** BreakStmt → 'break' ';' */
    std::unique_ptr<BreakStmt> ParseBreakStmt();
    /** ContinueStmt → 'continue' ';' */
    std::unique_ptr<ContinueStmt> ParseContinueStmt();
    /** ReturnStmt → 'return' [Exp] ';' */
    std::unique_ptr<ReturnStmt> ParseReturnStmt();
    /** PrintfStmt → 'printf''('StringConst {','Exp}')'';' */
    std::unique_ptr<PrintfStmt> ParsePrintfStmt();
    /** ExpStmt → [Exp] ';' */
    /** GetintStmt → LVal '=' 'getint''('')'';' */
    /** GetcharStmt → LVal '=' 'getchar''('')'';' */
    /** AssignStmt → LVal '=' Exp ';' */
    std::unique_ptr<Stmt> ParseOtherStmt();

    // -------------------------------------------------------------------------
    // Helper functions (TODO: implement in Parser.cpp)
    // -------------------------------------------------------------------------
    /** Helper function for constructing an expression from a list of elements and operators. */
    std::unique_ptr<Exp> ConstructExpFromElements(std::stack<std::unique_ptr<Exp>>& exp_stack,
                                                  std::stack<OpType>& op_stack);
};

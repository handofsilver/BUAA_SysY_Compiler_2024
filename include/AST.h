#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// =============================================================================
// Forward declarations
// =============================================================================
class ASTVisitor;
class Block;
class Stmt;
class Exp;
class ConstInitVal;
class InitVal;
class FuncFParam;

// =============================================================================
// Operator type (OpType)
// =============================================================================
enum class OpType {
    ADD,
    SUB,
    MUL,
    DIV,
    MOD,
    LT,
    GT,
    LE,
    GE,
    EQ,
    NE,
    AND,
    OR,
    NOT,
    PLUS,
    MINU,
    NONE,
};

OpType GetOperatorType(std::string_view op);

// =============================================================================
// Basic type (BType) / function type (FuncType shares BType)
// =============================================================================
enum class BType {
    INT,
    CHAR,
    VOID,
};

// =============================================================================
// Base class
// =============================================================================
class ASTNode {
public:
    virtual ~ASTNode() = default;
    ASTNode(const ASTNode&) = delete;
    ASTNode& operator=(const ASTNode&) = delete;

    /** Double-dispatch: call visitor.VisitXxx(*this). Defined in AST.cpp. */
    virtual void Accept(ASTVisitor& visitor) = 0;

    /** Source line (1-based). Parser should set for error reporting. Default 0. */
    int GetLine() const {
        return line_;
    }

    void SetLine(int line) {
        line_ = line;
    }

protected:
    ASTNode() = default;
    ASTNode(ASTNode&&) = default;
    ASTNode& operator=(ASTNode&&) = default;
    int line_ = 0;
};

// =============================================================================
// BlockItem: BlockItem -> Decl | Stmt. Inheritance: ASTNode -> BlockItem -> Decl/Stmt
// =============================================================================
class BlockItem : public ASTNode {
public:
    ~BlockItem() override = default;
};

// =============================================================================
// Decl: Decl -> ConstDecl | VarDecl (inherits BlockItem)
// =============================================================================
class Decl : public BlockItem {
public:
    ~Decl() override = default;
};

// Def / ConstDef / VarDef depend on Exp, ConstInitVal, InitVal; defined after Exp family.

// =============================================================================
// Expression (Exp) family
// =============================================================================
class Exp : public ASTNode {
public:
    ~Exp() override = default;
};

/** LVal: Ident [ '[' Exp ']' ]. */
class LVal : public Exp {
public:
    std::string ident;
    std::unique_ptr<Exp> index;

    LVal(std::string ident, std::unique_ptr<Exp> index) :
    ident(std::move(ident)),
    index(std::move(index)) {}
    LVal(std::string ident) : ident(std::move(ident)) {}

    void Accept(ASTVisitor& visitor) override;
    ~LVal() override = default;
};

/** Number: IntConst. */
class Number : public Exp {
public:
    int int_const;

    explicit Number(int value) : int_const(value) {}
    void Accept(ASTVisitor& visitor) override;
    ~Number() override = default;
};

/** Character: CharConst. */
class Character : public Exp {
public:
    char char_const;

    explicit Character(char value) : char_const(value) {}
    void Accept(ASTVisitor& visitor) override;
    ~Character() override = default;
};

/** Binary expression: MulExp/AddExp/RelExp/EqExp/LAndExp/LOrExp. */
class BinaryExp : public Exp {
public:
    std::unique_ptr<Exp> lhs;
    std::unique_ptr<Exp> rhs;
    OpType op;

    BinaryExp(std::unique_ptr<Exp> lhs, std::unique_ptr<Exp> rhs, OpType op) :
    lhs(std::move(lhs)),
    rhs(std::move(rhs)),
    op(op) {}
    void Accept(ASTVisitor& visitor) override;
    ~BinaryExp() override = default;
};

/** Unary expression: UnaryOp UnaryExp (+ / - / !). */
class UnaryExp : public Exp {
public:
    std::unique_ptr<Exp> operand;
    OpType op;

    UnaryExp(std::unique_ptr<Exp> operand, OpType op) : operand(std::move(operand)), op(op) {}
    void Accept(ASTVisitor& visitor) override;
    ~UnaryExp() override = default;
};

/** FuncRParams: Exp { ',' Exp }. */
class FuncRParams : public ASTNode {
public:
    std::vector<std::unique_ptr<Exp>> exp_list;

    explicit FuncRParams(std::vector<std::unique_ptr<Exp>> exp_list) :
    exp_list(std::move(exp_list)) {}
    ~FuncRParams() override = default;
};

/** Function call: Ident '(' [FuncRParams] ')'. */
class FuncCall : public Exp {
public:
    std::string ident;
    std::unique_ptr<FuncRParams> func_r_params;

    FuncCall(std::string ident, std::unique_ptr<FuncRParams> func_r_params) :
    ident(std::move(ident)),
    func_r_params(std::move(func_r_params)) {}
    void Accept(ASTVisitor& visitor) override;
    ~FuncCall() override = default;
};

/**
 * ConstExp -> AddExp. Wrapper to mark constant-only context for semantic analysis.
 */
class ConstExp : public Exp {
public:
    std::unique_ptr<Exp> inner;

    explicit ConstExp(std::unique_ptr<Exp> inner) : inner(std::move(inner)) {}
    void Accept(ASTVisitor& visitor) override;
    ~ConstExp() override = default;
};

// =============================================================================
// Init values: ConstInitVal / InitVal.
// Grammar: ConstInitVal -> ConstExp | '{' [ ConstExp { ',' ConstExp } ] '}' | StringConst;
//          InitVal     -> Exp       | '{' [ Exp       { ',' Exp       } ] '}' | StringConst.
// One of the three alternatives; represented with std::variant for type safety.
// =============================================================================

class ConstInitVal : public ASTNode {
public:
    using SingleExp = std::unique_ptr<ConstExp>;
    using ExpList = std::vector<std::unique_ptr<ConstExp>>;
    using StringVal = std::string;
    std::variant<SingleExp, ExpList, StringVal> value;

    explicit ConstInitVal(SingleExp single) : value(std::move(single)) {}
    explicit ConstInitVal(ExpList list) : value(std::move(list)) {}
    explicit ConstInitVal(StringVal s) : value(std::move(s)) {}
    void Accept(ASTVisitor& visitor) override;
    ~ConstInitVal() override = default;
};

class InitVal : public ASTNode {
public:
    using SingleExp = std::unique_ptr<Exp>;
    using ExpList = std::vector<std::unique_ptr<Exp>>;
    using StringVal = std::string;
    std::variant<SingleExp, ExpList, StringVal> value;

    explicit InitVal(SingleExp single) : value(std::move(single)) {}
    explicit InitVal(ExpList list) : value(std::move(list)) {}
    explicit InitVal(StringVal s) : value(std::move(s)) {}
    void Accept(ASTVisitor& visitor) override;
    ~InitVal() override = default;
};

// =============================================================================
// Def: ConstDef / VarDef
// =============================================================================
class Def : public ASTNode {
public:
    ~Def() override = default;
};

class ConstDef : public Def {
public:
    std::string ident;
    /** Grammar: at most one [ ConstExp ]; empty = scalar, has value = 1D array. */
    std::optional<std::unique_ptr<ConstExp>> array_size;
    std::unique_ptr<ConstInitVal> const_init_val;

    ConstDef(std::string ident, std::optional<std::unique_ptr<ConstExp>> array_size,
             std::unique_ptr<ConstInitVal> const_init_val) :
    ident(std::move(ident)),
    array_size(std::move(array_size)),
    const_init_val(std::move(const_init_val)) {}
    void Accept(ASTVisitor& visitor) override;
    ~ConstDef() override = default;
};

class VarDef : public Def {
public:
    std::string ident;
    /** Grammar: at most one [ ConstExp ]; empty = scalar, has value = 1D array. */
    std::optional<std::unique_ptr<ConstExp>> array_size;
    std::unique_ptr<InitVal> init_val;

    VarDef(std::string ident, std::optional<std::unique_ptr<ConstExp>> array_size,
           std::unique_ptr<InitVal> init_val) :
    ident(std::move(ident)),
    array_size(std::move(array_size)),
    init_val(std::move(init_val)) {}
    void Accept(ASTVisitor& visitor) override;
    ~VarDef() override = default;
};

// =============================================================================
// ConstDecl / VarDecl (depends on Def / ConstDef / VarDef)
// =============================================================================
class ConstDecl : public Decl {
public:
    BType btype;
    std::vector<std::unique_ptr<ConstDef>> const_defs;

    ConstDecl(BType btype, std::vector<std::unique_ptr<ConstDef>> const_defs) :
    btype(btype),
    const_defs(std::move(const_defs)) {}
    void Accept(ASTVisitor& visitor) override;
    ~ConstDecl() override = default;
};

class VarDecl : public Decl {
public:
    BType btype;
    std::vector<std::unique_ptr<VarDef>> var_defs;

    VarDecl(BType btype, std::vector<std::unique_ptr<VarDef>> var_defs) :
    btype(btype),
    var_defs(std::move(var_defs)) {}
    void Accept(ASTVisitor& visitor) override;
    ~VarDecl() override = default;
};

// =============================================================================
// FuncFParam: BType Ident ['[' ']']
// =============================================================================
class FuncFParam : public ASTNode {
public:
    BType btype;
    std::string ident;
    bool is_array;

    FuncFParam(BType btype, std::string ident, bool is_array) :
    btype(btype),
    ident(std::move(ident)),
    is_array(is_array) {}
    void Accept(ASTVisitor& visitor) override;
    ~FuncFParam() override = default;
};

// =============================================================================
// Block: defined before Stmt so that BlockStmt can hold unique_ptr<Block>
// =============================================================================
class Block : public ASTNode {
public:
    std::vector<std::unique_ptr<BlockItem>> block_items;

    explicit Block(std::vector<std::unique_ptr<BlockItem>> block_items) :
    block_items(std::move(block_items)) {}
    void Accept(ASTVisitor& visitor) override;
    ~Block() override = default;
};

// =============================================================================
// Statement (Stmt) family; Stmt inherits BlockItem
// =============================================================================
class ForInitOrStep : public ASTNode {
public:
    std::unique_ptr<LVal> lval;
    std::unique_ptr<Exp> exp;

    ForInitOrStep(std::unique_ptr<LVal> lval, std::unique_ptr<Exp> exp) :
    lval(std::move(lval)),
    exp(std::move(exp)) {}
    void Accept(ASTVisitor& visitor) override;
    ~ForInitOrStep() override = default;
};

class Stmt : public BlockItem {
public:
    ~Stmt() override = default;
};

/** LVal '=' Exp ';' */
class AssignStmt : public Stmt {
public:
    std::unique_ptr<LVal> lval;
    std::unique_ptr<Exp> exp;

    AssignStmt(std::unique_ptr<LVal> lval, std::unique_ptr<Exp> exp) :
    lval(std::move(lval)),
    exp(std::move(exp)) {}
    void Accept(ASTVisitor& visitor) override;
    ~AssignStmt() override = default;
};

/** [Exp] ';'. */
class ExpStmt : public Stmt {
public:
    std::optional<std::unique_ptr<Exp>> exp;

    explicit ExpStmt(std::optional<std::unique_ptr<Exp>> exp) : exp(std::move(exp)) {}
    void Accept(ASTVisitor& visitor) override;
    ~ExpStmt() override = default;
};

/** Block as statement (reuses Block node). */
class BlockStmt : public Stmt {
public:
    std::unique_ptr<Block> block;

    explicit BlockStmt(std::unique_ptr<Block> block) : block(std::move(block)) {}
    void Accept(ASTVisitor& visitor) override;
    ~BlockStmt() override = default;
};

/** if ( Cond ) Stmt [ else Stmt ] */
class IfStmt : public Stmt {
public:
    std::unique_ptr<Exp> cond;
    std::unique_ptr<Stmt> then_stmt;
    std::optional<std::unique_ptr<Stmt>> else_stmt;

    IfStmt(std::unique_ptr<Exp> cond, std::unique_ptr<Stmt> then_stmt,
           std::optional<std::unique_ptr<Stmt>> else_stmt) :
    cond(std::move(cond)),
    then_stmt(std::move(then_stmt)),
    else_stmt(std::move(else_stmt)) {}
    void Accept(ASTVisitor& visitor) override;
    ~IfStmt() override = default;
};

/**
 * for ( [ForInitOrStep] ; [Cond] ; [ForInitOrStep] ) Stmt.
 * Grammar uses "ForStmt" for the init/step clauses (LVal '=' Exp); we name the node
 * ForInitOrStep to avoid confusion with this full for-statement.
 */
class ForStmt : public Stmt {
public:
    std::optional<std::unique_ptr<ForInitOrStep>> init;
    std::optional<std::unique_ptr<Exp>> cond;
    std::optional<std::unique_ptr<ForInitOrStep>> step;
    std::unique_ptr<Stmt> body;

    ForStmt(std::optional<std::unique_ptr<ForInitOrStep>> init,
            std::optional<std::unique_ptr<Exp>> cond,
            std::optional<std::unique_ptr<ForInitOrStep>> step, std::unique_ptr<Stmt> body) :
    init(std::move(init)),
    cond(std::move(cond)),
    step(std::move(step)),
    body(std::move(body)) {}
    void Accept(ASTVisitor& visitor) override;
    ~ForStmt() override = default;
};

class BreakStmt : public Stmt {
public:
    BreakStmt() = default;
    void Accept(ASTVisitor& visitor) override;
    ~BreakStmt() override = default;
};

class ContinueStmt : public Stmt {
public:
    ContinueStmt() = default;
    void Accept(ASTVisitor& visitor) override;
    ~ContinueStmt() override = default;
};

/** return [Exp] ';' */
class ReturnStmt : public Stmt {
public:
    std::optional<std::unique_ptr<Exp>> exp;

    explicit ReturnStmt(std::optional<std::unique_ptr<Exp>> exp) : exp(std::move(exp)) {}
    void Accept(ASTVisitor& visitor) override;
    ~ReturnStmt() override = default;
};

/** LVal '=' getint() ';' */
class GetintStmt : public Stmt {
public:
    std::unique_ptr<LVal> lval;

    explicit GetintStmt(std::unique_ptr<LVal> lval) : lval(std::move(lval)) {}
    void Accept(ASTVisitor& visitor) override;
    ~GetintStmt() override = default;
};

/** LVal '=' getchar() ';' */
class GetcharStmt : public Stmt {
public:
    std::unique_ptr<LVal> lval;

    explicit GetcharStmt(std::unique_ptr<LVal> lval) : lval(std::move(lval)) {}
    void Accept(ASTVisitor& visitor) override;
    ~GetcharStmt() override = default;
};

/** printf ( StringConst { ',' Exp } ) ';' */
class PrintfStmt : public Stmt {
public:
    std::string format_string;
    std::vector<std::unique_ptr<Exp>> exp_list;

    PrintfStmt(std::string format_string, std::vector<std::unique_ptr<Exp>> exp_list) :
    format_string(std::move(format_string)),
    exp_list(std::move(exp_list)) {}
    void Accept(ASTVisitor& visitor) override;
    ~PrintfStmt() override = default;
};

// =============================================================================
// Function definition and main
// =============================================================================
class FuncDef : public ASTNode {
public:
    BType func_type;
    std::string ident;
    std::vector<std::unique_ptr<FuncFParam>> func_f_params;
    std::unique_ptr<Block> block;

    FuncDef(BType func_type, std::string ident,
            std::vector<std::unique_ptr<FuncFParam>> func_f_params, std::unique_ptr<Block> block) :
    func_type(func_type),
    ident(std::move(ident)),
    func_f_params(std::move(func_f_params)),
    block(std::move(block)) {}
    void Accept(ASTVisitor& visitor) override;
    ~FuncDef() override = default;
};

class MainFuncDef : public ASTNode {
public:
    std::unique_ptr<Block> block;

    explicit MainFuncDef(std::unique_ptr<Block> block) : block(std::move(block)) {}
    void Accept(ASTVisitor& visitor) override;
    ~MainFuncDef() override = default;
};

// =============================================================================
// CompUnit (root node)
// =============================================================================
class CompUnit : public ASTNode {
public:
    std::vector<std::unique_ptr<Decl>> decls;
    std::vector<std::unique_ptr<FuncDef>> func_defs;
    std::unique_ptr<MainFuncDef> main_func_def;

    CompUnit(std::vector<std::unique_ptr<Decl>> decls,
             std::vector<std::unique_ptr<FuncDef>> func_defs,
             std::unique_ptr<MainFuncDef> main_func_def) :
    decls(std::move(decls)),
    func_defs(std::move(func_defs)),
    main_func_def(std::move(main_func_def)) {}
    void Accept(ASTVisitor& visitor) override;
    ~CompUnit() override = default;
};

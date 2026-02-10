#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

// =============================================================================
// 前向声明 (Forward Declarations)
// =============================================================================
class Block;
class Stmt;
class Exp;
class ConstInitVal;
class InitVal;
class FuncFParam;

// =============================================================================
// 辅助枚举：运算符类型 (OpType)
// =============================================================================
enum class OpType {
    // 算术
    ADD,
    SUB,
    MUL,
    DIV,
    MOD,
    // 关系
    LT,
    GT,
    LE,
    GE,
    EQ,
    NE,
    // 逻辑
    AND,
    OR,
    NOT,
    // 单目 + - !
    PLUS,
    MINU,
};

// =============================================================================
// 基本类型 (BType) / 函数类型 (FuncType 与 BType 共用)
// =============================================================================
enum class BType {
    INT,
    CHAR,
    VOID,
};

// =============================================================================
// 基类 (Base Class)
// =============================================================================
class ASTNode {
public:
    virtual ~ASTNode() = default;
    ASTNode(const ASTNode&) = delete;
    ASTNode& operator=(const ASTNode&) = delete;

protected:
    ASTNode() = default;
    ASTNode(ASTNode&&) = default;
    ASTNode& operator=(ASTNode&&) = default;
};

// =============================================================================
// BlockItem：语句块项，对应 BlockItem → Decl | Stmt
// 继承链：ASTNode → BlockItem → Decl / Stmt
// =============================================================================
class BlockItem : public ASTNode {
public:
    ~BlockItem() override = default;
};

// =============================================================================
// 声明层次 (Decl)：Decl → ConstDecl | VarDecl，现继承自 BlockItem
// =============================================================================
class Decl : public BlockItem {
public:
    ~Decl() override = default;
};

// --- Def / ConstDef / VarDef 前置依赖 Exp, ConstInitVal, InitVal，故在 Exp 家族之后定义 ---

// =============================================================================
// 表达式 (Exp) 家族
// =============================================================================
class Exp : public ASTNode {
public:
    ~Exp() override = default;
};

/** 左值：Ident [ '[' Exp ']' ] */
class LVal : public Exp {
public:
    std::string ident;
    std::vector<std::unique_ptr<Exp>> index;

    LVal(std::string ident, std::vector<std::unique_ptr<Exp>> index) : ident(std::move(ident)), index(std::move(index)) {}
    ~LVal() override = default;
};

/** 数值 IntConst */
class Number : public Exp {
public:
    int int_const;

    explicit Number(int value) : int_const(value) {}
    ~Number() override = default;
};

/** 字符字面量 CharConst */
class Character : public Exp {
public:
    char char_const; // 或 std::string 若需支持转义等

    explicit Character(char value) : char_const(value) {}
    ~Character() override = default;
};

/** 双目运算：MulExp/AddExp/RelExp/EqExp/LAndExp/LOrExp 等 */
class BinaryExp : public Exp {
public:
    std::unique_ptr<Exp> lhs;
    std::unique_ptr<Exp> rhs;
    OpType op;

    BinaryExp(std::unique_ptr<Exp> lhs, std::unique_ptr<Exp> rhs, OpType op) : lhs(std::move(lhs)), rhs(std::move(rhs)), op(op) {}
    ~BinaryExp() override = default;
};

/** 单目运算：UnaryOp UnaryExp (+ / - / !) */
class UnaryExp : public Exp {
public:
    std::unique_ptr<Exp> operand;
    OpType op;

    UnaryExp(std::unique_ptr<Exp> operand, OpType op) : operand(std::move(operand)), op(op) {}
    ~UnaryExp() override = default;
};

/** 函数实参表：Exp { ',' Exp } */
class FuncRParams : public ASTNode {
public:
    std::vector<std::unique_ptr<Exp>> func_r_params;

    explicit FuncRParams(std::vector<std::unique_ptr<Exp>> func_r_params) : func_r_params(std::move(func_r_params)) {}
    ~FuncRParams() override = default;
};

/** 函数调用：Ident '(' [FuncRParams] ')' */
class FuncCall : public Exp {
public:
    std::string ident;
    std::unique_ptr<FuncRParams> func_r_params; // 可为空表示无实参

    FuncCall(std::string ident, std::unique_ptr<FuncRParams> func_r_params) : ident(std::move(ident)), func_r_params(std::move(func_r_params)) {}
    ~FuncCall() override = default;
};

/**
 * 常量表达式 ConstExp → AddExp。
 * 文法上等价于 Exp，此处用包装节点区分“仅允许常量”的语义，便于后续语义分析。
 */
class ConstExp : public Exp {
public:
    std::unique_ptr<Exp> inner;

    explicit ConstExp(std::unique_ptr<Exp> inner) : inner(std::move(inner)) {}
    ~ConstExp() override = default;
};

// =============================================================================
// 初值节点：ConstInitVal / InitVal（递归结构）
// ConstInitVal → ConstExp | '{' [ ConstExp { ',' ConstExp } ] '}' | StringConst
// InitVal      → Exp | '{' [ Exp { ',' Exp } ] '}' | StringConst
// 实现方案：用 kind 区分“单表达式 / 列表 / 字符串”，列表为 vector<unique_ptr<同类型>>。
// =============================================================================
enum class InitValKind { SINGLE_EXP,
                         LIST,
                         STRING };

class ConstInitVal : public ASTNode {
public:
    InitValKind kind;
    std::unique_ptr<Exp> single_exp;                 // SINGLE_EXP 时有效
    std::vector<std::unique_ptr<ConstInitVal>> list; // LIST 时有效
    std::string string_val;                          // STRING 时有效

    ConstInitVal(InitValKind kind, std::unique_ptr<Exp> single_exp,
                 std::vector<std::unique_ptr<ConstInitVal>> list,
                 std::string string_val) : kind(kind),
                                           single_exp(std::move(single_exp)),
                                           list(std::move(list)),
                                           string_val(std::move(string_val)) {}
    ~ConstInitVal() override = default;
};

class InitVal : public ASTNode {
public:
    InitValKind kind;
    std::unique_ptr<Exp> single_exp;
    std::vector<std::unique_ptr<InitVal>> list;
    std::string string_val;

    InitVal(InitValKind kind, std::unique_ptr<Exp> single_exp,
            std::vector<std::unique_ptr<InitVal>> list, std::string string_val) : kind(kind),
                                                                                  single_exp(std::move(single_exp)),
                                                                                  list(std::move(list)),
                                                                                  string_val(std::move(string_val)) {}
    ~InitVal() override = default;
};

// =============================================================================
// 定义层次 (Def)：ConstDef / VarDef
// =============================================================================
class Def : public ASTNode {
public:
    ~Def() override = default;
};

class ConstDef : public Def {
public:
    std::string ident;
    std::vector<std::unique_ptr<Exp>> dims; // 维度用 ConstExp，此处用 Exp 表示
    std::unique_ptr<ConstInitVal> const_init_val;

    ConstDef(std::string ident, std::vector<std::unique_ptr<Exp>> dims,
             std::unique_ptr<ConstInitVal> const_init_val) : ident(std::move(ident)),
                                                             dims(std::move(dims)),
                                                             const_init_val(std::move(const_init_val)) {}
    ~ConstDef() override = default;
};

class VarDef : public Def {
public:
    std::string ident;
    std::vector<std::unique_ptr<Exp>> dims;
    std::unique_ptr<InitVal> init_val; // 可选，无 '=' 时为空

    VarDef(std::string ident, std::vector<std::unique_ptr<Exp>> dims,
           std::unique_ptr<InitVal> init_val) : ident(std::move(ident)),
                                                dims(std::move(dims)),
                                                init_val(std::move(init_val)) {}
    ~VarDef() override = default;
};

// =============================================================================
// ConstDecl / VarDecl（依赖 Def / ConstDef / VarDef）
// =============================================================================
class ConstDecl : public Decl {
public:
    BType btype;
    std::vector<std::unique_ptr<ConstDef>> const_defs;

    ConstDecl(BType btype, std::vector<std::unique_ptr<ConstDef>> const_defs) : btype(btype), const_defs(std::move(const_defs)) {}
    ~ConstDecl() override = default;
};

class VarDecl : public Decl {
public:
    BType btype;
    std::vector<std::unique_ptr<VarDef>> var_defs;

    VarDecl(BType btype, std::vector<std::unique_ptr<VarDef>> var_defs) : btype(btype), var_defs(std::move(var_defs)) {}
    ~VarDecl() override = default;
};

// =============================================================================
// 函数形参 FuncFParam → BType Ident ['[' ']']
// =============================================================================
class FuncFParam : public ASTNode {
public:
    BType btype;
    std::string ident;
    bool is_array; // 是否有 '[' ']'

    FuncFParam(BType btype, std::string ident, bool is_array) : btype(btype), ident(std::move(ident)), is_array(is_array) {}
    ~FuncFParam() override = default;
};

// =============================================================================
// 语句块 (Block)：须在 Stmt 之前定义，因 BlockStmt 持有 unique_ptr<Block>
// =============================================================================
class Block : public ASTNode {
public:
    std::vector<std::unique_ptr<BlockItem>> block_items;

    explicit Block(std::vector<std::unique_ptr<BlockItem>> block_items) : block_items(std::move(block_items)) {}
    ~Block() override = default;
};

// =============================================================================
// 语句 (Stmt) 家族：Stmt 继承自 BlockItem
// =============================================================================
class Stmt : public BlockItem {
public:
    ~Stmt() override = default;
};

/** LVal '=' Exp ';' */
class AssignStmt : public Stmt {
public:
    std::unique_ptr<LVal> lval;
    std::unique_ptr<Exp> exp;

    AssignStmt(std::unique_ptr<LVal> lval, std::unique_ptr<Exp> exp) : lval(std::move(lval)), exp(std::move(exp)) {}
    ~AssignStmt() override = default;
};

/** [Exp] ';' */
class ExpStmt : public Stmt {
public:
    std::optional<std::unique_ptr<Exp>> exp; // 可为空

    explicit ExpStmt(std::optional<std::unique_ptr<Exp>> exp) : exp(std::move(exp)) {}
    ~ExpStmt() override = default;
};

/** Block 作为语句（直接复用 Block 节点） */
class BlockStmt : public Stmt {
public:
    std::unique_ptr<Block> block;

    explicit BlockStmt(std::unique_ptr<Block> block) : block(std::move(block)) {}
    ~BlockStmt() override = default;
};

/** if ( Cond ) Stmt [ else Stmt ] */
class IfStmt : public Stmt {
public:
    std::unique_ptr<Exp> cond;
    std::unique_ptr<Stmt> then_stmt;
    std::unique_ptr<Stmt> else_stmt; // 可为空

    IfStmt(std::unique_ptr<Exp> cond, std::unique_ptr<Stmt> then_stmt,
           std::unique_ptr<Stmt> else_stmt) : cond(std::move(cond)),
                                              then_stmt(std::move(then_stmt)),
                                              else_stmt(std::move(else_stmt)) {}
    ~IfStmt() override = default;
};

/** for ( [ForStmt] ; [Cond] ; [ForStmt] ) Stmt */
class ForStmt : public Stmt {
public:
    std::optional<std::unique_ptr<Stmt>> init; // 第一个 ForStmt（赋值）
    std::optional<std::unique_ptr<Exp>> cond;
    std::optional<std::unique_ptr<Stmt>> step; // 第二个 ForStmt（赋值）
    std::unique_ptr<Stmt> body;

    ForStmt(std::optional<std::unique_ptr<Stmt>> init,
            std::optional<std::unique_ptr<Exp>> cond,
            std::optional<std::unique_ptr<Stmt>> step,
            std::unique_ptr<Stmt> body) : init(std::move(init)),
                                          cond(std::move(cond)),
                                          step(std::move(step)),
                                          body(std::move(body)) {}
    ~ForStmt() override = default;
};

// /** while ( Cond ) Stmt（若文法扩展支持 while，可复用此节点） */
// class WhileStmt : public Stmt {
// public:
//     std::unique_ptr<Exp> cond;
//     std::unique_ptr<Stmt> body;

//     WhileStmt(std::unique_ptr<Exp> cond, std::unique_ptr<Stmt> body) : cond(std::move(cond)), body(std::move(body)) {}
//     ~WhileStmt() override = default;
// };

class BreakStmt : public Stmt {
public:
    BreakStmt() = default;
    ~BreakStmt() override = default;
};

class ContinueStmt : public Stmt {
public:
    ContinueStmt() = default;
    ~ContinueStmt() override = default;
};

/** return [Exp] ';' */
class ReturnStmt : public Stmt {
public:
    std::optional<std::unique_ptr<Exp>> exp;

    explicit ReturnStmt(std::optional<std::unique_ptr<Exp>> exp) : exp(std::move(exp)) {}
    ~ReturnStmt() override = default;
};

/** LVal '=' getint() ';' */
class GetintStmt : public Stmt {
public:
    std::unique_ptr<LVal> lval;

    explicit GetintStmt(std::unique_ptr<LVal> lval) : lval(std::move(lval)) {}
    ~GetintStmt() override = default;
};

/** LVal '=' getchar() ';' */
class GetcharStmt : public Stmt {
public:
    std::unique_ptr<LVal> lval;

    explicit GetcharStmt(std::unique_ptr<LVal> lval) : lval(std::move(lval)) {}
    ~GetcharStmt() override = default;
};

/** printf ( StringConst { ',' Exp } ) ';' */
class PrintfStmt : public Stmt {
public:
    std::string format_string;
    std::vector<std::unique_ptr<Exp>> exp_list;

    PrintfStmt(std::string format_string,
               std::vector<std::unique_ptr<Exp>> exp_list) : format_string(std::move(format_string)),
                                                             exp_list(std::move(exp_list)) {}
    ~PrintfStmt() override = default;
};

// =============================================================================
// 函数定义与主函数
// =============================================================================
class FuncDef : public ASTNode {
public:
    BType func_type;
    std::string ident;
    std::vector<std::unique_ptr<FuncFParam>> func_f_params;
    std::unique_ptr<Block> block;

    FuncDef(BType func_type, std::string ident,
            std::vector<std::unique_ptr<FuncFParam>> func_f_params,
            std::unique_ptr<Block> block) : func_type(func_type),
                                            ident(std::move(ident)),
                                            func_f_params(std::move(func_f_params)),
                                            block(std::move(block)) {}
    ~FuncDef() override = default;
};

class MainFuncDef : public ASTNode {
public:
    std::unique_ptr<Block> block;

    explicit MainFuncDef(std::unique_ptr<Block> block) : block(std::move(block)) {}
    ~MainFuncDef() override = default;
};

// =============================================================================
// 编译单元 (CompUnit) — 根节点
// =============================================================================
class CompUnit : public ASTNode {
public:
    std::vector<std::unique_ptr<Decl>> decls;
    std::vector<std::unique_ptr<FuncDef>> func_defs;
    std::unique_ptr<MainFuncDef> main_func_def;

    CompUnit(std::vector<std::unique_ptr<Decl>> decls,
             std::vector<std::unique_ptr<FuncDef>> func_defs,
             std::unique_ptr<MainFuncDef> main_func_def) : decls(std::move(decls)),
                                                           func_defs(std::move(func_defs)),
                                                           main_func_def(std::move(main_func_def)) {}
    ~CompUnit() override = default;
};

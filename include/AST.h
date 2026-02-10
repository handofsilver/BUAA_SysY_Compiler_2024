#pragma once

#include <memory>
#include <string>
#include <vector>

// =============================================================================
// 前向声明 (Forward Declarations)
// 用于解决循环依赖：例如 Block 被 FuncDef 持有，而 Block 内又可能持有 Stmt/Decl 等。
// 若后续 BlockItem 需要引用 Stmt，而 Stmt 又引用 Block，可在此处前向声明 Stmt。
// =============================================================================

// 若需要可在此添加: class Stmt;
class ConstDef;
class VarDef;
class Block;
class FuncDef;
class MainFuncDef;
class Decl;
class ConstDecl;
class VarDecl;
class Def;
class CompUnit;

// =============================================================================
// 基类 (Base Class)
// =============================================================================

/**
 * AST 节点基类。
 * - 虚析构函数保证：通过基类指针删除时，会调用到派生类的析构函数（Rule of Base Class）。
 * - 禁止拷贝，避免意外复制整棵 AST；移动语义可按需在派生类中开放。
 */

enum class BType {
    INT,
    CHAR,
    VOID // 既然 FuncType 也有 void，不如合并不然就分开定义
};

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
// 声明层次 (Decl): Decl → ConstDecl | VarDecl
// =============================================================================

/**
 * 声明的抽象基类，对应文法 Decl → ConstDecl | VarDecl。
 * 使用继承而非「一个类里两个可选引用」，避免 Java 式的 null 分支。
 */
class Decl : public ASTNode {
public:
    ~Decl() override = default;
};

class ConstDecl : public Decl {
public:
    // TODO(student): 参考 Java 的 ConstDecl.java
    // 1. 需要基本类型 (BType: 'int' | 'char'，可用 enum 或 string)
    // 2. 需要常量定义列表 (std::vector<std::unique_ptr<ConstDef>>)

    // TODO(student): 实现 ToString()
};

class VarDecl : public Decl {
public:
    // TODO(student): 参考 Java 的 VarDecl.java
    // 1. 需要基本类型 (BType)
    // 2. 需要变量定义列表 (std::vector<std::unique_ptr<VarDef>>)

    // TODO(student): 实现 ToString()
};

// =============================================================================
// 定义层次 (Def): 通用定义节点，ConstDef / VarDef 的父类
// =============================================================================

/**
 * 定义的抽象基类，对应 ConstDef、VarDef 的公共抽象。
 * 文法: ConstDef → Ident [ '[' ConstExp ']' ] '=' ConstInitVal
 *       VarDef   → Ident [ '[' ConstExp ']' ] | Ident [ '[' ConstExp ']' ] '=' InitVal
 */
class Def : public ASTNode {
public:
    ~Def() override = default;
};

class ConstDef : public Def {
public:
    // TODO(student): 参考 Java 的 ConstDef.java
    // 1. 标识符名 (std::string)
    // 2. 可选：数组维度 (ConstExp，可能多维，可用 vector<std::unique_ptr<ConstExp>>)
    // 3. 常量初值 (ConstInitVal，需定义 ConstInitVal 节点或 unique_ptr)

    // 构造函数待补全成员变量后再写
};

class VarDef : public Def {
public:
    // TODO(student): 参考 Java 的 VarDef.java
    // 1. 标识符名 (std::string)
    // 2. 可选：数组维度 (ConstExp)
    // 3. 可选：变量初值 (InitVal)，仅带 '=' 的 VarDef 有

    // 构造函数待补全成员变量后再写
};

// =============================================================================
// 语句块 (Block)
// =============================================================================

/**
 * 语句块，对应 Block → '{' { BlockItem } '}'。
 * BlockItem → Decl | Stmt，故块内为声明或语句的序列。
 */
class Block : public ASTNode {
public:
    ~Block() override = default;

    // TODO(student): 参考 Java 的 Block.java
    // 1. BlockItem 列表 (BlockItem → Decl | Stmt)
    //    可用 std::vector<std::unique_ptr<ASTNode>> 或
    //    std::vector<std::unique_ptr<Decl>> + std::vector<std::unique_ptr<Stmt>> 等方案
    // 2. 若引入 BlockItem 包装类，注意与 Stmt 的循环依赖，配合前向声明

    // 构造函数待补全成员变量后再写
};

// =============================================================================
// 函数定义 (FuncDef) 与主函数 (MainFuncDef)
// =============================================================================

/**
 * 函数定义，对应 FuncDef → FuncType Ident '(' [FuncFParams] ')' Block。
 */
class FuncDef : public ASTNode {
public:
    ~FuncDef() override = default;

    // TODO(student): 参考 Java 的 FuncDef.java
    // 1. 函数类型 FuncType ('void' | 'int' | 'char'，建议 enum 或 string)
    // 2. 函数名 (std::string)
    // 3. 形参列表 (std::vector<std::unique_ptr<FuncFParam>>，需另定义 FuncFParam 节点)
    // 4. 函数体 (std::unique_ptr<Block>)

    // 构造函数待补全成员变量后再写
};

/**
 * 主函数定义，对应 MainFuncDef → 'int' 'main' '(' ')' Block。
 */
class MainFuncDef : public ASTNode {
public:
    ~MainFuncDef() override = default;

    // TODO(student): 参考 Java 的 MainFuncDef.java
    // 1. 函数体 (std::unique_ptr<Block>)

    // 构造函数待补全成员变量后再写
};

// =============================================================================
// 编译单元 (CompUnit) — 根节点
// =============================================================================

/**
 * 编译单元根节点，对应 CompUnit → {Decl} {FuncDef} MainFuncDef。
 * 所有权：本节点独占所有 decls_、func_defs_ 以及 main_func_def_。
 */
class CompUnit : public ASTNode {
public:
    ~CompUnit() override = default;

    // TODO(student): 参考 Java 的 CompUnit.java
    // 1. 全局声明列表 (std::vector<std::unique_ptr<Decl>>)
    // 2. 函数定义列表 (std::vector<std::unique_ptr<FuncDef>>)
    // 3. 主函数 (std::unique_ptr<MainFuncDef>)

    // 构造函数待补全成员变量后再写
};

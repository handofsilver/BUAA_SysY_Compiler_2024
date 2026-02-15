/**
 * @file IRGenVisitor.h
 * @brief AST visitor that generates in-memory LLVM IR (Module / Function / BasicBlock /
 * Instruction).
 *
 * Uses IRBuilder to create instructions and maintains scope and mode flags for
 * declarations, left-value vs value use, and control-flow targets.
 */
#pragma once

#include "IRBuilder.h"
#include "ir/Function.h"
#include "ir/Module.h"
#include <ASTVisitor.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

class IRGenVisitor : public ASTVisitor {
public:
    IRGenVisitor();

    /** @brief Take ownership of the generated module (e.g. for printing or further passes). */
    std::unique_ptr<ir::Module> GetModule();

    // -------------------------------------------------------------------------
    // ASTVisitor overrides
    // -------------------------------------------------------------------------

    void VisitCompUnit(CompUnit& comp_unit) override;
    void VisitConstDecl(ConstDecl& const_decl) override;
    void VisitVarDecl(VarDecl& var_decl) override;
    void VisitBlock(Block& block) override;
    void VisitAssignStmt(AssignStmt& assign_stmt) override;
    void VisitExpStmt(ExpStmt& exp_stmt) override;
    void VisitBlockStmt(BlockStmt& block_stmt) override;
    void VisitIfStmt(IfStmt& if_stmt) override;
    void VisitForStmt(ForStmt& for_stmt) override;
    void VisitBreakStmt(BreakStmt& break_stmt) override;
    void VisitContinueStmt(ContinueStmt& continue_stmt) override;
    void VisitReturnStmt(ReturnStmt& return_stmt) override;
    void VisitGetintStmt(GetintStmt& getint_stmt) override;
    void VisitGetcharStmt(GetcharStmt& getchar_stmt) override;
    void VisitPrintfStmt(PrintfStmt& printf_stmt) override;
    void VisitLVal(LVal& lval) override;
    void VisitNumber(Number& number) override;
    void VisitCharacter(Character& character) override;
    void VisitBinaryExp(BinaryExp& binary_exp) override;
    void VisitUnaryExp(UnaryExp& unary_exp) override;
    void VisitFuncCall(FuncCall& func_call) override;
    void VisitFuncRParams(FuncRParams& func_r_params) override;
    void VisitConstExp(ConstExp& const_exp) override;
    void VisitConstDef(ConstDef& const_def) override;
    void VisitVarDef(VarDef& var_def) override;
    void VisitFuncFParam(FuncFParam& func_f_param) override;
    void VisitForInitOrStep(ForInitOrStep& for_init_or_step) override;
    void VisitFuncDef(FuncDef& func_def) override;
    void VisitMainFuncDef(MainFuncDef& main_func_def) override;
    void VisitConstInitVal(ConstInitVal& const_init_val) override;
    void VisitInitVal(InitVal& init_val) override;

private:
    // -------------------------------------------------------------------------
    // Core components
    // -------------------------------------------------------------------------

    std::unique_ptr<ir::Module> module_;
    std::unique_ptr<ir::IRBuilder> builder_;

    /**
     * @brief Last computed value from the most recent expression/operand visit.
     * Used to pass result from child to parent (e.g. from Number to AddExp).
     */
    ir::Value* temp_value_ = nullptr;

    /**
     * @brief When true, LVal is visited as a real left-value (address for store).
     * When false, LVal is used as a value (need load after address).
     */
    bool is_lval_mode_ = false;

    /**
     * @brief When true, we are in global scope (CompUnit-level Decl); declarations
     * produce global variables/constants instead of alloca.
     */
    bool is_global_ = false;

    /**
     * @brief The function currently being generated. Used when creating new basic
     * blocks and setting the builder insertion point.
     */
    ir::Function* current_function_ = nullptr;

    /**
     * @brief Stack of basic blocks for break targets (innermost loop end). Push on
     * entering a loop, pop on exit; break generates br to top of stack.
     */
    std::vector<ir::BasicBlock*> break_targets_;

    /**
     * @brief Stack of basic blocks for continue targets (loop step/cond). Push on
     * entering a loop, pop on exit; continue generates br to top of stack.
     */
    std::vector<ir::BasicBlock*> continue_targets_;

    // -------------------------------------------------------------------------
    // Symbol table (scope chain)
    // -------------------------------------------------------------------------

    struct Scope {
        int id;
        std::map<std::string, ir::Value*> map;
    };
    std::vector<Scope> scopes_;
    int next_scope_id_ = 1;
    int current_scope_id_ = 0;

    /** @brief Look up a variable by name in the scope chain (inner to outer). */
    ir::Value* LookupVariable(const std::string& name);

    /** @brief Push a new scope (e.g. on entering a block). */
    void PushScope();

    /** @brief Pop the current scope (e.g. on leaving a block). */
    void PopScope();
};

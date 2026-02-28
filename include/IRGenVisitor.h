/**
 * @file IRGenVisitor.h
 * @brief AST visitor that generates in-memory LLVM IR (Module / Function / BasicBlock /
 * Instruction).
 *
 * Uses IRBuilder to create instructions and maintains traversal-specific mode flags.
 * Environment state (resource pointers, symbol table) lives in IRGenContext.
 */
#pragma once

#include "AST.h"
#include "ASTVisitor.h"
#include "IRGenContext.h"
#include "ir/Function.h"
#include "ir/Module.h"
#include "ir/TypeManager.h"
#include <memory>
#include <string>
#include <vector>

class IRGenVisitor : public ASTVisitor {
public:
    IRGenVisitor();

    /** @brief Translate the given CompUnit into an LLVM IR Module. */
    std::unique_ptr<ir::Module> Translate(CompUnit& comp_unit);

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
    // Owned resources (lifetime managed by IRGenVisitor)
    // -------------------------------------------------------------------------

    std::unique_ptr<ir::Module> module_;
    std::unique_ptr<ir::IRBuilder> builder_;
    ir::TypeManager& types_ = ir::TypeManager::Get();

    // -------------------------------------------------------------------------
    // IR generation context (non-owning pointers into resources above + scope)
    // -------------------------------------------------------------------------

    IRGenContext ctx_;

    // -------------------------------------------------------------------------
    // Traversal state (MUST stay in IRGenVisitor, not in Context)
    // -------------------------------------------------------------------------

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
     * @brief When true, the next LVal (array/ptr) used as function argument should pass
     * the address (for pointer parameters), not the loaded value. Set per-argument in
     * VisitFuncCall when collecting call args.
     */
    bool func_arg_want_pointer_ = false;

    /**
     * @brief When true, we are in global scope (CompUnit-level Decl); declarations
     * produce global variables/constants instead of alloca.
     */
    bool is_global_ = false;

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

    /** BType of the current Decl (ConstDecl/VarDecl) for VisitConstDef/VisitVarDef. */
    BType current_decl_btype_ = BType::INT;

    /**
     * @brief Arguments for the current function call (filled by VisitFuncRParams, used by
     * VisitFuncCall).
     */
    std::vector<ir::Value*> call_args_;

    /** Counter for unique .str.N names in printf string literals. */
    int printf_str_counter_ = 0;

    // -------------------------------------------------------------------------
    // Helper methods
    // -------------------------------------------------------------------------

    /**
     * @brief Create a new BasicBlock, add it to ctx_.current_function, return raw pointer.
     * Labels are unique per function: "entry" is kept as-is; others get "name.N" using
     * ctx_.builder->GetNextSSAName() (reuses SSA counter, no extra block_counter).
     */
    ir::BasicBlock* CreateBasicBlock(const std::string& name);

    /**
     * @brief True if current insert block is null, empty, or last instruction is Br/Ret.
     */
    bool IsBlockTerminated() const;

    /**
     * @brief Coerce a condition value to i1 (for br). If already i1, return as-is; else icmp ne
     * val, 0.
     */
    ir::Value* CoerceToI1(ir::Value* cond_val);

    /**
     * @brief Emit short-circuit AND: lhs then (if true) rhs; result 0 when lhs is false, else rhs.
     * Sets temp_value_ to the merged result (i32) at the merge block.
     */
    void EmitShortCircuitAND(Exp* lhs, Exp* rhs);

    /**
     * @brief Emit short-circuit OR: lhs then (if false) rhs; result 1 when lhs is true, else rhs.
     * Sets temp_value_ to the merged result (i32) at the merge block.
     */
    void EmitShortCircuitOR(Exp* lhs, Exp* rhs);

    /** @brief Create alloca in entry block; name is next SSA number from ctx_.builder. */
    ir::Instruction* CreateEntryBlockAlloca(ir::Type* type);

    // -------------------------------------------------------------------------
    // Implicit type conversion (SysY int/char; see docs/ai_collab_notes/type_conversion.md)
    // -------------------------------------------------------------------------

    /** @brief Get pointee type of a pointer Value*, or nullptr if not a pointer. */
    ir::Type* GetPointeeType(ir::Value* ptr) const;

    /**
     * @brief Promote value to i32 for arithmetic/condition. If already i32/i1, return as-is; if i8,
     * insert zext i8 to i32. Other types return as-is.
     */
    ir::Value* PromoteToI32(ir::Value* v);

    /**
     * @brief Convert value to target scalar type (i8 or i32). Insert trunc i32->i8 or zext i8->i32
     * as needed; if types already match, return v. Non-scalar or null returns v unchanged.
     */
    ir::Value* ConvertToTargetType(ir::Value* v, ir::Type* target_ty);

    // -------------------------------------------------------------------------
    // Helpers for variable/constant definition (modularize VisitConstDef/VisitVarDef)
    // -------------------------------------------------------------------------

    /** Element type (i32 or i8) from current_decl_btype_. */
    ir::Type* GetCurDeclType() const;

    /** Array size from a ConstExp (e.g. array_size); returns >= 1. */
    int EvalArraySizeFromConstExp(ConstExp* cexp);

    /** Build a constant scalar initializer (i32 or i8 constant). */
    ir::ConstantInt* BuildConstScalarInit(int val) const;

    /** Build a constant array initializer from a list of integer values. */
    ir::ConstantArray* BuildConstArrayInit(ir::ArrayType* arr_ty,
                                           const std::vector<int>& values) const;

    void EmitGlobalConstDef(ConstDef& const_def, ir::Type* elem_type);
    void EmitLocalConstDef(ConstDef& const_def, ir::Type* elem_type);
    void EmitGlobalVarDef(VarDef& var_def, ir::Type* elem_type);
    void EmitLocalVarDef(VarDef& var_def, ir::Type* elem_type);

    /**
     * @brief Create a global constant string (i8 array, null-terminated) for printf; returns
     * pointer Value* to pass to putstr. Uses printf_str_counter_ for unique names.
     */
    ir::Value* EmitGlobalStringLiteral(const std::string& str);
};

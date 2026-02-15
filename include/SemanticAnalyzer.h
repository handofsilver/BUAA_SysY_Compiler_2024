#pragma once

#include "AST.h"
#include "ASTVisitor.h"
#include "ScopeGuard.h"
#include "Symbol.h"
#include "SymbolTable.h"
#include <string_view>
#include <vector>

/**
 * Semantic analysis visitor: symbol registration, scope management, error collection.
 * Traversal is visitor-driven (each VisitXxx recurses to children in the desired order).
 *
 * Design (see refactoring_semantic_analysis_qa.md):
 * - VisitBlock: push scope (ScopeGuard), then visit each block item.
 * - VisitFuncDef: register function in current (global) scope; push one scope for
 *   params + body; visit params; then visit block's block_items directly (do NOT
 *   call block->Accept again, so we don't push scope twice).
 * - VisitMainFuncDef: do not register "main"; push scope, visit block contents.
 */
class SemanticAnalyzer : public ASTVisitor {
public:
    SemanticAnalyzer() = default;

    /** Entry: run semantic analysis on root. Returns false if any errors were recorded. */
    bool Analyze(CompUnit& root);

    const std::vector<std::pair<int, std::string>>& GetErrorLog() const {
        return error_log_;
    }
    /** For symbol.txt: (scope_id, symbol) in requirement order (scope then declaration). */
    const SymbolTable::OrderedSymbolList& GetOrderedSymbols() const {
        return ordered_symbols_;
    }

    // -------------------------------------------------------------------------
    // ASTVisitor overrides (implemented in SemanticAnalyzer.cpp)
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
    SymbolTable symbol_table_;
    std::vector<std::pair<int, std::string>> error_log_;
    /** Appended on each successful Register; used for symbol.txt output. */
    SymbolTable::OrderedSymbolList ordered_symbols_;

    /** Record semantic error (line, error_code e.g. "b", "c"). Line is 1-based; <=0 is clamped
     * to 1. */
    void RecordError(int line, std::string_view code);

    /**
     * Register symbol in current scope and append to ordered_symbols_ for output.
     * Returns true if registered (no redefinition); false if redefinition (error already recorded).
     */
    bool RegisterSymbol(const std::string& name, Symbol symbol, int line);

    /** Visit block's block_items only (no scope push). Used from VisitFuncDef / VisitMainFuncDef.
     */
    void VisitBlockContents(Block& block);

    /** Current function return type (for return/void check). BType::VOID = void function. */
    BType current_func_type_ = BType::VOID;
    /** Nesting depth of loops (for break/continue). */
    int loop_depth_ = 0;
    /** BType of the current Decl (ConstDecl/VarDecl) for VisitConstDef/VisitVarDef. */
    BType current_decl_btype_ = BType::INT;
    /** True when visiting LVal as left-hand side of assignment (then check const -> h). */
    bool lval_is_left_of_assign_ = false;

    /** Last value calculated in constant folding. */
    int last_value_ = 0;

    /** Last values (e.g. const init list). */
    std::vector<int> last_values_;

    /**
     * After visiting an Exp used as a function argument: true if that expression
     * has array type (e.g. LVal without index, symbol is array). Used for error e.
     */
    bool current_exp_is_array_ = false;
};

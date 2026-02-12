#pragma once

#include "AST.h"
#include "ASTVisitor.h"
#include "ScopeGuard.h"
#include "Symbol.h"
#include "SymbolTable.h"
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

    const std::vector<std::pair<int, std::string>>& GetErrors() const {
        return errors_;
    }
    /** For symbol.txt: (scope_id, symbol) in requirement order (scope then declaration). */
    const SymbolTable::OrderedSymbolList& GetOrderedSymbols() const {
        return ordered_symbols_;
    }

    // -------------------------------------------------------------------------
    // ASTVisitor overrides (implemented in SemanticAnalyzer.cpp)
    // -------------------------------------------------------------------------
    void VisitCompUnit(CompUnit& node) override;
    void VisitConstDecl(ConstDecl& node) override;
    void VisitVarDecl(VarDecl& node) override;
    void VisitBlock(Block& node) override;
    void VisitAssignStmt(AssignStmt& node) override;
    void VisitExpStmt(ExpStmt& node) override;
    void VisitBlockStmt(BlockStmt& node) override;
    void VisitIfStmt(IfStmt& node) override;
    void VisitForStmt(ForStmt& node) override;
    void VisitBreakStmt(BreakStmt& node) override;
    void VisitContinueStmt(ContinueStmt& node) override;
    void VisitReturnStmt(ReturnStmt& node) override;
    void VisitGetintStmt(GetintStmt& node) override;
    void VisitGetcharStmt(GetcharStmt& node) override;
    void VisitPrintfStmt(PrintfStmt& node) override;
    void VisitLVal(LVal& node) override;
    void VisitNumber(Number& node) override;
    void VisitCharacter(Character& node) override;
    void VisitBinaryExp(BinaryExp& node) override;
    void VisitUnaryExp(UnaryExp& node) override;
    void VisitFuncCall(FuncCall& node) override;
    void VisitConstExp(ConstExp& node) override;
    void VisitConstDef(ConstDef& node) override;
    void VisitVarDef(VarDef& node) override;
    void VisitFuncFParam(FuncFParam& node) override;
    void VisitForInitOrStep(ForInitOrStep& node) override;
    void VisitFuncDef(FuncDef& node) override;
    void VisitMainFuncDef(MainFuncDef& node) override;
    void VisitConstInitVal(ConstInitVal& node) override;
    void VisitInitVal(InitVal& node) override;

private:
    SymbolTable symbol_table_;
    std::vector<std::pair<int, std::string>> errors_;
    /** Appended on each successful Register; used for symbol.txt output. */
    SymbolTable::OrderedSymbolList ordered_symbols_;

    /** Record semantic error (line, error_code e.g. "b", "c"). */
    void RecordError(int line, const std::string& code);

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
};

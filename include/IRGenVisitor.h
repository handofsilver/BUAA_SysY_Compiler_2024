#pragma once
#include <ASTVisitor.h>

class IRGenVisitor : public ASTVisitor {
public:
    IRGenVisitor() = default;

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
};

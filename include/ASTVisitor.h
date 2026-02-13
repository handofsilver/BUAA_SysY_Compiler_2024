#pragma once

// =============================================================================
// Forward declarations for all concrete AST node types.
// Accept() will dispatch to the corresponding VisitXxx(); definitions are in AST.cpp.
// =============================================================================
class CompUnit;
class ConstDecl;
class VarDecl;
class Block;
class AssignStmt;
class ExpStmt;
class BlockStmt;
class IfStmt;
class ForStmt;
class BreakStmt;
class ContinueStmt;
class ReturnStmt;
class GetintStmt;
class GetcharStmt;
class PrintfStmt;
class LVal;
class Number;
class Character;
class BinaryExp;
class UnaryExp;
class FuncCall;
class ConstExp;
class FuncRParams;
class ConstDef;
class VarDef;
class FuncFParam;
class ForInitOrStep;
class FuncDef;
class MainFuncDef;
class ConstInitVal;
class InitVal;

// =============================================================================
// Abstract visitor: double-dispatch entry for semantic analysis (and future IR gen).
// Traversal is visitor-driven: each VisitXxx() is responsible for calling
// Accept() on children in the desired order (e.g. push scope before block items).
// =============================================================================
class ASTVisitor {
public:
    virtual ~ASTVisitor() = default;

    virtual void VisitCompUnit(CompUnit& comp_unit) = 0;
    virtual void VisitConstDecl(ConstDecl& const_decl) = 0;
    virtual void VisitVarDecl(VarDecl& var_decl) = 0;
    virtual void VisitBlock(Block& block) = 0;
    virtual void VisitAssignStmt(AssignStmt& assign_stmt) = 0;
    virtual void VisitExpStmt(ExpStmt& exp_stmt) = 0;
    virtual void VisitBlockStmt(BlockStmt& block_stmt) = 0;
    virtual void VisitIfStmt(IfStmt& if_stmt) = 0;
    virtual void VisitForStmt(ForStmt& for_stmt) = 0;
    virtual void VisitBreakStmt(BreakStmt& break_stmt) = 0;
    virtual void VisitContinueStmt(ContinueStmt& continue_stmt) = 0;
    virtual void VisitReturnStmt(ReturnStmt& return_stmt) = 0;
    virtual void VisitGetintStmt(GetintStmt& getint_stmt) = 0;
    virtual void VisitGetcharStmt(GetcharStmt& getchar_stmt) = 0;
    virtual void VisitPrintfStmt(PrintfStmt& printf_stmt) = 0;
    virtual void VisitLVal(LVal& lval) = 0;
    virtual void VisitNumber(Number& number) = 0;
    virtual void VisitCharacter(Character& character) = 0;
    virtual void VisitBinaryExp(BinaryExp& binary_exp) = 0;
    virtual void VisitUnaryExp(UnaryExp& unary_exp) = 0;
    virtual void VisitFuncCall(FuncCall& func_call) = 0;
    virtual void VisitFuncRParams(FuncRParams& func_r_params) = 0;
    virtual void VisitConstExp(ConstExp& const_exp) = 0;
    virtual void VisitConstDef(ConstDef& const_def) = 0;
    virtual void VisitVarDef(VarDef& var_def) = 0;
    virtual void VisitFuncFParam(FuncFParam& func_f_param) = 0;
    virtual void VisitForInitOrStep(ForInitOrStep& for_init_or_step) = 0;
    virtual void VisitFuncDef(FuncDef& func_def) = 0;
    virtual void VisitMainFuncDef(MainFuncDef& main_func_def) = 0;
    virtual void VisitConstInitVal(ConstInitVal& const_init_val) = 0;
    virtual void VisitInitVal(InitVal& init_val) = 0;
};

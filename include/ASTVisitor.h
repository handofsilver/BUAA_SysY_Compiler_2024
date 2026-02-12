#pragma once

// =============================================================================
// Forward declarations for all concrete AST node types.
// Accept() will dispatch to the corresponding VisitXxx(); definitions are in AST.cpp.
// =============================================================================
class CompUnit;
class Decl;
class ConstDecl;
class VarDecl;
class Block;
class BlockItem;
class Stmt;
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
class Exp;
class LVal;
class Number;
class Character;
class BinaryExp;
class UnaryExp;
class FuncCall;
class ConstExp;
class FuncRParams;
class Def;
class ConstDef;
class VarDef;
class ConstInitVal;
class InitVal;
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

    virtual void VisitCompUnit(CompUnit& node) = 0;
    virtual void VisitConstDecl(ConstDecl& node) = 0;
    virtual void VisitVarDecl(VarDecl& node) = 0;
    virtual void VisitBlock(Block& node) = 0;
    virtual void VisitAssignStmt(AssignStmt& node) = 0;
    virtual void VisitExpStmt(ExpStmt& node) = 0;
    virtual void VisitBlockStmt(BlockStmt& node) = 0;
    virtual void VisitIfStmt(IfStmt& node) = 0;
    virtual void VisitForStmt(ForStmt& node) = 0;
    virtual void VisitBreakStmt(BreakStmt& node) = 0;
    virtual void VisitContinueStmt(ContinueStmt& node) = 0;
    virtual void VisitReturnStmt(ReturnStmt& node) = 0;
    virtual void VisitGetintStmt(GetintStmt& node) = 0;
    virtual void VisitGetcharStmt(GetcharStmt& node) = 0;
    virtual void VisitPrintfStmt(PrintfStmt& node) = 0;
    virtual void VisitLVal(LVal& node) = 0;
    virtual void VisitNumber(Number& node) = 0;
    virtual void VisitCharacter(Character& node) = 0;
    virtual void VisitBinaryExp(BinaryExp& node) = 0;
    virtual void VisitUnaryExp(UnaryExp& node) = 0;
    virtual void VisitFuncCall(FuncCall& node) = 0;
    virtual void VisitConstExp(ConstExp& node) = 0;
    virtual void VisitConstDef(ConstDef& node) = 0;
    virtual void VisitVarDef(VarDef& node) = 0;
    virtual void VisitFuncFParam(FuncFParam& node) = 0;
    virtual void VisitForInitOrStep(ForInitOrStep& node) = 0;
    virtual void VisitFuncDef(FuncDef& node) = 0;
    virtual void VisitMainFuncDef(MainFuncDef& node) = 0;
    virtual void VisitConstInitVal(ConstInitVal& node) = 0;
    virtual void VisitInitVal(InitVal& node) = 0;
};

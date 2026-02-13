/**
 * Double-dispatch: each node's Accept() calls the corresponding VisitXxx on the visitor.
 * Keeps AST.h free of ASTVisitor include; traversal order is fully controlled by the visitor.
 */
#include "AST.h"
#include "ASTVisitor.h"

#define ACCEPT(NODE_TYPE)                         \
    void NODE_TYPE::Accept(ASTVisitor& visitor) { \
        visitor.Visit##NODE_TYPE(*this);          \
    }

ACCEPT(CompUnit)
ACCEPT(ConstDecl)
ACCEPT(VarDecl)
ACCEPT(Block)
ACCEPT(AssignStmt)
ACCEPT(ExpStmt)
ACCEPT(BlockStmt)
ACCEPT(IfStmt)
ACCEPT(ForStmt)
ACCEPT(BreakStmt)
ACCEPT(ContinueStmt)
ACCEPT(ReturnStmt)
ACCEPT(GetintStmt)
ACCEPT(GetcharStmt)
ACCEPT(PrintfStmt)
ACCEPT(LVal)
ACCEPT(Number)
ACCEPT(Character)
ACCEPT(BinaryExp)
ACCEPT(UnaryExp)
ACCEPT(FuncCall)
ACCEPT(FuncRParams)
ACCEPT(ConstExp)
ACCEPT(ConstDef)
ACCEPT(VarDef)
ACCEPT(ConstInitVal)
ACCEPT(InitVal)
ACCEPT(FuncFParam)
ACCEPT(ForInitOrStep)
ACCEPT(FuncDef)
ACCEPT(MainFuncDef)

#undef ACCEPT

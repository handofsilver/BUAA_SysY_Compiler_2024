/**
 * @file IRGenVisitorDecl.cpp
 * @brief IRGenVisitor: CompUnit and declaration Visit* (const/var decl/def, init).
 */
#include "AST.h"
#include "IRGenVisitor.h"

// -----------------------------------------------------------------------------
// CompUnit and declarations (global vs local)
// -----------------------------------------------------------------------------

void IRGenVisitor::VisitCompUnit(CompUnit& comp_unit) {
    is_global_ = true;
    for (auto& d : comp_unit.decls) {
        d->Accept(*this);
    }
    is_global_ = false;
    for (auto& f : comp_unit.func_defs) {
        f->Accept(*this);
    }
    if (comp_unit.main_func_def) {
        comp_unit.main_func_def->Accept(*this);
    }
}

void IRGenVisitor::VisitConstDecl(ConstDecl& const_decl) {
    current_decl_btype_ = const_decl.btype;
    for (auto& def : const_decl.const_defs) {
        def->Accept(*this);
    }
}

void IRGenVisitor::VisitVarDecl(VarDecl& var_decl) {
    current_decl_btype_ = var_decl.btype;
    for (auto& def : var_decl.var_defs) {
        def->Accept(*this);
    }
}

void IRGenVisitor::VisitConstDef(ConstDef& const_def) {
    ir::Type* elem_type = GetCurDeclType();
    if (is_global_) {
        EmitGlobalConstDef(const_def, elem_type);
    } else {
        EmitLocalConstDef(const_def, elem_type);
    }
}

void IRGenVisitor::VisitVarDef(VarDef& var_def) {
    ir::Type* elem_type = GetCurDeclType();
    if (is_global_) {
        EmitGlobalVarDef(var_def, elem_type);
    } else {
        EmitLocalVarDef(var_def, elem_type);
    }
}

void IRGenVisitor::VisitConstInitVal(ConstInitVal& const_init_val) {
    // Initializer logic is inlined in VisitConstDef (temp_value_ cannot carry list).
    (void)const_init_val;
}

void IRGenVisitor::VisitInitVal(InitVal& init_val) {
    // Initializer logic is inlined in VisitVarDef.
    (void)init_val;
}

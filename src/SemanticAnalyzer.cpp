/**
 * Semantic analysis: symbol table, scope (RAII), error collection.
 * Visitor-driven traversal; FuncDef uses one scope for params+body and visits block contents
 * directly.
 */
#include "SemanticAnalyzer.h"
#include "AST.h"
#include <optional>
#include <variant>

namespace {

    BType ToBType(SymbolType t) {
        if (t == SymbolType::INT || t == SymbolType::INT_FUNC || t == SymbolType::CONST_INT ||
            t == SymbolType::INT_ARRAY || t == SymbolType::CONST_INT_ARRAY)
            return BType::INT;
        if (t == SymbolType::CHAR || t == SymbolType::CHAR_FUNC || t == SymbolType::CONST_CHAR ||
            t == SymbolType::CHAR_ARRAY || t == SymbolType::CONST_CHAR_ARRAY)
            return BType::CHAR;
        return BType::VOID;
    }

    SymbolType BTypeToConstScalar(BType b) {
        return b == BType::INT ? SymbolType::CONST_INT : SymbolType::CONST_CHAR;
    }
    SymbolType BTypeToVarScalar(BType b) {
        return b == BType::INT ? SymbolType::INT : SymbolType::CHAR;
    }
    SymbolType BTypeToConstArray(BType b) {
        return b == BType::INT ? SymbolType::CONST_INT_ARRAY : SymbolType::CONST_CHAR_ARRAY;
    }
    SymbolType BTypeToVarArray(BType b) {
        return b == BType::INT ? SymbolType::INT_ARRAY : SymbolType::CHAR_ARRAY;
    }
    SymbolType BTypeToFunc(BType b) {
        if (b == BType::VOID)
            return SymbolType::VOID_FUNC;
        if (b == BType::INT)
            return SymbolType::INT_FUNC;
        return SymbolType::CHAR_FUNC;
    }

} // namespace

bool SemanticAnalyzer::Analyze(CompUnit& root) {
    symbol_table_.PushScope(); // global scope id = 1
    root.Accept(*this);
    symbol_table_.PopScope();
    return errors_.empty();
}

void SemanticAnalyzer::RecordError(int line, const std::string& code) {
    errors_.emplace_back(line, code);
}

bool SemanticAnalyzer::RegisterSymbol(const std::string& name, Symbol symbol, int line) {
    symbol.scope_id = symbol_table_.GetCurrentScopeId();
    Symbol copy_for_output = symbol;
    if (!symbol_table_.Register(name, std::move(symbol))) {
        RecordError(line, "b");
        return false;
    }
    ordered_symbols_.push_back({copy_for_output.scope_id, std::move(copy_for_output)});
    return true;
}

void SemanticAnalyzer::VisitBlockContents(Block& block) {
    for (auto& item : block.block_items) {
        item->Accept(*this);
    }
}

// -----------------------------------------------------------------------------
// CompUnit: visit decls, func_defs, main (all in global scope).
// -----------------------------------------------------------------------------
void SemanticAnalyzer::VisitCompUnit(CompUnit& node) {
    for (auto& d : node.decls)
        d->Accept(*this);
    for (auto& f : node.func_defs)
        f->Accept(*this);
    if (node.main_func_def)
        node.main_func_def->Accept(*this);
}

// -----------------------------------------------------------------------------
// Decl: just recurse to ConstDecl / VarDecl.
// -----------------------------------------------------------------------------
void SemanticAnalyzer::VisitConstDecl(ConstDecl& node) {
    BType prev = current_decl_btype_;
    current_decl_btype_ = node.btype;
    for (auto& def : node.const_defs)
        def->Accept(*this);
    current_decl_btype_ = prev;
}

void SemanticAnalyzer::VisitVarDecl(VarDecl& node) {
    BType prev = current_decl_btype_;
    current_decl_btype_ = node.btype;
    for (auto& def : node.var_defs)
        def->Accept(*this);
    current_decl_btype_ = prev;
}

// -----------------------------------------------------------------------------
// Block: push scope (RAII), then visit each block item.
// -----------------------------------------------------------------------------
void SemanticAnalyzer::VisitBlock(Block& node) {
    ScopeGuard guard(symbol_table_);
    for (auto& item : node.block_items) {
        item->Accept(*this);
    }
}

// -----------------------------------------------------------------------------
// FuncDef: register function in current scope; one scope for params+body; visit params then block
// contents.
// -----------------------------------------------------------------------------
void SemanticAnalyzer::VisitFuncDef(FuncDef& node) {
    const int line = node.GetLine();
    Symbol sym;
    sym.type = BTypeToFunc(node.func_type);
    sym.name = node.ident;
    sym.scope_id = symbol_table_.GetCurrentScopeId();
    for (const auto& p : node.func_f_params) {
        sym.param_types.push_back({p->btype, p->is_array});
    }
    if (!RegisterSymbol(node.ident, sym, line))
        return;

    ScopeGuard guard(
        symbol_table_); // one scope for params + body (requirement: 函数的参数属于函数内部的作用域)
    BType prev_func = current_func_type_;
    current_func_type_ = node.func_type;

    for (auto& p : node.func_f_params)
        p->Accept(*this);
    VisitBlockContents(*node.block);

    current_func_type_ = prev_func;
}

// -----------------------------------------------------------------------------
// MainFuncDef: do not register "main"; push scope and visit block contents.
// -----------------------------------------------------------------------------
void SemanticAnalyzer::VisitMainFuncDef(MainFuncDef& node) {
    ScopeGuard guard(symbol_table_);
    BType prev = current_func_type_;
    current_func_type_ = BType::INT; // main returns int
    VisitBlockContents(*node.block);
    current_func_type_ = prev;
}

// -----------------------------------------------------------------------------
// ConstDef / VarDef: register symbol. TODO constant folding for const_value / array_size.
// -----------------------------------------------------------------------------
void SemanticAnalyzer::VisitConstDef(ConstDef& node) {
    const int line = node.GetLine();
    Symbol sym;
    sym.name = node.ident;
    if (!node.array_size.has_value()) {
        sym.type = BTypeToConstScalar(current_decl_btype_);
    } else {
        sym.type = BTypeToConstArray(current_decl_btype_);
        // TODO: constant folding — evaluate node.array_size->ConstExp to int and set
        // sym.array_size.
    }
    // TODO: constant folding — evaluate ConstInitVal to set sym.const_value for scalar const.
    RegisterSymbol(node.ident, sym, line);
}

void SemanticAnalyzer::VisitVarDef(VarDef& node) {
    const int line = node.GetLine();
    Symbol sym;
    sym.name = node.ident;
    if (!node.array_size.has_value()) {
        sym.type = BTypeToVarScalar(current_decl_btype_);
    } else {
        sym.type = BTypeToVarArray(current_decl_btype_);
        // TODO: evaluate node.array_size (ConstExp) for sym.array_size (for IR / later use).
    }
    RegisterSymbol(node.ident, sym, line);
}

// -----------------------------------------------------------------------------
// FuncFParam: register in current scope (function scope).
// -----------------------------------------------------------------------------
void SemanticAnalyzer::VisitFuncFParam(FuncFParam& node) {
    const int line = node.GetLine();
    Symbol sym;
    sym.name = node.ident;
    sym.type = node.is_array ? BTypeToVarArray(node.btype) : BTypeToVarScalar(node.btype);
    RegisterSymbol(node.ident, sym, line);
}

// -----------------------------------------------------------------------------
// Statements: recurse and optional checks (break/continue, return type, printf).
// -----------------------------------------------------------------------------
void SemanticAnalyzer::VisitBlockStmt(BlockStmt& node) {
    if (node.block)
        node.block->Accept(*this);
}

void SemanticAnalyzer::VisitAssignStmt(AssignStmt& node) {
    lval_is_left_of_assign_ = true;
    if (node.lval)
        node.lval->Accept(*this);
    lval_is_left_of_assign_ = false;
    if (node.exp)
        node.exp->Accept(*this);
}

void SemanticAnalyzer::VisitExpStmt(ExpStmt& node) {
    if (node.exp.has_value() && *node.exp)
        (*node.exp)->Accept(*this);
}

void SemanticAnalyzer::VisitIfStmt(IfStmt& node) {
    if (node.cond)
        node.cond->Accept(*this);
    if (node.then_stmt)
        node.then_stmt->Accept(*this);
    if (node.else_stmt.has_value() && *node.else_stmt)
        (*node.else_stmt)->Accept(*this);
}

void SemanticAnalyzer::VisitForStmt(ForStmt& node) {
    if (node.init.has_value() && *node.init)
        (*node.init)->Accept(*this);
    if (node.cond.has_value() && *node.cond)
        (*node.cond)->Accept(*this);
    if (node.step.has_value() && *node.step)
        (*node.step)->Accept(*this);
    loop_depth_++;
    if (node.body)
        node.body->Accept(*this);
    loop_depth_--;
}

void SemanticAnalyzer::VisitBreakStmt(BreakStmt& node) {
    // TODO: if (loop_depth_ == 0) RecordError(node.GetLine(), "m");
}

void SemanticAnalyzer::VisitContinueStmt(ContinueStmt& node) {
    // TODO: if (loop_depth_ == 0) RecordError(node.GetLine(), "m");
}

void SemanticAnalyzer::VisitReturnStmt(ReturnStmt& node) {
    if (node.exp.has_value() && *node.exp) {
        // TODO: if (current_func_type_ == BType::VOID) RecordError(node.GetLine(), "f");
        (*node.exp)->Accept(*this);
    }
    // TODO: for non-void function, check that every path returns (e.g. last stmt or has return).
}

void SemanticAnalyzer::VisitGetintStmt(GetintStmt& node) {
    lval_is_left_of_assign_ = true;
    if (node.lval)
        node.lval->Accept(*this);
    lval_is_left_of_assign_ = false;
}

void SemanticAnalyzer::VisitGetcharStmt(GetcharStmt& node) {
    lval_is_left_of_assign_ = true;
    if (node.lval)
        node.lval->Accept(*this);
    lval_is_left_of_assign_ = false;
}

void SemanticAnalyzer::VisitPrintfStmt(PrintfStmt& node) {
    // TODO: check format string vs exp_list count (error l).
    for (auto& e : node.exp_list)
        e->Accept(*this);
}

void SemanticAnalyzer::VisitForInitOrStep(ForInitOrStep& node) {
    lval_is_left_of_assign_ = true;
    if (node.lval)
        node.lval->Accept(*this);
    lval_is_left_of_assign_ = false;
    if (node.exp)
        node.exp->Accept(*this);
}

// -----------------------------------------------------------------------------
// Expressions: LVal does lookup + const check; FuncCall does lookup + arg check.
// -----------------------------------------------------------------------------
void SemanticAnalyzer::VisitLVal(LVal& node) {
    const Symbol* s = symbol_table_.Lookup(node.ident);
    if (!s) {
        RecordError(node.GetLine(), "c");
        return;
    }
    if (lval_is_left_of_assign_ && IsConst(s->type)) {
        RecordError(node.GetLine(), "h"); // assign to constant
    }
    if (node.index)
        node.index->Accept(*this);
}

void SemanticAnalyzer::VisitNumber(Number& node) {
    (void)node;
}

void SemanticAnalyzer::VisitCharacter(Character& node) {
    (void)node;
}

void SemanticAnalyzer::VisitBinaryExp(BinaryExp& node) {
    if (node.lhs)
        node.lhs->Accept(*this);
    if (node.rhs)
        node.rhs->Accept(*this);
}

void SemanticAnalyzer::VisitUnaryExp(UnaryExp& node) {
    if (node.operand)
        node.operand->Accept(*this);
}

void SemanticAnalyzer::VisitFuncCall(FuncCall& node) {
    const Symbol* s = symbol_table_.Lookup(node.ident);
    if (!s) {
        RecordError(node.GetLine(), "c");
        return;
    }
    if (!IsFunc(s->type)) {
        RecordError(node.GetLine(), "c"); // not a function
        return;
    }
    // TODO: check argument count (d) and argument types (e). Compare node.func_r_params with
    // s->param_types.
    if (node.func_r_params) {
        for (auto& exp : node.func_r_params->func_r_params)
            exp->Accept(*this);
    }
}

void SemanticAnalyzer::VisitConstExp(ConstExp& node) {
    if (node.inner)
        node.inner->Accept(*this);
    // TODO: constant folding: compute int result and store for use in ConstDef dims / const_value.
}

void SemanticAnalyzer::VisitConstInitVal(ConstInitVal& node) {
    if (std::holds_alternative<ConstInitVal::SingleExp>(node.value)) {
        auto& p = std::get<ConstInitVal::SingleExp>(node.value);
        if (p)
            p->Accept(*this);
    } else if (std::holds_alternative<ConstInitVal::ExpList>(node.value)) {
        for (auto& e : std::get<ConstInitVal::ExpList>(node.value))
            e->Accept(*this);
    }
}

void SemanticAnalyzer::VisitInitVal(InitVal& node) {
    if (std::holds_alternative<InitVal::SingleExp>(node.value)) {
        auto& p = std::get<InitVal::SingleExp>(node.value);
        if (p)
            p->Accept(*this);
    } else if (std::holds_alternative<InitVal::ExpList>(node.value)) {
        for (auto& e : std::get<InitVal::ExpList>(node.value))
            e->Accept(*this);
    }
}

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
        if (b == BType::VOID) {
            return SymbolType::VOID_FUNC;
        }
        if (b == BType::INT) {
            return SymbolType::INT_FUNC;
        }
        return SymbolType::CHAR_FUNC;
    }

} // namespace

bool SemanticAnalyzer::Analyze(CompUnit& root) {
    ScopeGuard guard(symbol_table_); // global scope; pop on return/exception
    root.Accept(*this);
    return error_log_.empty();
}

void SemanticAnalyzer::RecordError(int line, std::string_view code) {
    error_log_.emplace_back(line <= 0 ? 1 : line, std::string(code));
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

// Visit block items only, no scope push. Used by FuncDef/MainFuncDef so the function body
// shares the same scope as params (one scope per function); calling block->Accept would push
// an extra scope.
void SemanticAnalyzer::VisitBlockContents(Block& block) {
    for (auto& item : block.block_items) {
        item->Accept(*this);
    }

    // Error "g": non-void function must have return. Line number is the block's line
    // (last `}` of the function), per spec; Parser must set Block::line_ accordingly.
    if (current_func_type_ != BType::VOID) {
        bool has_return_at_end = false;
        if (!block.block_items.empty()) {
            BlockItem* last = block.block_items.back().get();
            has_return_at_end = (dynamic_cast<ReturnStmt*>(last) != nullptr);
        }
        if (!has_return_at_end) {
            RecordError(block.GetLine(), "g");
        }
    }
}

// -----------------------------------------------------------------------------
// CompUnit: visit decls, func_defs, main (all in global scope).
// -----------------------------------------------------------------------------
void SemanticAnalyzer::VisitCompUnit(CompUnit& comp_unit) {
    for (auto& d : comp_unit.decls) {
        d->Accept(*this);
    }
    for (auto& f : comp_unit.func_defs) {
        f->Accept(*this);
    }
    if (comp_unit.main_func_def) {
        comp_unit.main_func_def->Accept(*this);
    }
}

// -----------------------------------------------------------------------------
// Decl: just recurse to ConstDecl / VarDecl.
// -----------------------------------------------------------------------------
void SemanticAnalyzer::VisitConstDecl(ConstDecl& const_decl) {
    BType prev = current_decl_btype_;
    current_decl_btype_ = const_decl.btype;
    for (auto& def : const_decl.const_defs) {
        def->Accept(*this);
    }
    current_decl_btype_ = prev;
}

void SemanticAnalyzer::VisitVarDecl(VarDecl& var_decl) {
    BType prev = current_decl_btype_;
    current_decl_btype_ = var_decl.btype;
    for (auto& def : var_decl.var_defs) {
        def->Accept(*this);
    }
    current_decl_btype_ = prev;
}

// -----------------------------------------------------------------------------
// Block: push scope (RAII), then visit each block item.
// -----------------------------------------------------------------------------
void SemanticAnalyzer::VisitBlock(Block& block) {
    ScopeGuard guard(symbol_table_);
    for (auto& item : block.block_items) {
        item->Accept(*this);
    }
}

// -----------------------------------------------------------------------------
// FuncDef: register function in current scope; one scope for params+body; visit params then
// block contents.
// -----------------------------------------------------------------------------
void SemanticAnalyzer::VisitFuncDef(FuncDef& func_def) {
    const int line = func_def.GetLine();
    Symbol sym;
    sym.type = BTypeToFunc(func_def.func_type);
    sym.name = func_def.ident;
    sym.scope_id = symbol_table_.GetCurrentScopeId();
    for (const auto& p : func_def.func_f_params) {
        sym.param_types.push_back({p->btype, p->is_array});
    }
    RegisterSymbol(func_def.ident, sym, line);

    // Requirement: redefined functions should also be fully analyzed to collect other errors,
    // so we don't return here

    ScopeGuard guard(symbol_table_);
    BType prev_func = current_func_type_;
    current_func_type_ = func_def.func_type;

    for (auto& p : func_def.func_f_params) {
        p->Accept(*this);
    }
    VisitBlockContents(*func_def.block);

    current_func_type_ = prev_func;
}

// -----------------------------------------------------------------------------
// MainFuncDef: do not register "main"; push scope and visit block contents.
// -----------------------------------------------------------------------------
void SemanticAnalyzer::VisitMainFuncDef(MainFuncDef& main_func_def) {
    ScopeGuard guard(symbol_table_);
    BType prev = current_func_type_;
    current_func_type_ = BType::INT; // main returns int
    VisitBlockContents(*main_func_def.block);
    current_func_type_ = prev;
}

// -----------------------------------------------------------------------------
// ConstDef / VarDef: register symbol.
// -----------------------------------------------------------------------------
void SemanticAnalyzer::VisitConstDef(ConstDef& const_def) {
    const int line = const_def.GetLine();
    Symbol sym;
    sym.name = const_def.ident;
    if (!const_def.array_size.has_value()) {
        sym.type = BTypeToConstScalar(current_decl_btype_);
    } else {
        sym.type = BTypeToConstArray(current_decl_btype_);
        const auto& p = *const_def.array_size;
        p->Accept(*this);
        sym.array_size = last_value_;
    }

    if (const_def.const_init_val) {
        const_def.const_init_val->Accept(*this);
        sym.const_values = last_values_;
    }

    RegisterSymbol(const_def.ident, sym, line);
}

void SemanticAnalyzer::VisitVarDef(VarDef& var_def) {
    const int line = var_def.GetLine();
    Symbol sym;
    sym.name = var_def.ident;
    if (!var_def.array_size.has_value()) {
        sym.type = BTypeToVarScalar(current_decl_btype_);
    } else {
        sym.type = BTypeToVarArray(current_decl_btype_);
        if (var_def.array_size.has_value()) {
            const auto& p = *var_def.array_size;
            p->Accept(*this);
            sym.array_size = last_value_;
        }
    }

    if (var_def.init_val) {
        var_def.init_val->Accept(*this);
    }

    RegisterSymbol(var_def.ident, sym, line);
}

// -----------------------------------------------------------------------------
// FuncFParam: register in current scope (function scope).
// -----------------------------------------------------------------------------
void SemanticAnalyzer::VisitFuncFParam(FuncFParam& func_f_param) {
    const int line = func_f_param.GetLine();
    Symbol sym;
    sym.name = func_f_param.ident;
    sym.type = func_f_param.is_array ? BTypeToVarArray(func_f_param.btype) :
                                       BTypeToVarScalar(func_f_param.btype);
    RegisterSymbol(func_f_param.ident, sym, line);
}

// -----------------------------------------------------------------------------
// Statements: recurse and optional checks (break/continue, return type, printf).
// -----------------------------------------------------------------------------
void SemanticAnalyzer::VisitBlockStmt(BlockStmt& block_stmt) {
    if (block_stmt.block) {
        block_stmt.block->Accept(*this);
    }
}

void SemanticAnalyzer::VisitAssignStmt(AssignStmt& assign_stmt) {
    lval_is_left_of_assign_ = true;
    if (assign_stmt.lval) {
        assign_stmt.lval->Accept(*this);
    }
    lval_is_left_of_assign_ = false;
    if (assign_stmt.exp) {
        assign_stmt.exp->Accept(*this);
    }
}

void SemanticAnalyzer::VisitExpStmt(ExpStmt& exp_stmt) {
    if (exp_stmt.exp.has_value() && *exp_stmt.exp) {
        (*exp_stmt.exp)->Accept(*this);
    }
}

void SemanticAnalyzer::VisitIfStmt(IfStmt& if_stmt) {
    if (if_stmt.cond) {
        if_stmt.cond->Accept(*this);
    }
    if (if_stmt.then_stmt) {
        if_stmt.then_stmt->Accept(*this);
    }
    if (if_stmt.else_stmt.has_value() && *if_stmt.else_stmt) {
        (*if_stmt.else_stmt)->Accept(*this);
    }
}

void SemanticAnalyzer::VisitForStmt(ForStmt& for_stmt) {
    if (for_stmt.init.has_value() && *for_stmt.init) {
        (*for_stmt.init)->Accept(*this);
    }
    if (for_stmt.cond.has_value() && *for_stmt.cond) {
        (*for_stmt.cond)->Accept(*this);
    }
    if (for_stmt.step.has_value() && *for_stmt.step) {
        (*for_stmt.step)->Accept(*this);
    }
    loop_depth_++;
    if (for_stmt.body) {
        for_stmt.body->Accept(*this);
    }
    loop_depth_--;
}

void SemanticAnalyzer::VisitBreakStmt(BreakStmt& break_stmt) {
    if (loop_depth_ == 0) {
        RecordError(break_stmt.GetLine(), "m");
    }
}

void SemanticAnalyzer::VisitContinueStmt(ContinueStmt& continue_stmt) {
    if (loop_depth_ == 0) {
        RecordError(continue_stmt.GetLine(), "m");
    }
}

void SemanticAnalyzer::VisitReturnStmt(ReturnStmt& return_stmt) {
    if (return_stmt.exp.has_value() && *return_stmt.exp) {
        if (current_func_type_ == BType::VOID) {
            RecordError(return_stmt.GetLine(), "f");
        }
        (*return_stmt.exp)->Accept(*this);
    }
}

void SemanticAnalyzer::VisitGetintStmt(GetintStmt& getint_stmt) {
    lval_is_left_of_assign_ = true;
    if (getint_stmt.lval) {
        getint_stmt.lval->Accept(*this);
    }
    lval_is_left_of_assign_ = false;
}

void SemanticAnalyzer::VisitGetcharStmt(GetcharStmt& getchar_stmt) {
    lval_is_left_of_assign_ = true;
    if (getchar_stmt.lval) {
        getchar_stmt.lval->Accept(*this);
    }
    lval_is_left_of_assign_ = false;
}

void SemanticAnalyzer::VisitPrintfStmt(PrintfStmt& printf_stmt) {
    for (auto& e : printf_stmt.exp_list) {
        e->Accept(*this);
    }

    size_t format_count = 0;
    const std::string& fmt = printf_stmt.format_string;
    for (size_t i = 0; i + 1 < fmt.size(); ++i) {
        if (fmt[i] == '%' && (fmt[i + 1] == '%')) {
            i++;
        } else if (fmt[i] == '%' && (fmt[i + 1] == 'c' || fmt[i + 1] == 'd')) {
            format_count++;
        }
    }

    if (printf_stmt.exp_list.size() != format_count) {
        RecordError(printf_stmt.GetLine(), "l");
    }
}

void SemanticAnalyzer::VisitForInitOrStep(ForInitOrStep& for_init_or_step) {
    lval_is_left_of_assign_ = true;
    if (for_init_or_step.lval) {
        for_init_or_step.lval->Accept(*this);
    }
    lval_is_left_of_assign_ = false;
    if (for_init_or_step.exp) {
        for_init_or_step.exp->Accept(*this);
    }
}

// -----------------------------------------------------------------------------
// Expressions: LVal does lookup + const check; FuncCall does lookup + arg check.
// -----------------------------------------------------------------------------
void SemanticAnalyzer::VisitLVal(LVal& lval) {
    const Symbol* sym = symbol_table_.Lookup(lval.ident);
    if (!sym) {
        RecordError(lval.GetLine(), "c");
        return;
    }
    if (lval_is_left_of_assign_ && IsConst(sym->type)) {
        RecordError(lval.GetLine(), "h");
    }
    if (lval.index) {
        lval.index->Accept(*this);
    }

    // The type of the expression when used as an argument: no index and symbol is array type;
    // otherwise scalar
    if (!lval_is_left_of_assign_) {
        current_exp_is_array_ = IsArray(sym->type) && !lval.index;
    }

    // Constant folding: when used as value, const symbol yields last_value_ for ConstExp eval.
    if (IsConst(sym->type) && !lval_is_left_of_assign_ && !sym->const_values.empty()) {
        if (sym->array_size.has_value()) {
            size_t idx = static_cast<size_t>(last_value_);
            if (idx < sym->const_values.size()) {
                last_value_ = sym->const_values[idx];
            }
        } else {
            last_value_ = sym->const_values[0];
        }
    }
}

void SemanticAnalyzer::VisitNumber(Number& number) {
    last_value_ = number.int_const;
    current_exp_is_array_ = false;
}

void SemanticAnalyzer::VisitCharacter(Character& character) {
    last_value_ = static_cast<int>(character.char_const); // NOLINT(bugprone-signed-char-misuse)
    current_exp_is_array_ = false;
}

// Constant folding: evaluate at visit time so ConstExp/array size get last_value_.
void SemanticAnalyzer::VisitBinaryExp(BinaryExp& binary_exp) {
    if (binary_exp.lhs) {
        binary_exp.lhs->Accept(*this);
    }

    int left = last_value_;

    if (binary_exp.rhs) {
        binary_exp.rhs->Accept(*this);
    }

    int right = last_value_;
    current_exp_is_array_ = false; // the result of the operation is a scalar
    last_value_ = binary_exp.op == OpType::PLUS ? left + right :
                  binary_exp.op == OpType::MINU ? left - right :
                  binary_exp.op == OpType::MUL  ? left * right :
                  binary_exp.op == OpType::DIV  ? left / right :
                  binary_exp.op == OpType::MOD  ? left % right :
                  binary_exp.op == OpType::LT   ? left < right :
                  binary_exp.op == OpType::GT   ? left > right :
                  binary_exp.op == OpType::LE   ? left <= right :
                  binary_exp.op == OpType::GE   ? left >= right :
                  binary_exp.op == OpType::EQ   ? left == right :
                  binary_exp.op == OpType::NE   ? left != right :
                  binary_exp.op == OpType::AND  ? left && right :
                  binary_exp.op == OpType::OR   ? left || right :
                                                  0;
}

void SemanticAnalyzer::VisitUnaryExp(UnaryExp& unary_exp) {
    if (unary_exp.operand) {
        unary_exp.operand->Accept(*this);
    }
    current_exp_is_array_ = false;
    last_value_ = unary_exp.op == OpType::NOT  ? !last_value_ :
                  unary_exp.op == OpType::MINU ? -last_value_ :
                                                 last_value_;
}

void SemanticAnalyzer::VisitFuncCall(FuncCall& func_call) {
    const Symbol* sym = symbol_table_.Lookup(func_call.ident);
    if (!sym) {
        RecordError(func_call.GetLine(), "c");
        return;
    }

    if (!IsFunc(sym->type)) {
        RecordError(func_call.GetLine(), "c");
        return;
    }

    std::vector<bool> actual_is_array;
    if (func_call.func_r_params) {
        actual_is_array.reserve(func_call.func_r_params->exp_list.size());
        for (auto& exp : func_call.func_r_params->exp_list) {
            current_exp_is_array_ = false;
            exp->Accept(*this);
            actual_is_array.push_back(current_exp_is_array_);
        }
    }

    size_t param_count = actual_is_array.size();
    if (param_count != sym->param_types.size()) {
        RecordError(func_call.GetLine(), "d");
    }

    // Compare formal (param_types) vs actual (actual_is_array): scalar/array must match (error
    // "e").
    for (size_t i = 0; i < param_count && i < sym->param_types.size(); ++i) {
        if (actual_is_array[i] != sym->param_types[i].second) {
            RecordError(func_call.GetLine(), "e");
            break;
        }
    }
    current_exp_is_array_ = false; // the result of the function call is a scalar
}

void SemanticAnalyzer::VisitFuncRParams(FuncRParams& func_r_params) {
    for (auto& exp : func_r_params.exp_list) {
        exp->Accept(*this);
    }
}

void SemanticAnalyzer::VisitConstExp(ConstExp& const_exp) {
    if (const_exp.inner) {
        const_exp.inner->Accept(*this);
    }
}

void SemanticAnalyzer::VisitConstInitVal(ConstInitVal& const_init_val) {
    last_values_.clear();
    if (std::holds_alternative<ConstInitVal::SingleExp>(const_init_val.value)) {
        auto& p = std::get<ConstInitVal::SingleExp>(const_init_val.value);
        if (p) {
            p->Accept(*this);
            last_values_.push_back(last_value_);
        }
    } else if (std::holds_alternative<ConstInitVal::ExpList>(const_init_val.value)) {
        for (auto& e : std::get<ConstInitVal::ExpList>(const_init_val.value)) {
            e->Accept(*this);
            last_values_.push_back(last_value_);
        }
    }
}

void SemanticAnalyzer::VisitInitVal(InitVal& init_val) {
    if (std::holds_alternative<InitVal::SingleExp>(init_val.value)) {
        auto& p = std::get<InitVal::SingleExp>(init_val.value);
        if (p) {
            p->Accept(*this);
        }
    } else if (std::holds_alternative<InitVal::ExpList>(init_val.value)) {
        for (auto& e : std::get<InitVal::ExpList>(init_val.value)) {
            e->Accept(*this);
        }
    }
}

/**
 * @file IRGenVisitor.cpp
 * @brief Implementation of IRGenVisitor: AST traversal and IR construction.
 *
 * Visit* bodies are left as stubs with brief guidance; implement according to
 * the grammar and docs/ai_collab_notes/llvm_ir_instructions_summary.md.
 */
#include "IRGenVisitor.h"
#include "AST.h"
#include "IRScopeGuard.h"
#include "ir/Constant.h"
#include "ir/Instruction.h"
#include <optional>
namespace {

    /** Map comparison OpType to LLVM icmp predicate (for VisitBinaryExp). */
    std::optional<ir::IcmpPred> OpTypeToIcmpPred(OpType op) {
        switch (op) {
            case OpType::LT: return ir::IcmpPred::SLT;
            case OpType::GT: return ir::IcmpPred::SGT;
            case OpType::LE: return ir::IcmpPred::SLE;
            case OpType::GE: return ir::IcmpPred::SGE;
            case OpType::EQ: return ir::IcmpPred::EQ;
            case OpType::NE: return ir::IcmpPred::NE;
            default: return std::nullopt;
        }
    }

    /** True if OpType is arithmetic (add/sub/mul/div/mod). */
    bool IsArithmeticOp(OpType op) {
        return op == OpType::ADD || op == OpType::SUB || op == OpType::MUL || op == OpType::DIV ||
               op == OpType::MOD;
    }

} // namespace

// -----------------------------------------------------------------------------
// Constructor and module access
// -----------------------------------------------------------------------------

IRGenVisitor::IRGenVisitor() {
    module_ = std::make_unique<ir::Module>();
    builder_ = std::make_unique<ir::IRBuilder>();
}

std::unique_ptr<ir::Module> IRGenVisitor::Translate(CompUnit& comp_unit) {
    IRScopeGuard guard(*this);
    comp_unit.Accept(*this);
    return std::move(module_);
}

// -----------------------------------------------------------------------------
// Scope helpers (symbol table)
// -----------------------------------------------------------------------------

ir::Value* IRGenVisitor::LookupVariable(const std::string& name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto i = it->map.find(name);
        if (i != it->map.end()) {
            return i->second;
        }
    }
    return nullptr;
}

void IRGenVisitor::RegisterVariable(const std::string& name, ir::Value* value) {
    scopes_.back().map[name] = value;
}

void IRGenVisitor::PushScope() {
    current_scope_id_ = next_scope_id_++;
    scopes_.push_back(Scope{current_scope_id_, {}});
}

void IRGenVisitor::PopScope() {
    if (scopes_.empty()) {
        return;
    }
    scopes_.pop_back();
    current_scope_id_ = scopes_.empty() ? 0 : scopes_.back().id;
}

// -----------------------------------------------------------------------------
// CompUnit and declarations (global vs local)
// -----------------------------------------------------------------------------

void IRGenVisitor::VisitCompUnit(CompUnit& comp_unit) {
    is_global_ = true;
    for (auto& d : comp_unit.decls) {
        d->Accept(*this);
    }
    for (auto& f : comp_unit.func_defs) {
        f->Accept(*this);
    }
    if (comp_unit.main_func_def) {
        comp_unit.main_func_def->Accept(*this);
    }
    is_global_ = false;
}

void IRGenVisitor::VisitConstDecl(ConstDecl& const_decl) {
    // Implement (exercise): For each ConstDef, create global constant or local alloca + store
    // depending on is_global_. Use ConstInitVal for initializer.
}

void IRGenVisitor::VisitVarDecl(VarDecl& var_decl) {
    // Implement (exercise): For each VarDef, create global variable or local alloca (and store if
    // InitVal present). Respect is_global_.
}

void IRGenVisitor::VisitConstDef(ConstDef& const_def) {
    // Implement (exercise): Create constant/alloc, bind name in current scope. Evaluate
    // ConstInitVal.
}

void IRGenVisitor::VisitVarDef(VarDef& var_def) {
    // Implement (exercise): Create global or alloca, optional init from InitVal, bind name in
    // scope.
    is_lval_mode_ = true;
}

void IRGenVisitor::VisitConstInitVal(ConstInitVal& const_init_val) {
    // Implement (exercise): Evaluate constant initializer (ConstExp or list); result used by
    // ConstDef.
}

void IRGenVisitor::VisitInitVal(InitVal& init_val) {
    // Implement (exercise): Evaluate initializer (Exp or list); result stored by VarDef.
}

// -----------------------------------------------------------------------------
// Functions
// -----------------------------------------------------------------------------

void IRGenVisitor::VisitFuncDef(FuncDef& func_def) {
    // Implement (exercise): Create Function, add entry BasicBlock, set current_function_ and
    // builder_ insert point. Alloca for each FuncFParam and store args. Visit Block.
}

void IRGenVisitor::VisitMainFuncDef(MainFuncDef& main_func_def) {
    // Implement (exercise): Same as FuncDef for main: define i32 @main(), then visit body.
}

void IRGenVisitor::VisitFuncFParam(FuncFParam& func_f_param) {
    // Implement (exercise): Create alloca for parameter (or register), bind name in scope.
}

// -----------------------------------------------------------------------------
// Block and statements
// -----------------------------------------------------------------------------

void IRGenVisitor::VisitBlock(Block& block) {
    // Implement (exercise): PushScope(). For each BlockItem, Accept(). PopScope().
}

void IRGenVisitor::VisitBlockStmt(BlockStmt& block_stmt) {
    // Implement (exercise): Visit the inner Block (it will push/pop scope).
}

void IRGenVisitor::VisitAssignStmt(AssignStmt& assign_stmt) {
    is_lval_mode_ = true;
    assign_stmt.lval->Accept(*this);
    ir::Value* addr = temp_value_;
    is_lval_mode_ = false;
    assign_stmt.exp->Accept(*this);
    ir::Value* val = temp_value_;
    if (addr && val && builder_->GetInsertBlock()) {
        builder_->CreateStore(val, addr);
    }
}

void IRGenVisitor::VisitExpStmt(ExpStmt& exp_stmt) {
    if (exp_stmt.exp.has_value() && *exp_stmt.exp) {
        (*exp_stmt.exp)->Accept(*this);
    }
}

void IRGenVisitor::VisitIfStmt(IfStmt& if_stmt) {
    // Implement (exercise): Visit Cond (temp_value_ = i1). Create cond_br, then/else/merge blocks,
    // set insert point and visit Stmt(s).
}

void IRGenVisitor::VisitForStmt(ForStmt& for_stmt) {
    // Implement (exercise): Create cond/body/step/end blocks. Push break_targets_.push_back(end),
    // continue_targets_.push_back(step). Visit init, cond, body, step; wire br.
    // Pop break/continue targets.
}

void IRGenVisitor::VisitForInitOrStep(ForInitOrStep& for_init_or_step) {
    is_lval_mode_ = true;
    if (for_init_or_step.lval) {
        for_init_or_step.lval->Accept(*this);
    }
    is_lval_mode_ = false;
    ir::Value* addr = temp_value_;
    if (for_init_or_step.exp) {
        for_init_or_step.exp->Accept(*this);
    }
    ir::Value* val = temp_value_;
    if (addr && val && builder_->GetInsertBlock()) {
        builder_->CreateStore(val, addr);
    }
}

void IRGenVisitor::VisitBreakStmt(BreakStmt& break_stmt) {
    // Implement (exercise): Create br to break_targets_.back() (must be inside a loop).
}

void IRGenVisitor::VisitContinueStmt(ContinueStmt& continue_stmt) {
    // Implement (exercise): Create br to continue_targets_.back() (must be inside a loop).
}

void IRGenVisitor::VisitReturnStmt(ReturnStmt& return_stmt) {
    // Implement (exercise): If return value present, visit Exp then CreateRet(temp_value_);
    // else CreateRetVoid(). Then set insert point to unreachable or omit further code.
}

void IRGenVisitor::VisitGetintStmt(GetintStmt& getint_stmt) {
    // Implement (exercise): Create call @getint(), then store result to LVal (is_lval_mode_ = true,
    // visit LVal).
    is_lval_mode_ = true;
    getint_stmt.lval->Accept(*this);
    is_lval_mode_ = false;
    ir::Value* addr = temp_value_;
    ir::Instruction* call =
        builder_->CreateCall(module_->GetI32Type(), module_->GetFunction("getint"), {});
    if (addr && call && builder_->GetInsertBlock()) {
        builder_->CreateStore(call, addr);
    }
}

void IRGenVisitor::VisitGetcharStmt(GetcharStmt& getchar_stmt) {
    // Implement (exercise): Create call @getchar(), then store result to LVal.
    is_lval_mode_ = true;
    getchar_stmt.lval->Accept(*this);
    is_lval_mode_ = false;
    ir::Value* addr = temp_value_;
    ir::Instruction* call =
        builder_->CreateCall(module_->GetI32Type(), module_->GetFunction("getchar"), {});
    if (addr && call && builder_->GetInsertBlock()) {
        builder_->CreateStore(call, addr);
    }
}

void IRGenVisitor::VisitPrintfStmt(PrintfStmt& printf_stmt) {
    // Implement (exercise): Parse format string, emit putint/putch/putstr calls for each format
    // specifier and Exp.
}

// -----------------------------------------------------------------------------
// Expressions (result in temp_value_)
// -----------------------------------------------------------------------------

void IRGenVisitor::VisitLVal(LVal& lval) {
    // LVal is either:  ident   or   ident [ Exp ]
    // We need: (1) base address from symbol table; (2) if index present, GEP to element;
    //          (3) if is_lval_mode_ → leave address in temp_value_; else → load and put value.
    ir::Value* value = LookupVariable(lval.ident);
    if (!value) {
        return;
    }

    // --- No index: scalar (or whole array when used as pointer) ---
    if (!lval.index.has_value() || !*lval.index) {
        if (is_lval_mode_) {
            temp_value_ = value; // caller will store to this address
        } else {
            ir::Instruction* load = builder_->CreateLoad(value);
            temp_value_ = load ? load : value;
        }
        return;
    }

    // --- Has index: array element ---
    (*lval.index)->Accept(*this);
    ir::Value* index = temp_value_;
    if (!index || !builder_->GetInsertBlock()) {
        return;
    }
    auto* ptr_ty = dynamic_cast<ir::PointerType*>(value->GetType());
    if (!ptr_ty) {
        temp_value_ = value;
        return;
    }
    ir::Type* elem_ty = ptr_ty->GetPointeeType();
    ir::Instruction* gep = builder_->CreateGEP(module_->GetPointerType(elem_ty), value, index);
    if (!gep) {
        temp_value_ = value;
        return;
    }
    if (is_lval_mode_) {
        temp_value_ = gep; // caller will store to this element address
    } else {
        ir::Instruction* load = builder_->CreateLoad(gep);
        temp_value_ = load ? load : gep;
    }
}

void IRGenVisitor::VisitNumber(Number& number) {
    temp_value_ = module_->GetInt32Constant(number.int_const);
}

void IRGenVisitor::VisitCharacter(Character& character) {
    temp_value_ = module_->GetInt8Constant(character.char_const);
}

void IRGenVisitor::VisitBinaryExp(BinaryExp& binary_exp) {
    binary_exp.lhs->Accept(*this);
    ir::Value* lhs = temp_value_;
    binary_exp.rhs->Accept(*this);
    ir::Value* rhs = temp_value_;
    if (!lhs || !rhs || !builder_->GetInsertBlock()) {
        return;
    }
    OpType op = binary_exp.op;
    if (IsArithmeticOp(op)) {
        ir::Instruction* inst = builder_->CreateBinary(op, lhs, rhs);
        temp_value_ = inst ? inst : temp_value_;
        return;
    }
    std::optional<ir::IcmpPred> pred = OpTypeToIcmpPred(op);
    if (pred) {
        ir::Instruction* cmp = builder_->CreateIcmp(module_->GetI1Type(), *pred, lhs, rhs);
        if (cmp) {
            ir::Instruction* zext = builder_->CreateZext(cmp, module_->GetI32Type());
            temp_value_ = zext ? zext : cmp;
        }
        return;
    }
    // AND / OR (short-circuit) require control flow; extend when implementing Cond.
}

void IRGenVisitor::VisitUnaryExp(UnaryExp& unary_exp) {
    // Implement (exercise): Handle PrimaryExp (delegate to child), UnaryOp '-' (sub 0, x), '!'
    // (icmp eq x, 0).
    unary_exp.operand->Accept(*this);
    ir::Value* operand = temp_value_;
    if (!operand || !builder_->GetInsertBlock()) {
        return;
    }

    OpType op = unary_exp.op;
    ir::ConstantInt* zero = module_->GetInt32Constant(0);

    if (op == OpType::MINU) {
        ir::Instruction* inst = builder_->CreateBinary(OpType::SUB, zero, temp_value_);
        temp_value_ = inst ? inst : temp_value_;
        return;
    }
    if (op == OpType::NOT) {
        ir::Instruction* cmp =
            builder_->CreateIcmp(module_->GetI1Type(), ir::IcmpPred::EQ, operand, zero);
        if (cmp) {
            ir::Instruction* zext = builder_->CreateZext(cmp, module_->GetI32Type());
            temp_value_ = zext ? zext : cmp;
        }
        return;
    }
}

void IRGenVisitor::VisitFuncCall(FuncCall& func_call) {
    // Implement (exercise): Look up function, visit FuncRParams for args, CreateCall; set
    // temp_value_ if non-void.
}

void IRGenVisitor::VisitFuncRParams(FuncRParams& func_r_params) {
    // Implement (exercise): Visit each Exp, collect Value* args for the current call.
}

void IRGenVisitor::VisitConstExp(ConstExp& const_exp) {
    // Implement (exercise): Evaluate as constant expression (e.g. for array size or constant init).
    if (const_exp.const_value.has_value()) {
        temp_value_ = module_->GetInt32Constant(const_exp.const_value.value());
    }
}

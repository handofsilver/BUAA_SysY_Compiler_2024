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

    /**
     * Map BType to IR type for function return (FuncType: void | int | char).
     * Caller must have Module* to get i32/i8; void is from GetVoidType().
     */
    ir::Type* BTypeToReturnType(BType btype, ir::Module* module) {
        switch (btype) {
            case BType::VOID: return ir::GetVoidType();
            case BType::INT: return module->GetI32Type();
            case BType::CHAR: return module->GetI8Type();
            default: return module->GetI32Type();
        }
    }

    /**
     * Map FuncFParam (BType + is_array) to IR parameter type.
     * Grammar: FuncFParam → BType Ident ['[' ']']; BType → 'int' | 'char'.
     * Scalar: i32 or i8; array: i32* or i8* (pointer to first element).
     */
    ir::Type* BTypeToParamType(BType btype, bool is_array, ir::Module* module) {
        ir::Type* elem = (btype == BType::CHAR) ? static_cast<ir::Type*>(module->GetI8Type()) :
                                                  static_cast<ir::Type*>(module->GetI32Type());
        return is_array ? static_cast<ir::Type*>(module->GetPointerType(elem)) : elem;
    }

    /** Allocated type for a scalar parameter (for alloca): i32 or i8. */
    ir::Type* BTypeToAllocaType(BType btype, ir::Module* module) {
        return (btype == BType::CHAR) ? static_cast<ir::Type*>(module->GetI8Type()) :
                                        static_cast<ir::Type*>(module->GetI32Type());
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
// Control-flow helpers
// -----------------------------------------------------------------------------

ir::BasicBlock* IRGenVisitor::CreateBasicBlock(const std::string& name) {
    if (!current_function_) {
        return nullptr;
    }
    auto block = std::make_unique<ir::BasicBlock>(name);
    ir::BasicBlock* ptr = block.get();
    current_function_->AddBlock(std::move(block));
    return ptr;
}

bool IRGenVisitor::IsBlockTerminated() const {
    ir::BasicBlock* block = builder_->GetInsertBlock();
    if (!block) {
        return true;
    }
    const auto& insts = block->GetInstructions();
    if (insts.empty()) {
        return false;
    }
    ir::Instruction* last = insts.back().get();
    return (dynamic_cast<ir::BranchInst*>(last) != nullptr ||
            dynamic_cast<ir::ReturnInst*>(last) != nullptr);
}

ir::Instruction* IRGenVisitor::CreateEntryBlockAlloca(ir::Type* type, const std::string& name) {
    if (!current_function_ || !type) {
        return nullptr;
    }
    const auto& blocks = current_function_->GetBlocks();
    if (blocks.empty()) {
        return nullptr;
    }
    ir::BasicBlock* entry = blocks.front().get();
    ir::Type* ptr_type =
        module_->GetPointerType(type); // alloca result is pointer to allocated type
    auto inst = std::make_unique<ir::AllocaInst>(name, ptr_type, entry);
    ir::Instruction* result = inst.get();
    entry->AddInstruction(std::move(inst)); // append to entry; no insert-point change (allocas stay
                                            // at front while we only process params)
    return result;
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
    // VarDef: global -> get/create global and bind; local -> entry alloca and bind (init handled by
    // InitVal visit)
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
    // FuncDef -> FuncType Ident '(' [FuncFParams] ')' Block; FuncType -> void|int|char; FuncFParam
    // -> BType Ident ['[' ']']
    ir::Type* return_type = BTypeToReturnType(func_def.func_type, module_.get());
    std::vector<ir::Type*> param_types;
    for (const auto& p : func_def.func_f_params) {
        param_types.push_back(BTypeToParamType(p->btype, p->is_array, module_.get()));
    }
    ir::Function* func = module_->CreateFunction(func_def.ident, return_type, param_types);
    for (size_t i = 0; i < func_def.func_f_params.size(); ++i) {
        ir::Argument* arg = func->GetArgument(i);
        if (arg) {
            arg->SetName(func_def.func_f_params[i]->ident);
        }
    }
    current_function_ = func;
    IRScopeGuard scope_guard(*this); // RAII: PopScope on exit

    ir::BasicBlock* entry = CreateBasicBlock("entry");
    builder_->SetInsertPoint(entry);

    for (size_t i = 0; i < func_def.func_f_params.size(); ++i) {
        const auto& p = func_def.func_f_params[i];
        ir::Value* arg_val = func->GetArgument(i);
        if (!arg_val) {
            continue;
        }
        if (p->is_array) {
            // array param is already a pointer (i32* / i8*); bind as-is
            RegisterVariable(p->ident, arg_val);
        } else {
            // scalar: alloca a slot in entry, store incoming value, bind alloca address
            ir::Type* alloc_ty = BTypeToAllocaType(p->btype, module_.get());
            ir::Instruction* alloca_inst = CreateEntryBlockAlloca(alloc_ty, p->ident);
            if (alloca_inst && builder_->GetInsertBlock()) {
                builder_->CreateStore(arg_val, alloca_inst);
                RegisterVariable(p->ident, alloca_inst);
            }
        }
    }

    func_def.block->Accept(*this);

    if (func_def.func_type == BType::VOID && !IsBlockTerminated()) {
        builder_->CreateRetVoid();
    }
}

void IRGenVisitor::VisitMainFuncDef(MainFuncDef& main_func_def) {
    ir::Function* func = module_->CreateFunction("main", module_->GetI32Type(), {});
    current_function_ = func;
    IRScopeGuard scope_guard(*this);

    ir::BasicBlock* entry = CreateBasicBlock("entry");
    builder_->SetInsertPoint(entry);

    main_func_def.block->Accept(*this);

    if (!IsBlockTerminated()) {
        builder_->CreateRet(module_->GetInt32Constant(0)); // fallback for int main()
    }
}

void IRGenVisitor::VisitFuncFParam(FuncFParam& func_f_param) {
    // Implement (exercise): Create alloca for parameter (or register), bind name in scope.
}

// -----------------------------------------------------------------------------
// Block and statements
// -----------------------------------------------------------------------------

void IRGenVisitor::VisitBlock(Block& block) {
    IRScopeGuard scope_guard(*this);
    for (auto& item : block.block_items) {
        item->Accept(*this);
    }
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
    ir::BasicBlock* true_block = CreateBasicBlock("if.then");
    ir::BasicBlock* next_block = CreateBasicBlock("if.next");
    ir::BasicBlock* false_block = (if_stmt.else_stmt.has_value() && if_stmt.else_stmt->get()) ?
                                      CreateBasicBlock("if.else") :
                                      next_block;

    if_stmt.cond->Accept(*this);
    ir::Value* cond_val = temp_value_;
    if (!cond_val || !builder_->GetInsertBlock()) {
        return;
    }
    ir::Value* cond_i1 = cond_val;
    if (cond_val->GetType() && cond_val->GetType() != module_->GetI1Type()) {
        ir::Instruction* cmp = builder_->CreateIcmp(module_->GetI1Type(), ir::IcmpPred::NE,
                                                    cond_val, module_->GetInt32Constant(0));
        if (cmp) {
            cond_i1 = cmp;
        }
    }
    builder_->CreateCondBr(cond_i1, true_block, false_block);

    builder_->SetInsertPoint(true_block);
    if_stmt.then_stmt->Accept(*this);
    if (!IsBlockTerminated()) {
        builder_->CreateBr(next_block);
    }

    if (if_stmt.else_stmt.has_value() && if_stmt.else_stmt->get()) {
        builder_->SetInsertPoint(false_block);
        (*if_stmt.else_stmt)->Accept(*this);
        if (!IsBlockTerminated()) {
            builder_->CreateBr(next_block);
        }
    }

    builder_->SetInsertPoint(next_block);
}

void IRGenVisitor::VisitForStmt(ForStmt& for_stmt) {
    IRScopeGuard scope_guard(*this);

    if (for_stmt.init.has_value() && for_stmt.init->get()) {
        (*for_stmt.init)->Accept(*this);
    }

    ir::BasicBlock* cond_block = CreateBasicBlock("for.cond");
    ir::BasicBlock* body_block = CreateBasicBlock("for.body");
    ir::BasicBlock* step_block = CreateBasicBlock("for.step");
    ir::BasicBlock* after_block = CreateBasicBlock("for.after");

    if (!IsBlockTerminated()) {
        builder_->CreateBr(cond_block);
    }

    builder_->SetInsertPoint(cond_block);
    if (for_stmt.cond.has_value() && for_stmt.cond->get()) {
        (*for_stmt.cond)->Accept(*this);
        ir::Value* cond_val = temp_value_;
        if (cond_val && builder_->GetInsertBlock()) {
            ir::Value* cond_i1 = cond_val;
            if (cond_val->GetType() && cond_val->GetType() != module_->GetI1Type()) {
                ir::Instruction* cmp = builder_->CreateIcmp(module_->GetI1Type(), ir::IcmpPred::NE,
                                                            cond_val, module_->GetInt32Constant(0));
                if (cmp) {
                    cond_i1 = cmp;
                }
            }
            builder_->CreateCondBr(cond_i1, body_block, after_block);
        } else {
            builder_->CreateBr(body_block);
        }
    } else {
        builder_->CreateBr(body_block);
    }

    break_targets_.push_back(after_block);
    continue_targets_.push_back(step_block);

    builder_->SetInsertPoint(body_block);
    for_stmt.body->Accept(*this);
    if (!IsBlockTerminated()) {
        builder_->CreateBr(step_block);
    }

    builder_->SetInsertPoint(step_block);
    if (for_stmt.step.has_value() && for_stmt.step->get()) {
        (*for_stmt.step)->Accept(*this);
    }
    builder_->CreateBr(cond_block);

    break_targets_.pop_back();
    continue_targets_.pop_back();
    builder_->SetInsertPoint(after_block);
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
    if (!break_targets_.empty()) {
        builder_->CreateBr(break_targets_.back());
    }
}

void IRGenVisitor::VisitContinueStmt(ContinueStmt& continue_stmt) {
    if (!continue_targets_.empty()) {
        builder_->CreateBr(continue_targets_.back());
    }
}

void IRGenVisitor::VisitReturnStmt(ReturnStmt& return_stmt) {
    if (return_stmt.exp.has_value() && *return_stmt.exp) {
        (*return_stmt.exp)->Accept(*this);
        ir::Value* val = temp_value_;
        if (val && builder_->GetInsertBlock()) {
            builder_->CreateRet(val);
        }
    } else {
        builder_->CreateRetVoid();
    }
}

void IRGenVisitor::VisitGetintStmt(GetintStmt& getint_stmt) {
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
    // LVal -> Ident | Ident '[' Exp ']'; base address from symbol table (alloca, global, or param
    // pointer)
    ir::Value* value = LookupVariable(lval.ident);
    if (!value) {
        return;
    }

    if (!lval.index.has_value() || !*lval.index) {
        // no index: scalar or whole-array-used-as-pointer
        if (is_lval_mode_) {
            temp_value_ = value; // caller will store to this address
        } else {
            ir::Instruction* load = builder_->CreateLoad(value);
            temp_value_ = load ? load : value;
        }
        return;
    }

    (*lval.index)->Accept(*this);
    ir::Value* index_val = temp_value_;
    if (!index_val || !builder_->GetInsertBlock()) {
        return;
    }
    auto* ptr_ty = dynamic_cast<ir::PointerType*>(value->GetType());
    if (!ptr_ty) {
        temp_value_ = value;
        return;
    }
    ir::Type* pointee = ptr_ty->GetPointeeType();
    ir::Type* elem_ty = nullptr; // element type for GEP result pointer (i32* or i8*)
    ir::Instruction* gep = nullptr;
    if (auto* arr_ty = dynamic_cast<ir::ArrayType*>(pointee)) {
        // real array: type [N x T]* -> GEP(base, 0, index) to get element address
        elem_ty = arr_ty->GetElementType();
        gep = builder_->CreateGEP(module_->GetPointerType(elem_ty), value,
                                  module_->GetInt32Constant(0), index_val);
    } else {
        // pointer param: type T* (e.g. int a[] -> i32*) -> GEP(base, index)
        elem_ty = pointee;
        gep = builder_->CreateGEP(module_->GetPointerType(elem_ty), value, index_val);
    }
    if (!gep) {
        temp_value_ = value;
        return;
    }
    if (is_lval_mode_) {
        temp_value_ = gep;
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
    if (const_exp.const_value.has_value()) {
        temp_value_ = module_->GetInt32Constant(const_exp.const_value.value());
    }
}

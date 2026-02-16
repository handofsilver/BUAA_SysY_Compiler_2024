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
// Constant expression evaluation (compile-time, for global initializers)
// -----------------------------------------------------------------------------

int IRGenVisitor::GetConstIntVal(Exp* exp) {
    if (!exp) {
        return 0;
    }
    if (auto* num = dynamic_cast<Number*>(exp)) {
        return num->int_const;
    }
    if (auto* cexp = dynamic_cast<ConstExp*>(exp)) {
        return GetConstIntVal(cexp->inner.get());
    }
    if (auto* uexp = dynamic_cast<UnaryExp*>(exp)) {
        int v = GetConstIntVal(uexp->operand.get());
        if (uexp->op == OpType::PLUS) {
            return v;
        }
        if (uexp->op == OpType::MINU) {
            return -v;
        }
        return 0;
    }
    if (auto* bexp = dynamic_cast<BinaryExp*>(exp)) {
        int l = GetConstIntVal(bexp->lhs.get());
        int r = GetConstIntVal(bexp->rhs.get());
        switch (bexp->op) {
            case OpType::ADD: return l + r;
            case OpType::SUB: return l - r;
            case OpType::MUL: return l * r;
            case OpType::DIV: return (r == 0) ? 0 : (l / r);
            case OpType::MOD: return (r == 0) ? 0 : (l % r);
            default: return 0;
        }
    }
    return 0;
}

// -----------------------------------------------------------------------------
// Helpers for variable/constant definition
// -----------------------------------------------------------------------------

ir::Type* IRGenVisitor::GetCurDeclType() const {
    return (current_decl_btype_ == BType::CHAR) ? static_cast<ir::Type*>(module_->GetI8Type()) :
                                                  static_cast<ir::Type*>(module_->GetI32Type());
}

int IRGenVisitor::EvalArraySizeFromConstExp(ConstExp* cexp) {
    if (!cexp || !cexp->inner) {
        return 1;
    }
    int n = GetConstIntVal(cexp->inner.get());
    return (n <= 0) ? 1 : n;
}

ir::Constant* IRGenVisitor::BuildConstScalarInit(int val) const {
    return (current_decl_btype_ == BType::CHAR) ?
               static_cast<ir::Constant*>(module_->GetInt8Constant(val)) :
               static_cast<ir::Constant*>(module_->GetInt32Constant(val));
}

ir::Constant* IRGenVisitor::BuildConstArrayInit(ir::ArrayType* arr_ty,
                                                const std::vector<int>& values) const {
    std::vector<ir::Constant*> inits;
    for (int v : values) {
        inits.push_back(BuildConstScalarInit(v));
    }
    return module_->CreateConstantArray(arr_ty, inits);
}

void IRGenVisitor::EmitGlobalConstDef(ConstDef& const_def, ir::Type* elem_type) {
    const bool kIsArray = const_def.array_size.has_value() && const_def.array_size->get();
    ir::Type* var_type = nullptr;
    ir::Constant* init = nullptr;

    if (kIsArray) {
        // --- Global const array: type [N x T], init = ConstantArray (no Store allowed) ---
        int n = EvalArraySizeFromConstExp(const_def.array_size->get());
        ir::ArrayType* arr_ty = module_->GetArrayType(elem_type, static_cast<unsigned>(n));
        var_type = arr_ty;

        // Parse init: ConstInitVal is variant<SingleExp, ExpList, StringVal>; we need ExpList.
        std::vector<int> values;
        auto* list = std::get_if<ConstInitVal::ExpList>(&const_def.const_init_val->value);
        if (list) {
            for (auto& cexp : *list) {
                values.push_back(GetConstIntVal(cexp->inner.get()));
            }
        }
        // Pad with zeros if init list is shorter than n (e.g. int a[5] = {1,2}; -> 1,2,0,0,0).
        while (static_cast<int>(values.size()) < n) {
            values.push_back(0);
        }
        init = BuildConstArrayInit(arr_ty, values);
    } else {
        // --- Global const scalar: type T, init = ConstantInt ---
        var_type = elem_type;
        int val = 0;
        auto* single = std::get_if<ConstInitVal::SingleExp>(&const_def.const_init_val->value);
        if (single && single->get()) {
            val = GetConstIntVal((*single)->inner.get());
        }
        init = BuildConstScalarInit(val);
    }

    // Globals: scalar has type T, array has type T* (pointer to [N x T]). Then create and bind.
    ir::Type* global_type = kIsArray ? module_->GetPointerType(var_type) : var_type;
    ir::GlobalVar* gv = module_->CreateGlobalVar(const_def.ident, global_type, init, true);
    RegisterVariable(const_def.ident, gv);
}

void IRGenVisitor::EmitLocalConstDef(ConstDef& const_def, ir::Type* elem_type) {
    const bool kIsArray = const_def.array_size.has_value() && const_def.array_size->get();

    if (kIsArray) {
        // Local const array: alloca [N x T], then GEP+Store for each element
        int n = EvalArraySizeFromConstExp(const_def.array_size->get());
        ir::ArrayType* arr_ty = module_->GetArrayType(elem_type, static_cast<unsigned>(n));
        ir::Instruction* alloca = CreateEntryBlockAlloca(arr_ty, const_def.ident);
        RegisterVariable(const_def.ident, alloca);

        auto* list = std::get_if<ConstInitVal::ExpList>(&const_def.const_init_val->value);
        if (list && builder_->GetInsertBlock()) {
            for (size_t i = 0; i < list->size() && i < static_cast<size_t>(n); ++i) {
                int val = GetConstIntVal((*list)[i]->inner.get());
                ir::Value* to_store = BuildConstScalarInit(val);
                ir::Value* idx = module_->GetInt32Constant(static_cast<int64_t>(i));
                ir::Instruction* gep = builder_->CreateGEP(
                    module_->GetPointerType(elem_type), alloca, module_->GetInt32Constant(0), idx);
                if (gep && to_store) {
                    builder_->CreateStore(to_store, gep);
                }
            }
        }
    } else {
        // Local const scalar: alloca T, Store constant
        ir::Instruction* alloca = CreateEntryBlockAlloca(elem_type, const_def.ident);
        RegisterVariable(const_def.ident, alloca);

        int val = 0;
        auto* single = std::get_if<ConstInitVal::SingleExp>(&const_def.const_init_val->value);
        if (single && single->get() && builder_->GetInsertBlock()) {
            val = GetConstIntVal((*single)->inner.get());
            builder_->CreateStore(BuildConstScalarInit(val), alloca);
        }
    }
}

void IRGenVisitor::EmitGlobalVarDef(VarDef& var_def, ir::Type* elem_type) {
    const bool kIsArray = var_def.array_size.has_value() && var_def.array_size->get();
    ir::Type* var_type = nullptr;
    ir::Constant* init = nullptr;

    if (kIsArray) {
        // --- Global var array: [N x T], init = ConstantArray (or zero-padded) ---
        int n = EvalArraySizeFromConstExp(var_def.array_size->get());
        ir::ArrayType* arr_ty = module_->GetArrayType(elem_type, static_cast<unsigned>(n));
        var_type = arr_ty;

        // VarDef may have no init_val (e.g. int a[3];); if present, parse ExpList and eval each.
        std::vector<int> values;
        if (var_def.init_val) {
            auto* list = std::get_if<InitVal::ExpList>(&var_def.init_val->value);
            if (list) {
                for (auto& e : *list) {
                    values.push_back(GetConstIntVal(e.get()));
                }
            }
        }
        while (static_cast<int>(values.size()) < n) {
            values.push_back(0);
        }
        init = BuildConstArrayInit(arr_ty, values);
    } else {
        // --- Global var scalar: T, optional ConstantInt init ---
        var_type = elem_type;
        if (var_def.init_val) {
            auto* single = std::get_if<InitVal::SingleExp>(&var_def.init_val->value);
            int val = 0;
            if (single && single->get()) {
                val = GetConstIntVal(single->get());
            }
            init = BuildConstScalarInit(val);
        }
    }

    ir::Type* global_type = kIsArray ? module_->GetPointerType(var_type) : var_type;
    ir::GlobalVar* gv = module_->CreateGlobalVar(var_def.ident, global_type, init, false);
    RegisterVariable(var_def.ident, gv);
}

void IRGenVisitor::EmitLocalVarDef(VarDef& var_def, ir::Type* elem_type) {
    const bool kIsArray = var_def.array_size.has_value() && var_def.array_size->get();

    if (kIsArray) {
        // --- Local var array: alloca [N x T], then optional GEP+Store per element ---
        int n = 1;
        if (var_def.array_size && var_def.array_size->get()) {
            n = EvalArraySizeFromConstExp(var_def.array_size->get());
        }
        ir::ArrayType* arr_ty = module_->GetArrayType(elem_type, static_cast<unsigned>(n));
        ir::Instruction* alloca = CreateEntryBlockAlloca(arr_ty, var_def.ident);
        RegisterVariable(var_def.ident, alloca);

        // If init present: visit each Exp in the list (runtime eval), then GEP + Store.
        if (var_def.init_val && builder_->GetInsertBlock()) {
            auto* list = std::get_if<InitVal::ExpList>(&var_def.init_val->value);
            if (list) {
                for (size_t i = 0; i < list->size() && i < static_cast<size_t>(n); ++i) {
                    (*list)[i]->Accept(*this);
                    ir::Value* val = temp_value_;
                    if (val) {
                        ir::Value* idx = module_->GetInt32Constant(static_cast<int64_t>(i));
                        ir::Instruction* gep =
                            builder_->CreateGEP(module_->GetPointerType(elem_type), alloca,
                                                module_->GetInt32Constant(0), idx);
                        if (gep) {
                            builder_->CreateStore(val, gep);
                        }
                    }
                }
            }
        }
    } else {
        // --- Local var scalar: alloca T, optional Store(exp result) ---
        ir::Instruction* alloca = CreateEntryBlockAlloca(elem_type, var_def.ident);
        RegisterVariable(var_def.ident, alloca);

        // If init present: visit the single Exp, then Store(temp_value_, alloca).
        if (var_def.init_val && builder_->GetInsertBlock()) {
            auto* single = std::get_if<InitVal::SingleExp>(&var_def.init_val->value);
            if (single && single->get()) {
                (*single)->Accept(*this);
                if (temp_value_) {
                    builder_->CreateStore(temp_value_, alloca);
                }
            }
        }
    }
}

ir::Value* IRGenVisitor::EmitGlobalStringLiteral(const std::string& str) {
    // Null-terminated i8 array; empty string -> [1 x i8] with 0.
    std::vector<ir::Constant*> inits;
    for (unsigned char c : str) {
        inits.push_back(module_->GetInt8Constant(static_cast<int64_t>(c)));
    }
    inits.push_back(module_->GetInt8Constant(0));
    ir::Type* i8 = module_->GetI8Type();
    ir::ArrayType* arr_ty = module_->GetArrayType(i8, static_cast<unsigned>(inits.size()));
    ir::Constant* init = module_->CreateConstantArray(arr_ty, inits);
    std::string name = ".str." + std::to_string(printf_str_counter_++);
    ir::GlobalVar* g = module_->CreateGlobalVar(name, module_->GetPointerType(arr_ty), init, true);
    return g;
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
    // Parameters are set up in VisitFuncDef (alloca + store + RegisterVariable).
    // This node is not visited during our traversal; no-op for safety.
    (void)func_f_param;
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
    if (block_stmt.block) {
        block_stmt.block->Accept(*this);
    }
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
    // printf(format_string, exp_list): format_string can contain %d, %c and literal segments.
    // We scan format_string once; for each literal segment emit putstr(global_str), for %d/%c
    // emit putint/putch with the next exp from exp_list (order matches grammar).
    if (!builder_->GetInsertBlock()) {
        return;
    }
    const std::string& fmt = printf_stmt.format_string;
    size_t exp_idx = 0;  // index into printf_stmt.exp_list for %d and %c
    std::string literal; // current run of non-format chars (will be emitted as one putstr)

    for (size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] == '%' && i + 1 < fmt.size()) {
            // Flush any literal we accumulated before this format specifier.
            if (!literal.empty()) {
                ir::Value* str_ptr = EmitGlobalStringLiteral(literal);
                if (str_ptr) {
                    ir::Function* putstr_fn = module_->GetFunction("putstr");
                    if (putstr_fn) {
                        builder_->CreateCall(ir::GetVoidType(), putstr_fn, {str_ptr});
                    }
                }
                literal.clear();
            }
            // Handle %d: evaluate next exp, call putint(val).
            if (fmt[i + 1] == 'd') {
                if (exp_idx < printf_stmt.exp_list.size()) {
                    printf_stmt.exp_list[exp_idx]->Accept(*this);
                    ir::Value* val = temp_value_;
                    if (val) {
                        ir::Function* putint_fn = module_->GetFunction("putint");
                        if (putint_fn) {
                            builder_->CreateCall(ir::GetVoidType(), putint_fn, {val});
                        }
                    }
                    ++exp_idx;
                }
                ++i; // skip the 'd'
            } else if (fmt[i + 1] == 'c') {
                // Handle %c: evaluate next exp, call putch(val).
                if (exp_idx < printf_stmt.exp_list.size()) {
                    printf_stmt.exp_list[exp_idx]->Accept(*this);
                    ir::Value* val = temp_value_;
                    if (val) {
                        ir::Function* putch_fn = module_->GetFunction("putch");
                        if (putch_fn) {
                            builder_->CreateCall(ir::GetVoidType(), putch_fn, {val});
                        }
                    }
                    ++exp_idx;
                }
                ++i; // skip the 'c'
            } else if (fmt[i + 1] == '%') {
                ++i;
            }
        } else {
            // Ordinary character: append to current literal segment.
            literal.push_back(fmt[i]);
        }
    }
    // Flush trailing literal (e.g. "\n" at end of "hello %d\n").
    if (!literal.empty()) {
        ir::Value* str_ptr = EmitGlobalStringLiteral(literal);
        if (str_ptr) {
            ir::Function* putstr_fn = module_->GetFunction("putstr");
            if (putstr_fn) {
                builder_->CreateCall(ir::GetVoidType(), putstr_fn, {str_ptr});
            }
        }
    }
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
    // PrimaryExp: delegate to child; PLUS: result already in temp_value_; MINU/NOT: handled below.
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
    ir::Function* callee = module_->GetFunction(func_call.ident);
    if (!callee || !builder_->GetInsertBlock()) {
        return;
    }
    call_args_.clear();
    if (func_call.func_r_params) {
        func_call.func_r_params->Accept(*this);
    }
    ir::Type* ret_type = nullptr;
    if (ir::Type* ft = callee->GetType()) {
        if (auto* fty = dynamic_cast<ir::FunctionType*>(ft)) {
            ret_type = fty->GetReturnType();
        }
    }
    ir::Instruction* call = builder_->CreateCall(ret_type, callee, call_args_);
    if (call && ret_type && ret_type != ir::GetVoidType()) {
        temp_value_ = call;
    }
}

void IRGenVisitor::VisitFuncRParams(FuncRParams& func_r_params) {
    for (auto& exp : func_r_params.exp_list) {
        exp->Accept(*this);
        if (temp_value_) {
            call_args_.push_back(temp_value_);
        }
    }
}

void IRGenVisitor::VisitConstExp(ConstExp& const_exp) {
    if (const_exp.const_value.has_value()) {
        temp_value_ = module_->GetInt32Constant(const_exp.const_value.value());
    } else if (const_exp.inner) {
        const_exp.inner->Accept(*this);
    }
}

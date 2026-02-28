/**
 * @file IRGenVisitor.cpp
 * @brief Core IRGenVisitor implementation: constructor, control-flow helpers, type conversion,
 * declaration emission, CompUnit/Decl/Func dispatch. Stmt and Expr visitors are in
 * IRGenVisitorStmt.cpp and IRGenVisitorExpr.cpp respectively.
 */
#include "irgen/IRGenVisitor.h"
#include "AST.h"
#include "ir/Constant.h"
#include "ir/Instruction.h"
#include "ir/Type.h"
#include "irgen/ConstExpEvaluator.h"
#include "irgen/IRScopeGuard.h"
#include "irgen/TypeMapping.h"

// -----------------------------------------------------------------------------
// Constructor and module access
// -----------------------------------------------------------------------------

IRGenVisitor::IRGenVisitor() :
module_(std::make_unique<ir::Module>()),
builder_(std::make_unique<ir::IRBuilder>()),
ctx_(module_.get(), builder_.get(), types_),
decl_emitter_(ctx_) {}

std::unique_ptr<ir::Module> IRGenVisitor::Translate(CompUnit& comp_unit) {
    IRScopeGuard guard(ctx_);
    comp_unit.Accept(*this);
    return std::move(module_);
}

// -----------------------------------------------------------------------------
// Control-flow helpers
// -----------------------------------------------------------------------------

ir::BasicBlock* IRGenVisitor::CreateBasicBlock(const std::string& name) {
    if (!ctx_.current_function || !ctx_.builder) {
        return nullptr;
    }
    std::string label = (name == "entry") ? name : (name + "." + ctx_.builder->GetNextSSAName());
    auto block = std::make_unique<ir::BasicBlock>(label);
    ir::BasicBlock* ptr = block.get();
    ctx_.current_function->AddBlock(std::move(block));
    return ptr;
}

bool IRGenVisitor::IsBlockTerminated() const {
    ir::BasicBlock* block = ctx_.builder->GetInsertBlock();
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

ir::Value* IRGenVisitor::CoerceToI1(ir::Value* cond_val) {
    if (!cond_val || !ctx_.builder->GetInsertBlock()) {
        return nullptr;
    }
    if (cond_val->GetType() && cond_val->GetType() == ctx_.types.GetI1Type()) {
        return cond_val;
    }
    cond_val = PromoteToI32(cond_val);
    ir::Instruction* cmp = ctx_.builder->CreateIcmp(ctx_.types.GetI1Type(), ir::IcmpPred::NE,
                                                    cond_val, ctx_.module->GetInt32Constant(0));
    return cmp ? cmp : cond_val;
}

void IRGenVisitor::EmitShortCircuitAND(Exp* lhs, Exp* rhs) {
    ir::Value* result_slot = decl_emitter_.EmitLocalAlloca("", ctx_.types.GetI32Type(), 0);
    if (!result_slot || !ctx_.builder->GetInsertBlock()) {
        return;
    }
    ir::BasicBlock* true_block = CreateBasicBlock("and.then");
    ir::BasicBlock* false_block = CreateBasicBlock("and.false");
    ir::BasicBlock* merge_block = CreateBasicBlock("and.merge");
    if (!true_block || !false_block || !merge_block) {
        return;
    }

    lhs->Accept(*this);
    ir::Value* cond_val = temp_value_;
    if (!cond_val) {
        return;
    }
    ir::Value* cond_i1 = CoerceToI1(cond_val);
    if (!cond_i1) {
        return;
    }
    ctx_.builder->CreateCondBr(cond_i1, true_block, false_block);

    ctx_.builder->SetInsertPoint(false_block);
    ctx_.builder->CreateStore(ctx_.module->GetInt32Constant(0), result_slot);
    ctx_.builder->CreateBr(merge_block);

    ctx_.builder->SetInsertPoint(true_block);
    rhs->Accept(*this);
    ir::Value* rhs_val = PromoteToI32(temp_value_);
    if (!IsBlockTerminated()) {
        if (rhs_val) {
            ctx_.builder->CreateStore(rhs_val, result_slot);
        }
        ctx_.builder->CreateBr(merge_block);
    }

    ctx_.builder->SetInsertPoint(merge_block);
    ir::Instruction* load = ctx_.builder->CreateLoad(result_slot);
    temp_value_ = load ? load : result_slot;
}

void IRGenVisitor::EmitShortCircuitOR(Exp* lhs, Exp* rhs) {
    ir::Value* result_slot = decl_emitter_.EmitLocalAlloca("", ctx_.types.GetI32Type(), 0);
    if (!result_slot || !ctx_.builder->GetInsertBlock()) {
        return;
    }
    ir::BasicBlock* true_block = CreateBasicBlock("or.then");
    ir::BasicBlock* rhs_block = CreateBasicBlock("or.rhs");
    ir::BasicBlock* merge_block = CreateBasicBlock("or.merge");
    if (!true_block || !rhs_block || !merge_block) {
        return;
    }

    lhs->Accept(*this);
    ir::Value* cond_val = temp_value_;
    if (!cond_val) {
        return;
    }
    ir::Value* cond_i1 = CoerceToI1(cond_val);
    if (!cond_i1) {
        return;
    }
    ctx_.builder->CreateCondBr(cond_i1, true_block, rhs_block);

    ctx_.builder->SetInsertPoint(true_block);
    ctx_.builder->CreateStore(ctx_.module->GetInt32Constant(1), result_slot);
    ctx_.builder->CreateBr(merge_block);

    ctx_.builder->SetInsertPoint(rhs_block);
    rhs->Accept(*this);
    ir::Value* rhs_val = PromoteToI32(temp_value_);
    if (!IsBlockTerminated()) {
        if (rhs_val) {
            ctx_.builder->CreateStore(rhs_val, result_slot);
        }
        ctx_.builder->CreateBr(merge_block);
    }

    ctx_.builder->SetInsertPoint(merge_block);
    ir::Instruction* load = ctx_.builder->CreateLoad(result_slot);
    temp_value_ = load ? load : result_slot;
}

// -----------------------------------------------------------------------------
// Implicit type conversion (SysY int/char)
// -----------------------------------------------------------------------------

ir::Value* IRGenVisitor::PromoteToI32(ir::Value* v) {
    if (!v || !ctx_.builder->GetInsertBlock()) {
        return v;
    }
    ir::Type* ty = v->GetType();
    auto* int_ty = dynamic_cast<ir::IntegerType*>(ty);
    if (!int_ty || int_ty->GetBits() != 8) {
        return v;
    }
    ir::Instruction* z = ctx_.builder->CreateZext(v, ctx_.types.GetI32Type());
    return z ? z : v;
}

ir::Value* IRGenVisitor::ConvertToTargetType(ir::Value* v, ir::Type* target_ty) {
    if (!v || !target_ty || !ctx_.builder->GetInsertBlock()) {
        return v;
    }
    auto* target_int = dynamic_cast<ir::IntegerType*>(target_ty);
    auto* val_int = dynamic_cast<ir::IntegerType*>(v->GetType());
    if (!target_int || !val_int) {
        return v;
    }
    unsigned target_bits = target_int->GetBits();
    unsigned val_bits = val_int->GetBits();
    if (target_bits == val_bits) {
        return v;
    }
    if (target_bits == 8 && val_bits == 32) {
        ir::Instruction* t = ctx_.builder->CreateTrunc(v, ctx_.types.GetI8Type());
        return t ? t : v;
    }
    if (target_bits == 32 && val_bits == 8) {
        ir::Instruction* z = ctx_.builder->CreateZext(v, ctx_.types.GetI32Type());
        return z ? z : v;
    }
    return v;
}

// -----------------------------------------------------------------------------
// Helpers for variable/constant definition
// -----------------------------------------------------------------------------

ir::Type* IRGenVisitor::GetCurDeclType() const {
    return (current_decl_btype_ == BType::CHAR) ? static_cast<ir::Type*>(ctx_.types.GetI8Type()) :
                                                  static_cast<ir::Type*>(ctx_.types.GetI32Type());
}

int IRGenVisitor::EvalArraySizeFromConstExp(ConstExp* cexp) {
    if (!cexp || !cexp->inner) {
        return 1;
    }
    int n = irgen::EvalConstInt(cexp->inner.get());
    return (n <= 0) ? 1 : n;
}

ir::ConstantInt* IRGenVisitor::BuildConstScalarInit(int val) const {
    return (current_decl_btype_ == BType::CHAR) ?
               static_cast<ir::ConstantInt*>(ctx_.module->GetInt8Constant(val)) :
               static_cast<ir::ConstantInt*>(ctx_.module->GetInt32Constant(val));
}

void IRGenVisitor::EmitGlobalConstDef(ConstDef& const_def, ir::Type* elem_type) {
    const bool kIsArray = const_def.array_size.has_value() && const_def.array_size->get();
    int size = kIsArray ? EvalArraySizeFromConstExp(const_def.array_size->get()) : 0;

    // Collect compile-time init values from AST.
    std::vector<int> init_vals;
    if (kIsArray) {
        auto* list = std::get_if<ConstInitVal::ExpList>(&const_def.const_init_val->value);
        if (list) {
            for (auto& cexp : *list) {
                init_vals.push_back(irgen::EvalConstInt(cexp->inner.get()));
            }
        }
        auto* str = std::get_if<ConstInitVal::StringVal>(&const_def.const_init_val->value);
        if (str) {
            for (unsigned char c : *str) {
                init_vals.push_back(static_cast<int>(c));
            }
            init_vals.push_back(0);
        }
    } else {
        int val = 0;
        auto* single = std::get_if<ConstInitVal::SingleExp>(&const_def.const_init_val->value);
        if (single && single->get()) {
            val = irgen::EvalConstInt((*single)->inner.get());
        }
        init_vals.push_back(val);
    }

    decl_emitter_.EmitGlobal(const_def.ident, elem_type, size, init_vals, true);
}

void IRGenVisitor::EmitLocalConstDef(ConstDef& const_def, ir::Type* elem_type) {
    const bool kIsArray = const_def.array_size.has_value() && const_def.array_size->get();
    int size = kIsArray ? EvalArraySizeFromConstExp(const_def.array_size->get()) : 0;

    ir::Value* alloca_ptr = decl_emitter_.EmitLocalAlloca(const_def.ident, elem_type, size);
    ctx_.RegisterVariable(const_def.ident, alloca_ptr);

    if (kIsArray) {
        auto* list = std::get_if<ConstInitVal::ExpList>(&const_def.const_init_val->value);
        if (list) {
            std::vector<ir::Value*> vals;
            vals.reserve(list->size());
            for (auto& cexp : *list) {
                vals.push_back(BuildConstScalarInit(irgen::EvalConstInt(cexp->inner.get())));
            }
            decl_emitter_.EmitLocalArrayInit(alloca_ptr, elem_type, size, vals);
        }
        auto* str = std::get_if<ConstInitVal::StringVal>(&const_def.const_init_val->value);
        if (str) {
            decl_emitter_.EmitLocalStringInit(alloca_ptr, elem_type, size, *str);
        }
    } else {
        auto* single = std::get_if<ConstInitVal::SingleExp>(&const_def.const_init_val->value);
        if (single && single->get() && ctx_.builder->GetInsertBlock()) {
            int val = irgen::EvalConstInt((*single)->inner.get());
            ctx_.builder->CreateStore(BuildConstScalarInit(val), alloca_ptr);
        }
    }
}

void IRGenVisitor::EmitGlobalVarDef(VarDef& var_def, ir::Type* elem_type) {
    const bool kIsArray = var_def.array_size.has_value() && var_def.array_size->get();
    int size = kIsArray ? EvalArraySizeFromConstExp(var_def.array_size->get()) : 0;

    // Collect compile-time init values from AST (empty if no initializer → zeroinitializer).
    std::vector<int> init_vals;
    if (var_def.init_val) {
        if (kIsArray) {
            auto* list = std::get_if<InitVal::ExpList>(&var_def.init_val->value);
            if (list) {
                for (auto& e : *list) {
                    init_vals.push_back(irgen::EvalConstInt(e.get()));
                }
            }
            auto* str = std::get_if<InitVal::StringVal>(&var_def.init_val->value);
            if (str) {
                for (unsigned char c : *str) {
                    init_vals.push_back(static_cast<int>(c));
                }
            }
        } else {
            auto* single = std::get_if<InitVal::SingleExp>(&var_def.init_val->value);
            int val = 0;
            if (single && single->get()) {
                val = irgen::EvalConstInt(single->get());
            }
            init_vals.push_back(val);
        }
    }

    decl_emitter_.EmitGlobal(var_def.ident, elem_type, size, init_vals, false);
}

void IRGenVisitor::EmitLocalVarDef(VarDef& var_def, ir::Type* elem_type) {
    const bool kIsArray = var_def.array_size.has_value() && var_def.array_size->get();
    int size = kIsArray ? EvalArraySizeFromConstExp(var_def.array_size->get()) : 0;

    ir::Value* alloca_ptr = decl_emitter_.EmitLocalAlloca(var_def.ident, elem_type, size);
    ctx_.RegisterVariable(var_def.ident, alloca_ptr);

    if (kIsArray) {
        if (var_def.init_val && ctx_.builder->GetInsertBlock()) {
            auto* list = std::get_if<InitVal::ExpList>(&var_def.init_val->value);
            if (list) {
                // Visitor traverses AST to collect runtime values.
                std::vector<ir::Value*> vals;
                vals.reserve(list->size());
                for (auto& exp : *list) {
                    exp->Accept(*this);
                    ir::Value* v =
                        temp_value_ ? ConvertToTargetType(temp_value_, elem_type) : nullptr;
                    vals.push_back(v);
                }
                decl_emitter_.EmitLocalArrayInit(alloca_ptr, elem_type, size, vals);
            }
            auto* str = std::get_if<InitVal::StringVal>(&var_def.init_val->value);
            if (str) {
                decl_emitter_.EmitLocalStringInit(alloca_ptr, elem_type, size, *str);
            }
        }
    } else {
        if (var_def.init_val && ctx_.builder->GetInsertBlock()) {
            auto* single = std::get_if<InitVal::SingleExp>(&var_def.init_val->value);
            if (single && single->get()) {
                (*single)->Accept(*this);
                ir::Value* val =
                    temp_value_ ? ConvertToTargetType(temp_value_, elem_type) : nullptr;
                if (val) {
                    ctx_.builder->CreateStore(val, alloca_ptr);
                }
            }
        }
    }
}

// =============================================================================
// CompUnit and declaration dispatch (merged from IRGenVisitorDecl.cpp)
// =============================================================================

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
    (void)const_init_val;
}

void IRGenVisitor::VisitInitVal(InitVal& init_val) {
    (void)init_val;
}

// =============================================================================
// Function definitions (merged from IRGenVisitorFunc.cpp)
// =============================================================================

void IRGenVisitor::VisitFuncDef(FuncDef& func_def) {
    ir::Type* return_type = irgen::BTypeToReturnType(func_def.func_type);
    std::vector<ir::Type*> param_types;
    for (const auto& p : func_def.func_f_params) {
        param_types.push_back(irgen::BTypeToParamType(p->btype, p->is_array));
    }
    ir::Function* func = ctx_.module->CreateFunction(func_def.ident, return_type, param_types);
    ctx_.current_function = func;
    IRScopeGuard scope_guard(ctx_);

    ir::BasicBlock* entry = CreateBasicBlock("entry");
    ctx_.builder->SetInsertPoint(entry);
    ctx_.builder->ResetSSACounter(static_cast<int>(func_def.func_f_params.size()));

    for (size_t i = 0; i < func_def.func_f_params.size(); ++i) {
        const auto& p = func_def.func_f_params[i];
        ir::Value* arg_val = func->GetArgument(i);
        if (!arg_val) {
            continue;
        }
        if (p->is_array) {
            ctx_.RegisterVariable(p->ident, arg_val);
        } else {
            ir::Type* alloc_ty = irgen::BTypeToAllocaType(p->btype);
            ir::Value* alloca_inst = decl_emitter_.EmitLocalAlloca("", alloc_ty, 0);
            if (alloca_inst && ctx_.builder->GetInsertBlock()) {
                ctx_.builder->CreateStore(arg_val, alloca_inst);
                ctx_.RegisterVariable(p->ident, alloca_inst);
            }
        }
    }

    func_def.block->Accept(*this);

    if (func_def.func_type == BType::VOID && !IsBlockTerminated()) {
        ctx_.builder->CreateRetVoid();
    }
}

void IRGenVisitor::VisitMainFuncDef(MainFuncDef& main_func_def) {
    ir::Function* func = ctx_.module->CreateFunction("main", ctx_.types.GetI32Type(), {});
    ctx_.current_function = func;
    IRScopeGuard scope_guard(ctx_);

    ir::BasicBlock* entry = CreateBasicBlock("entry");
    ctx_.builder->SetInsertPoint(entry);
    ctx_.builder->ResetSSACounter(0);

    main_func_def.block->Accept(*this);

    if (!IsBlockTerminated()) {
        ctx_.builder->CreateRet(ctx_.module->GetInt32Constant(0));
    }
}

void IRGenVisitor::VisitFuncFParam(FuncFParam& func_f_param) {
    (void)func_f_param;
}

/**
 * @file IRGenVisitorFunc.cpp
 * @brief IRGenVisitor: function and main definition, formal params.
 */
#include "AST.h"
#include "IRGenVisitor.h"
#include "IRScopeGuard.h"
#include "irgen/TypeMapping.h"

// -----------------------------------------------------------------------------
// Functions
// -----------------------------------------------------------------------------

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
            ir::Instruction* alloca_inst = CreateEntryBlockAlloca(alloc_ty);
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

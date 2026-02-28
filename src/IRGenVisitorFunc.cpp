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
    ir::Function* func = module_->CreateFunction(func_def.ident, return_type, param_types);
    current_function_ = func;
    IRScopeGuard scope_guard(*this);

    ir::BasicBlock* entry = CreateBasicBlock("entry");
    builder_->SetInsertPoint(entry);
    builder_->ResetSSACounter(static_cast<int>(func_def.func_f_params.size()));

    for (size_t i = 0; i < func_def.func_f_params.size(); ++i) {
        const auto& p = func_def.func_f_params[i];
        ir::Value* arg_val = func->GetArgument(i);
        if (!arg_val) {
            continue;
        }
        if (p->is_array) {
            RegisterVariable(p->ident, arg_val);
        } else {
            ir::Type* alloc_ty = irgen::BTypeToAllocaType(p->btype);
            ir::Instruction* alloca_inst = CreateEntryBlockAlloca(alloc_ty);
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
    ir::Function* func = module_->CreateFunction("main", types_.GetI32Type(), {});
    current_function_ = func;
    IRScopeGuard scope_guard(*this);

    ir::BasicBlock* entry = CreateBasicBlock("entry");
    builder_->SetInsertPoint(entry);
    builder_->ResetSSACounter(0);

    main_func_def.block->Accept(*this);

    if (!IsBlockTerminated()) {
        builder_->CreateRet(module_->GetInt32Constant(0));
    }
}

void IRGenVisitor::VisitFuncFParam(FuncFParam& func_f_param) {
    (void)func_f_param;
}

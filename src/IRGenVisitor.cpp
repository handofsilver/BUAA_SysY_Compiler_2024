/**
 * @file IRGenVisitor.cpp
 * @brief Core implementation of IRGenVisitor: constructor, Translate, control-flow helpers,
 * type conversion, and declaration emission helpers. Visit* for Decl/Func/Stmt/Expr are in
 * IRGenVisitorDecl.cpp, IRGenVisitorFunc.cpp, IRGenVisitorStmt.cpp, IRGenVisitorExpr.cpp.
 */
#include "IRGenVisitor.h"
#include "AST.h"
#include "IRScopeGuard.h"
#include "ir/Constant.h"
#include "ir/Instruction.h"
#include "ir/Type.h"
#include "irgen/ConstExpEvaluator.h"

// -----------------------------------------------------------------------------
// Constructor and module access
// -----------------------------------------------------------------------------

IRGenVisitor::IRGenVisitor() :
module_(std::make_unique<ir::Module>()),
builder_(std::make_unique<ir::IRBuilder>()),
ctx_(module_.get(), builder_.get(), types_) {}

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
    ir::Instruction* result_slot = CreateEntryBlockAlloca(ctx_.types.GetI32Type());
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
    ir::Instruction* result_slot = CreateEntryBlockAlloca(ctx_.types.GetI32Type());
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

ir::Instruction* IRGenVisitor::CreateEntryBlockAlloca(ir::Type* type) {
    if (!ctx_.current_function || !type || !ctx_.builder) {
        return nullptr;
    }
    const auto& blocks = ctx_.current_function->GetBlocks();
    if (blocks.empty()) {
        return nullptr;
    }
    ir::BasicBlock* entry = blocks.front().get();
    ir::Type* ptr_type = ctx_.types.GetPointerType(type);
    std::string name = ctx_.builder->GetNextSSAName();
    auto inst = std::make_unique<ir::AllocaInst>(name, ptr_type, entry);
    ir::Instruction* result = inst.get();
    entry->AddInstructionAtFront(std::move(inst));
    return result;
}

// -----------------------------------------------------------------------------
// Implicit type conversion (SysY int/char)
// -----------------------------------------------------------------------------

ir::Type* IRGenVisitor::GetPointeeType(ir::Value* ptr) const {
    if (!ptr || !ptr->GetType()) {
        return nullptr;
    }
    auto* pt = dynamic_cast<ir::PointerType*>(ptr->GetType());
    return pt ? pt->GetPointeeType() : nullptr;
}

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

ir::ConstantArray* IRGenVisitor::BuildConstArrayInit(ir::ArrayType* arr_ty,
                                                     const std::vector<int>& values) const {
    std::vector<ir::Constant*> inits;
    for (int v : values) {
        inits.push_back(BuildConstScalarInit(v));
    }
    return ctx_.module->CreateConstantArray(arr_ty, inits);
}

void IRGenVisitor::EmitGlobalConstDef(ConstDef& const_def, ir::Type* elem_type) {
    const bool kIsArray = const_def.array_size.has_value() && const_def.array_size->get();
    ir::Type* var_type = nullptr;
    ir::Constant* init = nullptr;

    if (kIsArray) {
        int n = EvalArraySizeFromConstExp(const_def.array_size->get());
        ir::ArrayType* arr_ty = ctx_.types.GetArrayType(elem_type, static_cast<unsigned>(n));
        var_type = arr_ty;

        std::vector<int> values;
        auto* list = std::get_if<ConstInitVal::ExpList>(&const_def.const_init_val->value);
        if (list) {
            for (auto& cexp : *list) {
                values.push_back(irgen::EvalConstInt(cexp->inner.get()));
            }
        }
        auto* str = std::get_if<ConstInitVal::StringVal>(&const_def.const_init_val->value);
        if (str) {
            for (unsigned char c : *str) {
                values.push_back(static_cast<int>(c));
            }
            values.push_back(0);
        }
        while (static_cast<int>(values.size()) < n) {
            values.push_back(0);
        }
        init = BuildConstArrayInit(arr_ty, values);
    } else {
        var_type = elem_type;
        int val = 0;
        auto* single = std::get_if<ConstInitVal::SingleExp>(&const_def.const_init_val->value);
        if (single && single->get()) {
            val = irgen::EvalConstInt((*single)->inner.get());
        }
        init = BuildConstScalarInit(val);
    }

    ir::Type* global_type = ctx_.types.GetPointerType(var_type);
    ir::GlobalVar* gv = ctx_.module->CreateGlobalVar(const_def.ident, global_type, init, true);
    ctx_.RegisterVariable(const_def.ident, gv);
}

void IRGenVisitor::EmitLocalConstDef(ConstDef& const_def, ir::Type* elem_type) {
    const bool kIsArray = const_def.array_size.has_value() && const_def.array_size->get();

    if (kIsArray) {
        int n = EvalArraySizeFromConstExp(const_def.array_size->get());
        ir::ArrayType* arr_ty = ctx_.types.GetArrayType(elem_type, static_cast<unsigned>(n));
        ir::Instruction* alloca = CreateEntryBlockAlloca(arr_ty);
        ctx_.RegisterVariable(const_def.ident, alloca);

        auto* list = std::get_if<ConstInitVal::ExpList>(&const_def.const_init_val->value);
        if (list && ctx_.builder->GetInsertBlock()) {
            for (size_t i = 0; i < list->size() && i < static_cast<size_t>(n); ++i) {
                int val = irgen::EvalConstInt((*list)[i]->inner.get());
                ir::Value* to_store = BuildConstScalarInit(val);
                ir::Value* idx = ctx_.module->GetInt32Constant(static_cast<int64_t>(i));
                ir::Instruction* gep =
                    ctx_.builder->CreateGEP(ctx_.types.GetPointerType(elem_type), alloca,
                                            ctx_.module->GetInt32Constant(0), idx);
                if (gep && to_store) {
                    ctx_.builder->CreateStore(to_store, gep);
                }
            }
        } else {
            auto* str = std::get_if<ConstInitVal::StringVal>(&const_def.const_init_val->value);
            if (str && ctx_.builder->GetInsertBlock()) {
                ir::Value* global_str = EmitGlobalStringLiteral(*str);
                ir::Type* i8 = ctx_.types.GetI8Type();
                size_t copy_len =
                    static_cast<size_t>(std::min(n, static_cast<int>(str->size()) + 1));
                for (size_t i = 0; i < copy_len; ++i) {
                    ir::Value* idx = ctx_.module->GetInt32Constant(static_cast<int64_t>(i));
                    ir::Instruction* src_gep =
                        ctx_.builder->CreateGEP(ctx_.types.GetPointerType(i8), global_str,
                                                ctx_.module->GetInt32Constant(0), idx);
                    if (!src_gep) {
                        continue;
                    }
                    ir::Instruction* load = ctx_.builder->CreateLoad(src_gep);
                    if (!load) {
                        continue;
                    }
                    ir::Instruction* dst_gep =
                        ctx_.builder->CreateGEP(ctx_.types.GetPointerType(elem_type), alloca,
                                                ctx_.module->GetInt32Constant(0), idx);
                    if (dst_gep) {
                        ctx_.builder->CreateStore(load, dst_gep);
                    }
                }
                for (size_t i = copy_len; i < static_cast<size_t>(n); ++i) {
                    ir::Value* idx = ctx_.module->GetInt32Constant(static_cast<int64_t>(i));
                    ir::Instruction* dst_gep =
                        ctx_.builder->CreateGEP(ctx_.types.GetPointerType(elem_type), alloca,
                                                ctx_.module->GetInt32Constant(0), idx);
                    if (dst_gep) {
                        ctx_.builder->CreateStore(ctx_.module->GetInt8Constant(0), dst_gep);
                    }
                }
            }
        }
    } else {
        ir::Instruction* alloca = CreateEntryBlockAlloca(elem_type);
        ctx_.RegisterVariable(const_def.ident, alloca);

        int val = 0;
        auto* single = std::get_if<ConstInitVal::SingleExp>(&const_def.const_init_val->value);
        if (single && single->get() && ctx_.builder->GetInsertBlock()) {
            val = irgen::EvalConstInt((*single)->inner.get());
            ctx_.builder->CreateStore(BuildConstScalarInit(val), alloca);
        }
    }
}

void IRGenVisitor::EmitGlobalVarDef(VarDef& var_def, ir::Type* elem_type) {
    const bool kIsArray = var_def.array_size.has_value() && var_def.array_size->get();
    ir::Type* var_type = nullptr;
    ir::Constant* init = nullptr;

    if (kIsArray) {
        int n = EvalArraySizeFromConstExp(var_def.array_size->get());
        ir::ArrayType* arr_ty = ctx_.types.GetArrayType(elem_type, static_cast<unsigned>(n));
        var_type = arr_ty;

        std::vector<int> values;
        if (var_def.init_val) {
            auto* list = std::get_if<InitVal::ExpList>(&var_def.init_val->value);
            if (list) {
                for (auto& e : *list) {
                    values.push_back(irgen::EvalConstInt(e.get()));
                }
            }
            auto* str = std::get_if<InitVal::StringVal>(&var_def.init_val->value);
            if (str) {
                for (unsigned char c : *str) {
                    values.push_back(static_cast<int>(c));
                }
            }
            while (static_cast<int>(values.size()) < n) {
                values.push_back(0);
            }
            init = BuildConstArrayInit(arr_ty, values);
        }
    } else {
        var_type = elem_type;
        if (var_def.init_val) {
            auto* single = std::get_if<InitVal::SingleExp>(&var_def.init_val->value);
            int val = 0;
            if (single && single->get()) {
                val = irgen::EvalConstInt(single->get());
            }
            init = BuildConstScalarInit(val);
        }
    }

    ir::Type* global_type = ctx_.types.GetPointerType(var_type);
    ir::GlobalVar* gv = ctx_.module->CreateGlobalVar(var_def.ident, global_type, init, false);
    ctx_.RegisterVariable(var_def.ident, gv);
}

void IRGenVisitor::EmitLocalVarDef(VarDef& var_def, ir::Type* elem_type) {
    const bool kIsArray = var_def.array_size.has_value() && var_def.array_size->get();

    if (kIsArray) {
        int n = 1;
        if (var_def.array_size && var_def.array_size->get()) {
            n = EvalArraySizeFromConstExp(var_def.array_size->get());
        }
        ir::ArrayType* arr_ty = ctx_.types.GetArrayType(elem_type, static_cast<unsigned>(n));
        ir::Instruction* alloca = CreateEntryBlockAlloca(arr_ty);
        ctx_.RegisterVariable(var_def.ident, alloca);

        if (var_def.init_val && ctx_.builder->GetInsertBlock()) {
            auto* list = std::get_if<InitVal::ExpList>(&var_def.init_val->value);
            if (list) {
                for (size_t i = 0; i < list->size() && i < static_cast<size_t>(n); ++i) {
                    (*list)[i]->Accept(*this);
                    ir::Value* val = temp_value_;
                    if (val) {
                        val = ConvertToTargetType(val, elem_type);
                        ir::Value* idx = ctx_.module->GetInt32Constant(static_cast<int64_t>(i));
                        ir::Instruction* gep =
                            ctx_.builder->CreateGEP(ctx_.types.GetPointerType(elem_type), alloca,
                                                    ctx_.module->GetInt32Constant(0), idx);
                        if (gep) {
                            ctx_.builder->CreateStore(val, gep);
                        }
                    }
                }
            } else {
                auto* str = std::get_if<InitVal::StringVal>(&var_def.init_val->value);
                if (str) {
                    ir::Value* global_str = EmitGlobalStringLiteral(*str);
                    ir::Type* i8 = ctx_.types.GetI8Type();
                    size_t copy_len =
                        static_cast<size_t>(std::min(n, static_cast<int>(str->size()) + 1));
                    for (size_t i = 0; i < copy_len; ++i) {
                        ir::Value* idx = ctx_.module->GetInt32Constant(static_cast<int64_t>(i));
                        ir::Instruction* src_gep =
                            ctx_.builder->CreateGEP(ctx_.types.GetPointerType(i8), global_str,
                                                    ctx_.module->GetInt32Constant(0), idx);
                        if (!src_gep) {
                            continue;
                        }
                        ir::Instruction* load = ctx_.builder->CreateLoad(src_gep);
                        if (!load) {
                            continue;
                        }
                        ir::Instruction* dst_gep =
                            ctx_.builder->CreateGEP(ctx_.types.GetPointerType(elem_type), alloca,
                                                    ctx_.module->GetInt32Constant(0), idx);
                        if (dst_gep) {
                            ctx_.builder->CreateStore(load, dst_gep);
                        }
                    }
                    for (size_t i = copy_len; i < static_cast<size_t>(n); ++i) {
                        ir::Value* idx = ctx_.module->GetInt32Constant(static_cast<int64_t>(i));
                        ir::Instruction* dst_gep =
                            ctx_.builder->CreateGEP(ctx_.types.GetPointerType(elem_type), alloca,
                                                    ctx_.module->GetInt32Constant(0), idx);
                        if (dst_gep) {
                            ctx_.builder->CreateStore(ctx_.module->GetInt8Constant(0), dst_gep);
                        }
                    }
                }
            }
        }
    } else {
        ir::Instruction* alloca = CreateEntryBlockAlloca(elem_type);
        ctx_.RegisterVariable(var_def.ident, alloca);

        if (var_def.init_val && ctx_.builder->GetInsertBlock()) {
            auto* single = std::get_if<InitVal::SingleExp>(&var_def.init_val->value);
            if (single && single->get()) {
                (*single)->Accept(*this);
                ir::Value* val =
                    temp_value_ ? ConvertToTargetType(temp_value_, elem_type) : nullptr;
                if (val) {
                    ctx_.builder->CreateStore(val, alloca);
                }
            }
        }
    }
}

ir::Value* IRGenVisitor::EmitGlobalStringLiteral(const std::string& str) {
    std::vector<ir::Constant*> inits;
    for (unsigned char c : str) {
        inits.push_back(ctx_.module->GetInt8Constant(static_cast<int64_t>(c)));
    }
    inits.push_back(ctx_.module->GetInt8Constant(0));
    ir::Type* i8 = ctx_.types.GetI8Type();
    ir::ArrayType* arr_ty = ctx_.types.GetArrayType(i8, static_cast<unsigned>(inits.size()));
    ir::Constant* init = ctx_.module->CreateConstantArray(arr_ty, inits);
    std::string name = ".str." + std::to_string(printf_str_counter_++);
    ir::GlobalVar* gv =
        ctx_.module->CreateGlobalVar(name, ctx_.types.GetPointerType(arr_ty), init, true);
    return gv;
}

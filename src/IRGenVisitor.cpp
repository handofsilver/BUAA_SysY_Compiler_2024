/**
 * @file IRGenVisitor.cpp
 * @brief Core implementation of IRGenVisitor: constructor, Translate, scope/control-flow helpers,
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
    if (!current_function_ || !builder_) {
        return nullptr;
    }
    std::string label = (name == "entry") ? name : (name + "." + builder_->GetNextSSAName());
    auto block = std::make_unique<ir::BasicBlock>(label);
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

ir::Value* IRGenVisitor::CoerceToI1(ir::Value* cond_val) {
    if (!cond_val || !builder_->GetInsertBlock()) {
        return nullptr;
    }
    if (cond_val->GetType() && cond_val->GetType() == types_.GetI1Type()) {
        return cond_val;
    }
    cond_val = PromoteToI32(cond_val);
    ir::Instruction* cmp = builder_->CreateIcmp(types_.GetI1Type(), ir::IcmpPred::NE, cond_val,
                                                module_->GetInt32Constant(0));
    return cmp ? cmp : cond_val;
}

void IRGenVisitor::EmitShortCircuitAND(Exp* lhs, Exp* rhs) {
    ir::Instruction* result_slot = CreateEntryBlockAlloca(types_.GetI32Type());
    if (!result_slot || !builder_->GetInsertBlock()) {
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
    builder_->CreateCondBr(cond_i1, true_block, false_block);

    builder_->SetInsertPoint(false_block);
    builder_->CreateStore(module_->GetInt32Constant(0), result_slot);
    builder_->CreateBr(merge_block);

    builder_->SetInsertPoint(true_block);
    rhs->Accept(*this);
    ir::Value* rhs_val = PromoteToI32(temp_value_);
    if (!IsBlockTerminated()) {
        if (rhs_val) {
            builder_->CreateStore(rhs_val, result_slot);
        }
        builder_->CreateBr(merge_block);
    }

    builder_->SetInsertPoint(merge_block);
    ir::Instruction* load = builder_->CreateLoad(result_slot);
    temp_value_ = load ? load : result_slot;
}

void IRGenVisitor::EmitShortCircuitOR(Exp* lhs, Exp* rhs) {
    ir::Instruction* result_slot = CreateEntryBlockAlloca(types_.GetI32Type());
    if (!result_slot || !builder_->GetInsertBlock()) {
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
    builder_->CreateCondBr(cond_i1, true_block, rhs_block);

    builder_->SetInsertPoint(true_block);
    builder_->CreateStore(module_->GetInt32Constant(1), result_slot);
    builder_->CreateBr(merge_block);

    builder_->SetInsertPoint(rhs_block);
    rhs->Accept(*this);
    ir::Value* rhs_val = PromoteToI32(temp_value_);
    if (!IsBlockTerminated()) {
        if (rhs_val) {
            builder_->CreateStore(rhs_val, result_slot);
        }
        builder_->CreateBr(merge_block);
    }

    builder_->SetInsertPoint(merge_block);
    ir::Instruction* load = builder_->CreateLoad(result_slot);
    temp_value_ = load ? load : result_slot;
}

ir::Instruction* IRGenVisitor::CreateEntryBlockAlloca(ir::Type* type) {
    if (!current_function_ || !type || !builder_) {
        return nullptr;
    }
    const auto& blocks = current_function_->GetBlocks();
    if (blocks.empty()) {
        return nullptr;
    }
    ir::BasicBlock* entry = blocks.front().get();
    ir::Type* ptr_type = types_.GetPointerType(type); // alloca result is pointer to allocated type
    std::string name = builder_->GetNextSSAName();
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
    if (!v || !builder_->GetInsertBlock()) {
        return v;
    }
    ir::Type* ty = v->GetType();
    auto* int_ty = dynamic_cast<ir::IntegerType*>(ty);
    if (!int_ty || int_ty->GetBits() != 8) {
        return v;
    }
    ir::Instruction* z = builder_->CreateZext(v, types_.GetI32Type());
    return z ? z : v;
}

ir::Value* IRGenVisitor::ConvertToTargetType(ir::Value* v, ir::Type* target_ty) {
    if (!v || !target_ty || !builder_->GetInsertBlock()) {
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
        ir::Instruction* t = builder_->CreateTrunc(v, types_.GetI8Type());
        return t ? t : v;
    }
    if (target_bits == 32 && val_bits == 8) {
        ir::Instruction* z = builder_->CreateZext(v, types_.GetI32Type());
        return z ? z : v;
    }
    return v;
}

// -----------------------------------------------------------------------------
// Helpers for variable/constant definition
// -----------------------------------------------------------------------------

ir::Type* IRGenVisitor::GetCurDeclType() const {
    return (current_decl_btype_ == BType::CHAR) ? static_cast<ir::Type*>(types_.GetI8Type()) :
                                                  static_cast<ir::Type*>(types_.GetI32Type());
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
               static_cast<ir::ConstantInt*>(module_->GetInt8Constant(val)) :
               static_cast<ir::ConstantInt*>(module_->GetInt32Constant(val));
}

ir::ConstantArray* IRGenVisitor::BuildConstArrayInit(ir::ArrayType* arr_ty,
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
        ir::ArrayType* arr_ty = types_.GetArrayType(elem_type, static_cast<unsigned>(n));
        var_type = arr_ty;

        // Parse init: ConstInitVal is variant<SingleExp, ExpList, StringVal>; we need ExpList or
        // StringVal.
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
            values.push_back(0); // null terminator for string literal
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
            val = irgen::EvalConstInt((*single)->inner.get());
        }
        init = BuildConstScalarInit(val);
    }

    // Globals: in LLVM IR the name denotes the address, so type is always pointer (i32* or [N x
    // T]*).
    ir::Type* global_type = types_.GetPointerType(var_type);
    ir::GlobalVar* gv = module_->CreateGlobalVar(const_def.ident, global_type, init, true);
    RegisterVariable(const_def.ident, gv);
}

void IRGenVisitor::EmitLocalConstDef(ConstDef& const_def, ir::Type* elem_type) {
    const bool kIsArray = const_def.array_size.has_value() && const_def.array_size->get();

    if (kIsArray) {
        // Local const array: alloca [N x T], then GEP+Store for each element
        int n = EvalArraySizeFromConstExp(const_def.array_size->get());
        ir::ArrayType* arr_ty = types_.GetArrayType(elem_type, static_cast<unsigned>(n));
        ir::Instruction* alloca = CreateEntryBlockAlloca(arr_ty);
        RegisterVariable(const_def.ident, alloca);

        auto* list = std::get_if<ConstInitVal::ExpList>(&const_def.const_init_val->value);
        if (list && builder_->GetInsertBlock()) {
            for (size_t i = 0; i < list->size() && i < static_cast<size_t>(n); ++i) {
                int val = irgen::EvalConstInt((*list)[i]->inner.get());
                ir::Value* to_store = BuildConstScalarInit(val);
                ir::Value* idx = module_->GetInt32Constant(static_cast<int64_t>(i));
                ir::Instruction* gep = builder_->CreateGEP(types_.GetPointerType(elem_type), alloca,
                                                           module_->GetInt32Constant(0), idx);
                if (gep && to_store) {
                    builder_->CreateStore(to_store, gep);
                }
            }
        } else {
            auto* str = std::get_if<ConstInitVal::StringVal>(&const_def.const_init_val->value);
            if (str && builder_->GetInsertBlock()) {
                // String literal: emit global [len+1 x i8], then copy bytes into local alloca
                ir::Value* global_str = EmitGlobalStringLiteral(*str);
                ir::Type* i8 = types_.GetI8Type();
                size_t copy_len =
                    static_cast<size_t>(std::min(n, static_cast<int>(str->size()) + 1));
                for (size_t i = 0; i < copy_len; ++i) {
                    ir::Value* idx = module_->GetInt32Constant(static_cast<int64_t>(i));
                    ir::Instruction* src_gep = builder_->CreateGEP(
                        types_.GetPointerType(i8), global_str, module_->GetInt32Constant(0), idx);
                    if (!src_gep) {
                        continue;
                    }
                    ir::Instruction* load = builder_->CreateLoad(src_gep);
                    if (!load) {
                        continue;
                    }
                    ir::Instruction* dst_gep =
                        builder_->CreateGEP(types_.GetPointerType(elem_type), alloca,
                                            module_->GetInt32Constant(0), idx);
                    if (dst_gep) {
                        builder_->CreateStore(load, dst_gep);
                    }
                }
                for (size_t i = copy_len; i < static_cast<size_t>(n); ++i) {
                    ir::Value* idx = module_->GetInt32Constant(static_cast<int64_t>(i));
                    ir::Instruction* dst_gep =
                        builder_->CreateGEP(types_.GetPointerType(elem_type), alloca,
                                            module_->GetInt32Constant(0), idx);
                    if (dst_gep) {
                        builder_->CreateStore(module_->GetInt8Constant(0), dst_gep);
                    }
                }
            }
        }
    } else {
        // Local const scalar: alloca T, Store constant
        ir::Instruction* alloca = CreateEntryBlockAlloca(elem_type);
        RegisterVariable(const_def.ident, alloca);

        int val = 0;
        auto* single = std::get_if<ConstInitVal::SingleExp>(&const_def.const_init_val->value);
        if (single && single->get() && builder_->GetInsertBlock()) {
            val = irgen::EvalConstInt((*single)->inner.get());
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
        ir::ArrayType* arr_ty = types_.GetArrayType(elem_type, static_cast<unsigned>(n));
        var_type = arr_ty;

        // VarDef may have no init_val (e.g. int a[3];); if present, parse ExpList and eval each.
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
        // --- Global var scalar: T, optional ConstantInt init ---
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

    // In LLVM IR the global name denotes the address, so type is always pointer (i32* or [N x T]*).
    ir::Type* global_type = types_.GetPointerType(var_type);
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
        ir::ArrayType* arr_ty = types_.GetArrayType(elem_type, static_cast<unsigned>(n));
        ir::Instruction* alloca = CreateEntryBlockAlloca(arr_ty);
        RegisterVariable(var_def.ident, alloca);

        // If init present: visit each Exp in the list (runtime eval), or copy string literal.
        if (var_def.init_val && builder_->GetInsertBlock()) {
            auto* list = std::get_if<InitVal::ExpList>(&var_def.init_val->value);
            if (list) {
                for (size_t i = 0; i < list->size() && i < static_cast<size_t>(n); ++i) {
                    (*list)[i]->Accept(*this);
                    ir::Value* val = temp_value_;
                    if (val) {
                        val = ConvertToTargetType(val, elem_type);
                        ir::Value* idx = module_->GetInt32Constant(static_cast<int64_t>(i));
                        ir::Instruction* gep =
                            builder_->CreateGEP(types_.GetPointerType(elem_type), alloca,
                                                module_->GetInt32Constant(0), idx);
                        if (gep) {
                            builder_->CreateStore(val, gep);
                        }
                    }
                }
            } else {
                auto* str = std::get_if<InitVal::StringVal>(&var_def.init_val->value);
                if (str) {
                    // String literal: emit global [len+1 x i8], then copy bytes into local alloca
                    ir::Value* global_str = EmitGlobalStringLiteral(*str);
                    ir::Type* i8 = types_.GetI8Type();
                    size_t copy_len =
                        static_cast<size_t>(std::min(n, static_cast<int>(str->size()) + 1));
                    for (size_t i = 0; i < copy_len; ++i) {
                        ir::Value* idx = module_->GetInt32Constant(static_cast<int64_t>(i));
                        ir::Instruction* src_gep =
                            builder_->CreateGEP(types_.GetPointerType(i8), global_str,
                                                module_->GetInt32Constant(0), idx);
                        if (!src_gep) {
                            continue;
                        }
                        ir::Instruction* load = builder_->CreateLoad(src_gep);
                        if (!load) {
                            continue;
                        }
                        ir::Instruction* dst_gep =
                            builder_->CreateGEP(types_.GetPointerType(elem_type), alloca,
                                                module_->GetInt32Constant(0), idx);
                        if (dst_gep) {
                            builder_->CreateStore(load, dst_gep);
                        }
                    }
                    for (size_t i = copy_len; i < static_cast<size_t>(n); ++i) {
                        ir::Value* idx = module_->GetInt32Constant(static_cast<int64_t>(i));
                        ir::Instruction* dst_gep =
                            builder_->CreateGEP(types_.GetPointerType(elem_type), alloca,
                                                module_->GetInt32Constant(0), idx);
                        if (dst_gep) {
                            builder_->CreateStore(module_->GetInt8Constant(0), dst_gep);
                        }
                    }
                }
            }
        }
    } else {
        // --- Local var scalar: alloca T, optional Store(exp result) ---
        ir::Instruction* alloca = CreateEntryBlockAlloca(elem_type);
        RegisterVariable(var_def.ident, alloca);

        // If init present: visit the single Exp, convert to elem_type, then Store.
        if (var_def.init_val && builder_->GetInsertBlock()) {
            auto* single = std::get_if<InitVal::SingleExp>(&var_def.init_val->value);
            if (single && single->get()) {
                (*single)->Accept(*this);
                ir::Value* val =
                    temp_value_ ? ConvertToTargetType(temp_value_, elem_type) : nullptr;
                if (val) {
                    builder_->CreateStore(val, alloca);
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
    ir::Type* i8 = types_.GetI8Type();
    ir::ArrayType* arr_ty = types_.GetArrayType(i8, static_cast<unsigned>(inits.size()));
    ir::Constant* init = module_->CreateConstantArray(arr_ty, inits);
    std::string name = ".str." + std::to_string(printf_str_counter_++);
    ir::GlobalVar* gv = module_->CreateGlobalVar(name, types_.GetPointerType(arr_ty), init, true);
    return gv;
}

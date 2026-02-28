/**
 * @file IRDeclEmitter.cpp
 * @brief Implementation of IRDeclEmitter: global/local variable emission and array init.
 */
#include "irgen/IRDeclEmitter.h"
#include "ir/Constant.h"
#include "ir/Instruction.h"
#include <algorithm>

IRDeclEmitter::IRDeclEmitter(IRGenContext& ctx) : ctx_(ctx) {}

// -----------------------------------------------------------------------------
// Private helpers
// -----------------------------------------------------------------------------

ir::Instruction* IRDeclEmitter::CreateEntryBlockAlloca(ir::Type* type) {
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

ir::ConstantInt* IRDeclEmitter::MakeConstInt(ir::Type* elem_type, int val) const {
    auto* int_ty = dynamic_cast<ir::IntegerType*>(elem_type);
    if (int_ty && int_ty->GetBits() == 8) {
        return ctx_.module->GetInt8Constant(val);
    }
    return ctx_.module->GetInt32Constant(val);
}

ir::ConstantArray* IRDeclEmitter::BuildConstArrayInit(ir::ArrayType* arr_ty, ir::Type* elem_type,
                                                      const std::vector<int>& values) const {
    std::vector<ir::Constant*> inits;
    inits.reserve(values.size());
    for (int v : values) {
        inits.push_back(MakeConstInt(elem_type, v));
    }
    return ctx_.module->CreateConstantArray(arr_ty, inits);
}

// -----------------------------------------------------------------------------
// Global definitions
// -----------------------------------------------------------------------------

void IRDeclEmitter::EmitGlobal(const std::string& name, ir::Type* elem_type, int size,
                               const std::vector<int>& init_vals, bool is_const) {
    bool is_array = (size > 0);
    ir::Type* var_type = nullptr;
    ir::Constant* init = nullptr;

    if (is_array) {
        auto* arr_ty = ctx_.types.GetArrayType(elem_type, static_cast<unsigned>(size));
        var_type = arr_ty;
        if (!init_vals.empty()) {
            std::vector<int> padded = init_vals;
            while (static_cast<int>(padded.size()) < size) {
                padded.push_back(0);
            }
            init = BuildConstArrayInit(arr_ty, elem_type, padded);
        }
    } else {
        var_type = elem_type;
        if (!init_vals.empty()) {
            init = MakeConstInt(elem_type, init_vals[0]);
        }
    }

    ir::Type* global_type = ctx_.types.GetPointerType(var_type);
    ir::GlobalVar* gv = ctx_.module->CreateGlobalVar(name, global_type, init, is_const);
    ctx_.RegisterVariable(name, gv);
}

ir::Value* IRDeclEmitter::EmitGlobalStringLiteral(const std::string& str) {
    std::vector<ir::Constant*> inits;
    inits.reserve(str.size() + 1);
    for (unsigned char c : str) {
        inits.push_back(ctx_.module->GetInt8Constant(static_cast<int64_t>(c)));
    }
    inits.push_back(ctx_.module->GetInt8Constant(0));

    ir::Type* i8 = ctx_.types.GetI8Type();
    ir::ArrayType* arr_ty = ctx_.types.GetArrayType(i8, static_cast<unsigned>(inits.size()));
    ir::Constant* init = ctx_.module->CreateConstantArray(arr_ty, inits);
    std::string gv_name = ".str." + std::to_string(str_literal_counter_++);
    return ctx_.module->CreateGlobalVar(gv_name, ctx_.types.GetPointerType(arr_ty), init, true);
}

// -----------------------------------------------------------------------------
// Local definitions
// -----------------------------------------------------------------------------

ir::Value* IRDeclEmitter::EmitLocalAlloca(const std::string& /*name*/, ir::Type* elem_type,
                                          int size) {
    ir::Type* alloca_type = (size > 0) ? static_cast<ir::Type*>(ctx_.types.GetArrayType(
                                             elem_type, static_cast<unsigned>(size))) :
                                         elem_type;
    return CreateEntryBlockAlloca(alloca_type);
}

void IRDeclEmitter::EmitLocalArrayInit(ir::Value* alloca_ptr, ir::Type* elem_type, int size,
                                       const std::vector<ir::Value*>& init_vals) {
    if (!ctx_.builder->GetInsertBlock()) {
        return;
    }
    ir::Type* ptr_type = ctx_.types.GetPointerType(elem_type);
    ir::Value* zero = ctx_.module->GetInt32Constant(0);

    // Store provided init values via GEP + Store.
    for (size_t i = 0; i < init_vals.size() && static_cast<int>(i) < size; ++i) {
        ir::Value* val = init_vals[i];
        if (!val) {
            continue;
        }
        ir::Value* idx = ctx_.module->GetInt32Constant(static_cast<int64_t>(i));
        ir::Instruction* gep = ctx_.builder->CreateGEP(ptr_type, alloca_ptr, zero, idx);
        if (gep) {
            ctx_.builder->CreateStore(val, gep);
        }
    }

    // Zero-pad remaining elements.
    for (size_t i = init_vals.size(); i < static_cast<size_t>(size); ++i) {
        ir::Value* idx = ctx_.module->GetInt32Constant(static_cast<int64_t>(i));
        ir::Instruction* gep = ctx_.builder->CreateGEP(ptr_type, alloca_ptr, zero, idx);
        if (gep) {
            ctx_.builder->CreateStore(MakeConstInt(elem_type, 0), gep);
        }
    }
}

void IRDeclEmitter::EmitLocalStringInit(ir::Value* alloca_ptr, ir::Type* elem_type, int size,
                                        const std::string& str) {
    if (!ctx_.builder->GetInsertBlock()) {
        return;
    }
    ir::Value* global_str = EmitGlobalStringLiteral(str);
    ir::Type* i8 = ctx_.types.GetI8Type();
    ir::Type* i8_ptr = ctx_.types.GetPointerType(i8);
    ir::Type* elem_ptr = ctx_.types.GetPointerType(elem_type);
    ir::Value* zero = ctx_.module->GetInt32Constant(0);

    // Copy string bytes (including null terminator) from global to local.
    size_t copy_len = static_cast<size_t>(std::min(size, static_cast<int>(str.size()) + 1));
    for (size_t i = 0; i < copy_len; ++i) {
        ir::Value* idx = ctx_.module->GetInt32Constant(static_cast<int64_t>(i));
        ir::Instruction* src_gep = ctx_.builder->CreateGEP(i8_ptr, global_str, zero, idx);
        if (!src_gep) {
            continue;
        }
        ir::Instruction* load = ctx_.builder->CreateLoad(src_gep);
        if (!load) {
            continue;
        }
        ir::Instruction* dst_gep = ctx_.builder->CreateGEP(elem_ptr, alloca_ptr, zero, idx);
        if (dst_gep) {
            ctx_.builder->CreateStore(load, dst_gep);
        }
    }

    // Zero-pad remaining elements.
    for (size_t i = copy_len; i < static_cast<size_t>(size); ++i) {
        ir::Value* idx = ctx_.module->GetInt32Constant(static_cast<int64_t>(i));
        ir::Instruction* dst_gep = ctx_.builder->CreateGEP(elem_ptr, alloca_ptr, zero, idx);
        if (dst_gep) {
            ctx_.builder->CreateStore(ctx_.module->GetInt8Constant(0), dst_gep);
        }
    }
}

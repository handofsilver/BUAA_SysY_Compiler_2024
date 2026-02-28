/**
 * @file IRDeclEmitter.h
 * @brief Low-level emitter for variable/constant declarations and memory allocation.
 *
 * Encapsulates entry-block alloca, global variable creation, and array initialization
 * (GEP + Store loops, zero-padding). Depends only on IRGenContext and IR classes;
 * MUST NOT include any AST headers.
 */
#pragma once

#include "ir/Type.h"
#include "irgen/IRGenContext.h"
#include <string>
#include <vector>

namespace ir {
    class Value;
    class Instruction;
    class ConstantInt;
    class ConstantArray;
    class ArrayType;
} // namespace ir

class IRDeclEmitter {
public:
    explicit IRDeclEmitter(IRGenContext& ctx);

    // -------------------------------------------------------------------------
    // 1. Global definitions (compile-time evaluated)
    // -------------------------------------------------------------------------

    /**
     * @brief Create a global variable or constant.
     * @param name      Source-level identifier (becomes @name in IR).
     * @param elem_type Element type (i32 or i8).
     * @param size      0 → scalar, >0 → one-dimensional array of that length.
     * @param init_vals Compile-time integer values. Empty → zeroinitializer;
     *                  non-empty → explicit initializer (zero-padded to \p size for arrays).
     * @param is_const  True for const globals (emits `constant` instead of `global`).
     *
     * After creation the global is registered in the symbol table via ctx_.
     */
    void EmitGlobal(const std::string& name, ir::Type* elem_type, int size,
                    const std::vector<int>& init_vals, bool is_const);

    /**
     * @brief Create a global constant string literal (null-terminated i8 array).
     * Used by printf lowering and local string initialization.
     * @return Pointer Value* to the global (usable as putstr argument).
     */
    ir::Value* EmitGlobalStringLiteral(const std::string& str);

    // -------------------------------------------------------------------------
    // 2. Local definitions (runtime evaluated)
    // -------------------------------------------------------------------------

    /**
     * @brief Allocate an entry-block alloca for a local variable.
     * @param name      Source-level identifier (reserved for debug info; not used as SSA name).
     * @param elem_type Element type (i32 or i8).
     * @param size      0 → scalar alloca, >0 → [size x elem_type] array alloca.
     * @return The alloca pointer; caller registers it in the symbol table.
     */
    ir::Value* EmitLocalAlloca(const std::string& name, ir::Type* elem_type, int size);

    /**
     * @brief Initialize a local array via GEP + Store for each provided value,
     *        then zero-pad remaining elements up to \p size.
     */
    void EmitLocalArrayInit(ir::Value* alloca_ptr, ir::Type* elem_type, int size,
                            const std::vector<ir::Value*>& init_vals);

    /**
     * @brief Initialize a local array from a string literal: creates a global constant
     *        string, then element-wise load/store into the alloca; zero-pads the tail.
     */
    void EmitLocalStringInit(ir::Value* alloca_ptr, ir::Type* elem_type, int size,
                             const std::string& str);

private:
    IRGenContext& ctx_;
    int str_literal_counter_ = 0;

    /** Create an alloca in the entry block of the current function. */
    ir::Instruction* CreateEntryBlockAlloca(ir::Type* type);

    /** Create a ConstantInt of the appropriate width (i8 or i32) from elem_type. */
    ir::ConstantInt* MakeConstInt(ir::Type* elem_type, int val) const;

    /** Build a ConstantArray initializer from integer values, using elem_type for width. */
    ir::ConstantArray* BuildConstArrayInit(ir::ArrayType* arr_ty, ir::Type* elem_type,
                                           const std::vector<int>& values) const;
};

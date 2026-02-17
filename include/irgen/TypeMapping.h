/**
 * @file TypeMapping.h
 * @brief Pure mapping from AST BType/OpType to IR types and predicates.
 * No visitor state; used by IRGenVisitor.
 */
#pragma once

#include "AST.h"
#include "ir/Instruction.h"
#include "ir/Module.h"
#include "ir/Type.h"
#include <optional>

namespace irgen {

    /** Map comparison OpType to LLVM icmp predicate (for binary expressions). */
    std::optional<ir::IcmpPred> OpTypeToIcmpPred(OpType op);

    /** Map arithmetic OpType to IR BinaryOp (ADD/SUB/MUL/DIV/MOD). Others return nullopt. */
    std::optional<ir::BinaryOp> OpTypeToBinaryOp(OpType op);

    /** True if OpType is arithmetic (add/sub/mul/div/mod). */
    bool IsArithmeticOp(OpType op);

    /**
     * Map BType to IR type for function return (FuncType: void | int | char).
     * Caller must have Module* to get i32/i8/void.
     */
    ir::Type* BTypeToReturnType(BType btype, ir::Module* module);

    /**
     * Map FuncFParam (BType + is_array) to IR parameter type.
     * Scalar: i32 or i8; array: i32* or i8* (pointer to first element).
     */
    ir::Type* BTypeToParamType(BType btype, bool is_array, ir::Module* module);

    /** Allocated type for a scalar parameter (for alloca): i32 or i8. */
    ir::Type* BTypeToAllocaType(BType btype, ir::Module* module);

} // namespace irgen

/**
 * @file TypeMapping.h
 * @brief Pure mapping from AST BType/OpType to IR types and predicates.
 * Stateless utilities; types obtained from TypeManager singleton.
 */
#pragma once

#include "AST.h"
#include "ir/Instruction.h"
#include "ir/Type.h"
#include <optional>

namespace irgen {

    /** Map comparison OpType to LLVM icmp predicate (for binary expressions). */
    std::optional<ir::IcmpPred> OpTypeToIcmpPred(OpType op);

    /** Map arithmetic OpType to IR BinaryOp (ADD/SUB/MUL/DIV/MOD). Others return nullopt. */
    std::optional<ir::BinaryOp> OpTypeToBinaryOp(OpType op);

    /** True if OpType is arithmetic (add/sub/mul/div/mod). */
    bool IsArithmeticOp(OpType op);

    /** Map BType to IR type for function return (FuncType: void | int | char). */
    ir::Type* BTypeToReturnType(BType btype);

    /**
     * Map FuncFParam (BType + is_array) to IR parameter type.
     * Scalar: i32 or i8; array: i32* or i8* (pointer to first element).
     */
    ir::Type* BTypeToParamType(BType btype, bool is_array);

    /** Allocated type for a scalar parameter (for alloca): i32 or i8. */
    ir::Type* BTypeToAllocaType(BType btype);

} // namespace irgen

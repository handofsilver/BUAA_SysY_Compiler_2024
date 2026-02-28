/**
 * @file TypeMapping.cpp
 * @brief Implementation of BType/OpType to IR type mapping.
 */
#include "irgen/TypeMapping.h"

#include "ir/TypeManager.h"

namespace irgen {

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

    std::optional<ir::BinaryOp> OpTypeToBinaryOp(OpType op) {
        switch (op) {
            case OpType::ADD: return ir::BinaryOp::ADD;
            case OpType::SUB: return ir::BinaryOp::SUB;
            case OpType::MUL: return ir::BinaryOp::MUL;
            case OpType::DIV: return ir::BinaryOp::DIV;
            case OpType::MOD: return ir::BinaryOp::REM;
            default: return std::nullopt;
        }
    }

    bool IsArithmeticOp(OpType op) {
        return op == OpType::ADD || op == OpType::SUB || op == OpType::MUL || op == OpType::DIV ||
               op == OpType::MOD;
    }

    ir::Type* BTypeToReturnType(BType btype) {
        auto& tm = ir::TypeManager::Get();
        switch (btype) {
            case BType::VOID: return tm.GetVoidType();
            case BType::INT: return tm.GetI32Type();
            case BType::CHAR: return tm.GetI8Type();
            default: return tm.GetI32Type();
        }
    }

    ir::Type* BTypeToParamType(BType btype, bool is_array) {
        auto& tm = ir::TypeManager::Get();
        ir::Type* elem = (btype == BType::CHAR) ? static_cast<ir::Type*>(tm.GetI8Type()) :
                                                  static_cast<ir::Type*>(tm.GetI32Type());
        return is_array ? static_cast<ir::Type*>(tm.GetPointerType(elem)) : elem;
    }

    ir::Type* BTypeToAllocaType(BType btype) {
        auto& tm = ir::TypeManager::Get();
        return (btype == BType::CHAR) ? static_cast<ir::Type*>(tm.GetI8Type()) :
                                        static_cast<ir::Type*>(tm.GetI32Type());
    }

} // namespace irgen

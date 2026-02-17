/**
 * @file TypeMapping.cpp
 * @brief Implementation of BType/OpType to IR type mapping.
 */
#include "irgen/TypeMapping.h"

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

    ir::Type* BTypeToReturnType(BType btype, ir::Module* module) {
        switch (btype) {
            case BType::VOID: return module->GetVoidType();
            case BType::INT: return module->GetI32Type();
            case BType::CHAR: return module->GetI8Type();
            default: return module->GetI32Type();
        }
    }

    ir::Type* BTypeToParamType(BType btype, bool is_array, ir::Module* module) {
        ir::Type* elem = (btype == BType::CHAR) ? static_cast<ir::Type*>(module->GetI8Type()) :
                                                  static_cast<ir::Type*>(module->GetI32Type());
        return is_array ? static_cast<ir::Type*>(module->GetPointerType(elem)) : elem;
    }

    ir::Type* BTypeToAllocaType(BType btype, ir::Module* module) {
        return (btype == BType::CHAR) ? static_cast<ir::Type*>(module->GetI8Type()) :
                                        static_cast<ir::Type*>(module->GetI32Type());
    }

} // namespace irgen

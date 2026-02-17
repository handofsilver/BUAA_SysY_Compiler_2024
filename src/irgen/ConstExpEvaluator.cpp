/**
 * @file ConstExpEvaluator.cpp
 * @brief Implementation of constant expression evaluation.
 */
#include "irgen/ConstExpEvaluator.h"
#include "AST.h"

namespace irgen {

    int EvalConstInt(Exp* exp) {
        if (!exp) {
            return 0;
        }
        if (auto* num = dynamic_cast<Number*>(exp)) {
            return num->int_const;
        }
        if (auto* ch = dynamic_cast<Character*>(exp)) {
            return static_cast<int>(ch->char_const);
        }
        if (auto* cexp = dynamic_cast<ConstExp*>(exp)) {
            return EvalConstInt(cexp->inner.get());
        }
        if (auto* uexp = dynamic_cast<UnaryExp*>(exp)) {
            int v = EvalConstInt(uexp->operand.get());
            if (uexp->op == OpType::PLUS) {
                return v;
            }
            if (uexp->op == OpType::MINU) {
                return -v;
            }
            return 0;
        }
        if (auto* bexp = dynamic_cast<BinaryExp*>(exp)) {
            int l = EvalConstInt(bexp->lhs.get());
            int r = EvalConstInt(bexp->rhs.get());
            switch (bexp->op) {
                case OpType::ADD: return l + r;
                case OpType::SUB: return l - r;
                case OpType::MUL: return l * r;
                case OpType::DIV: return (r == 0) ? 0 : (l / r);
                case OpType::MOD: return (r == 0) ? 0 : (l % r);
                default: return 0;
            }
        }
        return 0;
    }

} // namespace irgen

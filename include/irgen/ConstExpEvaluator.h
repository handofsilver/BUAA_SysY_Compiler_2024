/**
 * @file ConstExpEvaluator.h
 * @brief Compile-time evaluation of constant integer expressions (for global init, array size).
 * Pure AST traversal; no IR or visitor state.
 */
#pragma once

class Exp;

namespace irgen {

    /**
     * Evaluate a constant integer expression at compile time.
     * Supports: Number, ConstExp (via inner), UnaryExp (+/-), BinaryExp (+ - * / %).
     * Unsupported cases (e.g. LVal, function call) return 0.
     */
    int EvalConstInt(Exp* exp);

} // namespace irgen

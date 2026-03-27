/**
 * @file ConstFoldLVN.cpp
 * @brief Constant folding + local value numbering implementation.
 */

#include "pass/ConstFoldLVN.h"

#include "ir/BasicBlock.h"
#include "ir/Constant.h"
#include "ir/Function.h"
#include "ir/Instruction.h"
#include "ir/Module.h"
#include "ir/Type.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <unordered_map>

namespace pass {

    namespace {

        struct BinaryKey {
            ir::BinaryOp op;
            ir::Value* lhs;
            ir::Value* rhs;

            bool operator==(const BinaryKey& other) const {
                return op == other.op && lhs == other.lhs && rhs == other.rhs;
            }
        };

        struct BinaryKeyHash {
            size_t operator()(const BinaryKey& key) const {
                size_t h = std::hash<int>{}(static_cast<int>(key.op));
                h ^= std::hash<void*>{}(key.lhs) + 0x9e3779b9 + (h << 6) + (h >> 2);
                h ^= std::hash<void*>{}(key.rhs) + 0x9e3779b9 + (h << 6) + (h >> 2);
                return h;
            }
        };

        bool IsConstInt(ir::Value* v, int64_t& out) {
            if (auto* c = dynamic_cast<ir::ConstantInt*>(v)) {
                out = c->GetValue();
                return true;
            }
            return false;
        }

        ir::ConstantInt* GetConstForType(ir::Module& module, ir::Type* ty, int64_t value) {
            if (auto* int_ty = dynamic_cast<ir::IntegerType*>(ty)) {
                if (int_ty->GetBits() <= 8) {
                    return module.GetInt8Constant(value);
                }
            }
            return module.GetInt32Constant(value);
        }

        bool IsCommutative(ir::BinaryOp op) {
            return op == ir::BinaryOp::ADD || op == ir::BinaryOp::MUL;
        }

        BinaryKey MakeCanonicalBinaryKey(ir::BinaryInst* bin) {
            ir::Value* lhs = bin->GetLhs();
            ir::Value* rhs = bin->GetRhs();
            ir::BinaryOp op = bin->GetOp();
            if (IsCommutative(op) && rhs < lhs) {
                std::swap(lhs, rhs);
            }
            return BinaryKey{op, lhs, rhs};
        }

        ir::Value* TryFoldBinary(ir::BinaryInst* bin, ir::Module& module) {
            ir::Value* lhs = bin->GetLhs();
            ir::Value* rhs = bin->GetRhs();
            ir::Type* result_ty = bin->GetType();
            if (!lhs || !rhs || !result_ty) {
                return nullptr;
            }

            ir::BinaryOp op = bin->GetOp();
            int64_t lval = 0;
            int64_t rval = 0;
            bool is_lhs_const = IsConstInt(lhs, lval);
            bool is_rhs_const = IsConstInt(rhs, rval);

            // Constant-expression folding.
            if (is_lhs_const && is_rhs_const) {
                switch (op) {
                    case ir::BinaryOp::ADD: return GetConstForType(module, result_ty, lval + rval);
                    case ir::BinaryOp::SUB: return GetConstForType(module, result_ty, lval - rval);
                    case ir::BinaryOp::MUL: return GetConstForType(module, result_ty, lval * rval);
                    case ir::BinaryOp::DIV:
                        if (rval != 0) {
                            return GetConstForType(module, result_ty, lval / rval);
                        }
                        return nullptr;
                    case ir::BinaryOp::REM:
                        if (rval != 0) {
                            return GetConstForType(module, result_ty, lval % rval);
                        }
                        return nullptr;
                    default: return nullptr;
                }
            }

            // Algebraic simplification.
            if (is_rhs_const) {
                if ((op == ir::BinaryOp::ADD || op == ir::BinaryOp::SUB) && rval == 0) {
                    return lhs;
                }
                switch (op) {
                    case ir::BinaryOp::MUL:
                        if (rval == 0) {
                            return GetConstForType(module, result_ty, 0);
                        }
                        if (rval == 1) {
                            return lhs;
                        }
                        break;
                    case ir::BinaryOp::DIV:
                        if (rval == 1) {
                            return lhs;
                        }
                        break;
                    case ir::BinaryOp::REM:
                        if (rval == 1) {
                            return GetConstForType(module, result_ty, 0);
                        }
                        break;
                    default: break;
                }
            }

            if (is_lhs_const) {
                switch (op) {
                    case ir::BinaryOp::ADD:
                        if (lval == 0) {
                            return rhs;
                        }
                        break;
                    case ir::BinaryOp::MUL:
                        if (lval == 0) {
                            return GetConstForType(module, result_ty, 0);
                        }
                        if (lval == 1) {
                            return rhs;
                        }
                        break;
                    default: break;
                }
            }

            if (lhs == rhs) {
                switch (op) {
                    case ir::BinaryOp::SUB: return GetConstForType(module, result_ty, 0);
                    default: break;
                }
            }
            return nullptr;
        }

        bool RunOnBlock(ir::BasicBlock& bb, ir::Module& module) {
            bool changed = false;
            std::unordered_map<BinaryKey, ir::Value*, BinaryKeyHash> lvn_table;

            for (auto& inst_uptr : bb.GetInstructions()) {
                auto* bin = dynamic_cast<ir::BinaryInst*>(inst_uptr.get());
                if (!bin) {
                    continue;
                }

                // Step 1: fold/simplify first.
                if (ir::Value* replacement = TryFoldBinary(bin, module)) {
                    if (replacement != bin) {
                        // Only count as a change when this substitution actually rewrites
                        // at least one use edge. Otherwise fixed-point iteration would keep
                        // reporting changes on already-dead instructions.
                        bool had_uses = !bin->GetUseList().empty();
                        bin->ReplaceAllUsesWith(replacement);
                        changed |= had_uses;
                    }
                    continue;
                }

                // Step 2: local value numbering (block-local CSE).
                BinaryKey key = MakeCanonicalBinaryKey(bin);
                auto it = lvn_table.find(key);
                if (it != lvn_table.end()) {
                    bool had_uses = !bin->GetUseList().empty();
                    bin->ReplaceAllUsesWith(it->second);
                    changed |= had_uses;
                    continue;
                }
                lvn_table.emplace(key, bin);
            }

            return changed;
        }

    } // namespace

    bool ConstFoldLVNPass::Run(ir::Function& func) {
        bool changed = false;
        for (auto& bb_uptr : func.GetBlocks()) {
            changed |= RunOnBlock(*bb_uptr, module_);
        }
        return changed;
    }

} // namespace pass

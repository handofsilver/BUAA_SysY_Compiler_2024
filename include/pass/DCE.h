/**
 * @file DCE.h
 * @brief Dead Code Elimination: removes instructions whose results are never
 *        consumed by any side-effectful operation.
 *
 * Algorithm (mark-and-sweep on SSA def-use chains):
 *   1. Mark roots: ReturnInst, StoreInst, CallInst, BranchInst.
 *   2. BFS backward through operands to mark all transitively live instructions.
 *   3. Erase every unmarked instruction (detach operands first, then erase).
 *
 * The pass does NOT remove unreachable basic blocks; that is a separate concern.
 * Designed to run once after ConstFoldLVNPass has reached its fixed point.
 */
#pragma once

#include "pass/Pass.h"

#include <string_view>

namespace ir {
    class Function;
}

namespace pass {

    class DCEPass : public FunctionPass {
    public:
        bool Run(ir::Function& func) override;
        std::string_view GetName() const override {
            return "DCE";
        }
    };

} // namespace pass

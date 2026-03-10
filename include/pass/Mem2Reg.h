/**
 * @file Mem2Reg.h
 * @brief Mem2Reg: promote alloca/load/store to SSA phi nodes.
 *
 * Transforms "pseudo-SSA" IR (alloca + store + load pattern) into true SSA form
 * by replacing promotable scalar allocas with phi nodes at dominance frontiers.
 *
 * Pipeline per function:
 *   1. Build CFG (predecessor/successor maps)
 *   2. Build Dominator Tree (Cooper algorithm)
 *   3. Compute Dominance Frontier
 *   4. Collect promotable allocas (scalar, only load/store uses)
 *   5. Insert phi nodes at dominance frontiers (iterative)
 *   6. SSA rename pass (DFS over dominator tree)
 *   7. Erase dead alloca / load / store instructions
 */
#pragma once

#include "pass/Pass.h"

namespace ir {
    class Module;
}

namespace pass {

    class Mem2RegPass : public FunctionPass {
    public:
        bool Run(ir::Function& func) override;

        /** @brief Run with module; when given, use-before-def loads are replaced with constant 0. */
        bool Run(ir::Function& func, ir::Module* module);

        std::string_view GetName() const override {
            return "mem2reg";
        }
    };

} // namespace pass

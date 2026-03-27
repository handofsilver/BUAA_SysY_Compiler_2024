/**
 * @file ConstFoldLVN.h
 * @brief Function-level IR optimization pass: constant folding + local value numbering.
 *
 * This pass works on SSA IR after Mem2Reg:
 *   1) Fold constant binary expressions to ConstantInt.
 *   2) Apply basic algebraic simplifications (e.g. x+0=x, x*1=x, x*0=0).
 *   3) Eliminate block-local common binary expressions via LVN (RAUW).
 *
 * Notes:
 * - The pass only performs substitutions (RAUW). It does not erase instructions.
 * - Dead instructions exposed by this pass are expected to be cleaned by DCE later.
 */
#pragma once

#include "pass/Pass.h"

#include <string_view>

namespace ir {
    class Function;
    class Module;
} // namespace ir

namespace pass {

    class ConstFoldLVNPass : public FunctionPass {
    public:
        explicit ConstFoldLVNPass(ir::Module& module) : module_(module) {}
        ~ConstFoldLVNPass() override = default;

        bool Run(ir::Function& func) override;
        std::string_view GetName() const override {
            return "ConstFoldLVN";
        }

    private:
        ir::Module& module_;
    };

} // namespace pass

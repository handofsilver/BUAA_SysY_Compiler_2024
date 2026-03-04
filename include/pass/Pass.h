/**
 * @file Pass.h
 * @brief Base class for function-level optimization passes.
 *
 * All optimization passes (Mem2Reg, DCE, etc.) implement FunctionPass and
 * are driven by PassManager.
 */
#pragma once

#include <string_view>

namespace ir {
    class Function;
}

namespace pass {

    /**
     * @brief Interface for a pass that transforms a single Function.
     *
     * Ownership: PassManager owns FunctionPass objects via unique_ptr.
     */
    class FunctionPass {
    public:
        virtual ~FunctionPass() = default;

        /**
         * @brief Run the pass on a function.
         * @return true if the IR was modified, false otherwise.
         */
        virtual bool Run(ir::Function& func) = 0;

        virtual std::string_view GetName() const = 0;
    };

} // namespace pass

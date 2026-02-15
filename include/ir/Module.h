/**
 * @file Module.h
 * @brief Module: top-level container for global variables and functions.
 *
 * The Module owns all GlobalVars and Functions. It is the root of the
 * IR ownership tree.
 */

#ifndef IR_MODULE_H
#define IR_MODULE_H

#include "ir/Function.h"
#include "ir/GlobalVar.h"

#include <memory>
#include <vector>

namespace ir {

    /**
     * @brief Top-level IR container: globals and functions.
     *
     * Ownership: Module owns all GlobalVar and Function objects
     * (vectors of unique_ptr). No one else owns these.
     */
    class Module {
    public:
        Module() = default;

        /** @brief Get global variables (ownership held here). */
        std::vector<std::unique_ptr<GlobalVar>>& GetGlobalVars() {
            return global_vars_;
        }
        const std::vector<std::unique_ptr<GlobalVar>>& GetGlobalVars() const {
            return global_vars_;
        }

        /** @brief Get functions (ownership held here). */
        std::vector<std::unique_ptr<Function>>& GetFunctions() {
            return functions_;
        }
        const std::vector<std::unique_ptr<Function>>& GetFunctions() const {
            return functions_;
        }

    private:
        std::vector<std::unique_ptr<GlobalVar>> global_vars_;
        std::vector<std::unique_ptr<Function>> functions_;
    };

} // namespace ir

#endif // IR_MODULE_H

/**
 * @file Module.h
 * @brief Module: top-level container for global variables and functions.
 *
 * The Module owns all GlobalVars and Functions. It is the root of the
 * IR ownership tree.
 */

#ifndef IR_MODULE_H
#define IR_MODULE_H

#include "ir/Constant.h"
#include "ir/Function.h"
#include "ir/GlobalVar.h"
#include "ir/Type.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace ir {

    /**
     * @brief Top-level IR container: globals, functions, and constant pool.
     *
     * Ownership: Module owns all GlobalVar, Function, and ConstantInt objects.
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

        /** @brief Common types (owned here). */
        IntegerType* GetI32Type();
        IntegerType* GetI8Type();
        IntegerType* GetI1Type();

        /**
         * @brief Get or create an i32 constant. Returned pointer is valid for module lifetime.
         */
        ConstantInt* GetInt32Constant(int64_t value);

        /** Out-of-line destructor so TUs that only see Module do not need to destroy Instruction.
         */
        ~Module();

    private:
        std::vector<std::unique_ptr<GlobalVar>> global_vars_;
        std::vector<std::unique_ptr<Function>> functions_;
        std::vector<std::unique_ptr<IntegerType>> integer_types_;
        std::vector<std::unique_ptr<ConstantInt>> constants_;
        std::unordered_map<int64_t, ConstantInt*> const_i32_cache_;
    };

} // namespace ir

#endif // IR_MODULE_H

/**
 * @file Function.h
 * @brief Function: a callable unit with a list of basic blocks.
 *
 * Function is a Constant (its address is fixed). It owns its BasicBlocks.
 */

#ifndef IR_FUNCTION_H
#define IR_FUNCTION_H

#include "ir/BasicBlock.h"
#include "ir/Constant.h"

#include <memory>
#include <vector>

namespace ir {

    /**
     * @brief Represents a function in the module.
     *
     * Ownership: Function owns its BasicBlocks (vector of unique_ptr).
     * Module owns Functions. Function is a Constant because the function
     * address is a compile-time constant.
     */
    class Function : public Constant {
    public:
        Function() = default;
        Function(const std::string& name, Type* type) : Constant(name, type) {}

        /** @brief Get the list of basic blocks (ownership held here). */
        std::vector<std::unique_ptr<BasicBlock>>& GetBlocks() {
            return blocks_;
        }
        const std::vector<std::unique_ptr<BasicBlock>>& GetBlocks() const {
            return blocks_;
        }

    private:
        std::vector<std::unique_ptr<BasicBlock>> blocks_;
    };

} // namespace ir

#endif // IR_FUNCTION_H

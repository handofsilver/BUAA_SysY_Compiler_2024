/**
 * @file Function.h
 * @brief Function: a callable unit with a list of basic blocks.
 *
 * Function is a Constant (its address is fixed). It owns its BasicBlocks.
 */
#pragma once

#include "ir/Argument.h"
#include "ir/BasicBlock.h"
#include "ir/Constant.h"

#include <memory>
#include <ostream>
#include <vector>

namespace ir {

    /**
     * @brief Represents a function in the module.
     *
     * Ownership: Function owns its BasicBlocks and Arguments (vectors of unique_ptr).
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

        /** @brief Append basic block to this function. */
        void AddBlock(std::unique_ptr<BasicBlock> block) {
            blocks_.push_back(std::move(block));
        }

        /** @brief Get the list of arguments (ownership held here). */
        std::vector<std::unique_ptr<Argument>>& GetArguments() {
            return args_;
        }
        const std::vector<std::unique_ptr<Argument>>& GetArguments() const {
            return args_;
        }

        /** @brief Get the i-th argument value, or nullptr if out of range. */
        Argument* GetArgument(size_t i) const {
            if (i >= args_.size()) {
                return nullptr;
            }
            return args_[i].get();
        }

        /** @brief Add an argument to this function. */
        void AddArgument(std::unique_ptr<Argument> arg) {
            args_.push_back(std::move(arg));
        }

        /** @brief Print as operand: @name. */
        void DefaultPrintAsOperand(std::ostream& os) const override;

        /**
         * @brief Print declare or define to stream.
         * @param renumber_ssa If true (default), use layout-order SSA/block renumbering; if false,
         *        use original Value/Block names.
         */
        void Print(std::ostream& os, bool renumber_ssa = true) const;

    private:
        std::vector<std::unique_ptr<BasicBlock>> blocks_;
        std::vector<std::unique_ptr<Argument>> args_;
    };

} // namespace ir

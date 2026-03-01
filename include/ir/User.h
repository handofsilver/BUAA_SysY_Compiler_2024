/**
 * @file User.h
 * @brief User: a Value that uses other Values (has operands).
 *
 * User inherits Value and adds a vector of Use objects (operands).
 * Instructions and Constants are Users; BasicBlock is a Value but not a User
 * in the classic LLVM sense—here we follow the spec and have Instruction
 * inherit User.
 */
#pragma once

#include "ir/Use.h"
#include "ir/Value.h"

#include <cassert>
#include <vector>

namespace ir {

    /**
     * @brief A Value that has operands (references to other Values).
     *
     * Ownership: User owns the Use objects in operands_ (stored by value for
     * cache locality). The Value* in each Use are not owned.
     */
    class User : public Value {
    public:
        User() = default;
        User(const std::string& name, Type* type) : Value(name, type) {}
        virtual ~User() = default;

        /** @brief Number of operands. */
        size_t GetNumOperands() const {
            return operands_.size();
        }

        /**
         * @brief Get the Value at operand index i.
         * @return nullptr if i is out of range.
         */
        Value* GetOperand(int i) const {
            assert(i >= 0 && static_cast<size_t>(i) < operands_.size());
            return operands_[static_cast<size_t>(i)].GetValue();
        }

        /**
         * @brief Resize operand vector to n slots. New slots are bound to this User.
         * Call this before SetOperand when constructing instructions with a fixed operand count
         * to avoid iteration invalidation (resize once, then set by index).
         */
        void ResizeOperands(size_t n);

        /**
         * @brief Set operand i to val. Updates use lists (remove from old value, add to new).
         */
        void SetOperand(int i, Value* val);

        /** @brief Direct access to the operand storage (for construction / iteration). */
        std::vector<Use>& GetOperands() {
            return operands_;
        }
        const std::vector<Use>& GetOperands() const {
            return operands_;
        }

    protected:
        /** @brief Operands stored inline for cache locality. */
        std::vector<Use> operands_;
    };

} // namespace ir

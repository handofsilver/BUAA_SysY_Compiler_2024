/**
 * @file User.h
 * @brief User: a Value that uses other Values (has operands).
 *
 * User inherits Value and adds a vector of Use objects (operands).
 * Instructions and Constants are Users; BasicBlock is a Value but not a User
 * in the classic LLVM sense—here we follow the spec and have Instruction
 * inherit User.
 */

#ifndef IR_USER_H
#define IR_USER_H

#include "ir/Use.h"
#include "ir/Value.h"

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
            if (i < 0 || static_cast<size_t>(i) >= operands_.size())
                return nullptr;
            return operands_[static_cast<size_t>(i)].GetValue();
        }

        /**
         * @brief Set operand i to val. Does not update use lists.
         * TODO: caller is responsible for add_use/remove_use if needed.
         */
        void SetOperand(int i, Value* val) {
            if (i >= 0 && static_cast<size_t>(i) < operands_.size()) {
                operands_[static_cast<size_t>(i)].SetValue(val);
            }
        }

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

#endif // IR_USER_H

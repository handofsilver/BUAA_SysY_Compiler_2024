/**
 * @file Value.h
 * @brief Base class for all named IR entities with a type and use list.
 *
 * Value is the root of the IR hierarchy. Every value has a type and a name,
 * and maintains a list of Use objects that reference it (def-use chain).
 */

#ifndef IR_VALUE_H
#define IR_VALUE_H

#include "ir/Type.h"
#include "ir/Use.h"

#include <list>
#include <string>

namespace ir {

    class Use;

    /**
     * @brief Base class for all IR values (instructions, constants, arguments, etc.).
     *
     * Ownership: Value does not own its Type* (types are often shared).
     * The use_list_ stores pointers to Use objects that live inside User::operands_.
     */
    class Value {
    public:
        Value() : type_(nullptr) {}
        Value(const std::string& name, Type* type) : name_(name), type_(type) {}
        virtual ~Value() = default;

        const std::string& GetName() const {
            return name_;
        }
        void SetName(const std::string& name) {
            name_ = name;
        }

        Type* GetType() const {
            return type_;
        }
        void SetType(Type* type) {
            type_ = type;
        }

        /** @brief Get the list of Use objects that reference this Value. */
        const std::list<Use*>& GetUseList() const {
            return use_list_;
        }
        std::list<Use*>& GetUseList() {
            return use_list_;
        }

        /**
         * @brief Register a Use that references this Value.
         * Called when a User adds this Value as an operand.
         */
        void AddUse(Use* use) {
            use_list_.push_back(use);
        }

        /**
         * @brief Unregister a Use (e.g. when operand is replaced or User is destroyed).
         */
        void RemoveUse(Use* use) {
            use_list_.remove(use);
        }

        /**
         * @brief Replace every use of this Value with new_val.
         * Used by SSA substitution and optimizations.
         * TODO: implement; update all Users that reference this Value.
         */
        void ReplaceAllUsesWith(Value* new_val);

    protected:
        std::string name_;
        Type* type_;
        std::list<Use*> use_list_; /**< Uses that reference this Value (not owned). */
    };

} // namespace ir

#endif // IR_VALUE_H

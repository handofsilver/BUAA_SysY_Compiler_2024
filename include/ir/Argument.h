/**
 * @file Argument.h
 * @brief Function argument: a Value that represents a function parameter.
 *
 * Ownership: Function owns its Arguments (vector of unique_ptr).
 */

#ifndef IR_ARGUMENT_H
#define IR_ARGUMENT_H

#include "ir/Value.h"

namespace ir {

    /**
     * @brief Represents a function parameter (e.g. i32 %a, i32* %arr).
     * Not a User; just a Value with a type and name.
     */
    class Argument : public Value {
    public:
        Argument() = default;
        Argument(const std::string& name, Type* type) : Value(name, type) {}
    };

} // namespace ir

#endif // IR_ARGUMENT_H

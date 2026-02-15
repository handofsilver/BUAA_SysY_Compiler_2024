/**
 * @file GlobalVar.h
 * @brief Global variable: a Constant whose address is fixed at compile time.
 *
 * Global variables are declared at module scope and have a single address
 * for the entire program. They are Constants because that address is
 * constant.
 */

#ifndef IR_GLOBALVAR_H
#define IR_GLOBALVAR_H

#include "ir/Constant.h"

namespace ir {

    /**
     * @brief Represents a global variable in the module.
     *
     * Inherits Constant because the address of a global variable is a
     * compile-time constant. Owned by Module.
     */
    class GlobalVar : public Constant {
    public:
        GlobalVar() = default;
        GlobalVar(const std::string& name, Type* type) : Constant(name, type) {}
        virtual ~GlobalVar() = default;
    };

} // namespace ir

#endif // IR_GLOBALVAR_H

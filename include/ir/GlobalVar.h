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

        Constant* GetInitializer() const {
            return init_;
        }
        void SetInitializer(Constant* init) {
            init_ = init;
        }
        bool IsConstant() const {
            return is_constant_;
        }
        void SetConstant(bool c) {
            is_constant_ = c;
        }

    private:
        Constant* init_ = nullptr;
        bool is_constant_ = false;
    };

} // namespace ir

#endif // IR_GLOBALVAR_H

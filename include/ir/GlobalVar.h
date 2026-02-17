/**
 * @file GlobalVar.h
 * @brief Global variable: a Constant whose address is fixed at compile time.
 *
 * Global variables are declared at module scope and have a single address
 * for the entire program. They are Constants because that address is
 * constant.
 */
#pragma once

#include "ir/Constant.h"
#include <ostream>

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

        void PrintAsOperand(std::ostream& os) const override;

        /** @brief Print global/constant definition (e.g. @a = dso_local global i32 0, align 4). */
        void Print(std::ostream& os) const;

    private:
        Constant* init_ = nullptr;
        bool is_constant_ = false;
    };

} // namespace ir

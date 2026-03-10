/**
 * @file ValueLocation.h
 * @brief Describes where an IR Value lives in MIPS at runtime.
 *
 * Currently every Value is assigned a stack slot (STACK), so GetLocation()
 * always returns OnStack(offset).  When register allocation is enabled,
 * some Values will be promoted to REGISTER and LoadValueToReg will emit
 * a move instead of a load.
 *
 * This is the bridge between the "all values on stack" baseline and the
 * future register-allocated backend.
 */
#pragma once

#include <string>

namespace mips {

    struct ValueLocation {
        enum Kind { STACK, REGISTER };

        Kind kind;
        int stack_offset;     // valid when kind == STACK
        std::string reg_name; // valid when kind == REGISTER (e.g. "$s0")

        static ValueLocation OnStack(int offset) {
            return {STACK, offset, {}};
        }

        static ValueLocation InRegister(const std::string& reg) {
            return {REGISTER, 0, reg};
        }
    };

} // namespace mips

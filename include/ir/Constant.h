/**
 * @file Constant.h
 * @brief Constant: base class for compile-time constant values.
 *
 * Constants are Values that do not change at runtime (literals, global
 * addresses, function addresses). GlobalVar and Function inherit from
 * Constant because their addresses are constants.
 */
#pragma once

#include "ir/User.h"

#include <ostream>
#include <vector>

namespace ir {

    /**
     * @brief Base class for all constant values in the IR.
     *
     * Constants are Users (they may have operands, e.g. constant expressions).
     * Subclasses: GlobalVar, Function, ConstantInt.
     */
    class Constant : public User {
    public:
        Constant() = default;
        Constant(const std::string& name, Type* type) : User(name, type) {}
        virtual ~Constant() = default;
    };

    /**
     * @brief Integer constant (e.g. i32 0, i8 97). Not an instruction; used as operands.
     * Ownership: typically held by Module's constant pool.
     */
    class ConstantInt : public Constant {
    public:
        ConstantInt() : value_(0) {}
        ConstantInt(const std::string& name, Type* type, int64_t value) :
        Constant(name, type),
        value_(value) {}

        int64_t GetValue() const {
            return value_;
        }

        void PrintAsOperand(std::ostream& os) const override;

    private:
        int64_t value_;
    };

    class ArrayType; // forward

    /**
     * @brief Constant array (e.g. [3 x i32] [i32 1, i32 2, i32 3]).
     * Used as global array initializer. Elements are not owned (typically ConstantInt from Module).
     */
    class ConstantArray : public Constant {
    public:
        ConstantArray(const std::string& name, ArrayType* type,
                      const std::vector<Constant*>& elements) :
        Constant(name, type),
        elements_(elements) {}
        const std::vector<Constant*>& GetElements() const {
            return elements_;
        }

    private:
        std::vector<Constant*> elements_;
    };
} // namespace ir

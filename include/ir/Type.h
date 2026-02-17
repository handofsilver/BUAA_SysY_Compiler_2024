/**
 * @file Type.h
 * @brief LLVM IR type system: TypeID enum and Type hierarchy.
 *
 * Types represent the shape of values (void, label, integer, pointer, function).
 * Used by Value and related IR classes for type checking and IR printing.
 */
#pragma once

#include <ostream>
#include <vector>

namespace ir {

    /**
     * @brief Enumeration of all type kinds in the IR.
     */
    enum class TypeID {
        VOID_TY_ID,
        LABEL_TY_ID,
        INTEGER_TY_ID,
        FUNCTION_TY_ID,
        POINTER_TY_ID,
        ARRAY_TY_ID
    };

    /**
     * @brief Base class for all IR types.
     * Stores the type kind (TypeID) for dispatch and debugging.
     */
    class Type {
    public:
        explicit Type(TypeID id) : type_id_(id) {}
        virtual ~Type() = default;

        TypeID GetTypeId() const {
            return type_id_;
        }
        void SetTypeId(TypeID id) {
            type_id_ = id;
        }

        /** @brief Print this type to LLVM IR text (e.g. i32, ptr, void). */
        virtual void Print(std::ostream& os) const = 0;

    protected:
        TypeID type_id_;
    };

    /**
     * @brief Integer type (e.g. i1, i8, i32).
     * Holds the bit width for the integer.
     */
    class IntegerType : public Type {
    public:
        explicit IntegerType(unsigned bits = 32) : Type(TypeID::INTEGER_TY_ID), bits_(bits) {}

        unsigned GetBits() const {
            return bits_;
        }
        void SetBits(unsigned bits) {
            bits_ = bits;
        }

        void Print(std::ostream& os) const override;

    private:
        unsigned bits_;
    };

    /**
     * @brief Pointer type: points to another type.
     * Must store the pointee type for address calculation and type checking.
     */
    class PointerType : public Type {
    public:
        explicit PointerType(Type* pointee_type) :
        Type(TypeID::POINTER_TY_ID),
        pointee_type_(pointee_type) {}

        Type* GetPointeeType() const {
            return pointee_type_;
        }
        void SetPointeeType(Type* t) {
            pointee_type_ = t;
        }

        void Print(std::ostream& os) const override;

    private:
        Type* pointee_type_;
    };

    /**
     * @brief Array type: [N x element_type] (e.g. [10 x i32]).
     * Used as pointee of pointer for local/global arrays; distinguishes from i32* (param).
     */
    class ArrayType : public Type {
    public:
        ArrayType(Type* element_type, unsigned num_elements) :
        Type(TypeID::ARRAY_TY_ID),
        element_type_(element_type),
        num_elements_(num_elements) {}

        Type* GetElementType() const {
            return element_type_;
        }
        unsigned GetNumElements() const {
            return num_elements_;
        }

        void Print(std::ostream& os) const override;

    private:
        Type* element_type_;
        unsigned num_elements_;
    };

    /**
     * @brief Function type: return type + parameter types.
     */
    class FunctionType : public Type {
    public:
        FunctionType(Type* return_type, const std::vector<Type*>& param_types) :
        Type(TypeID::FUNCTION_TY_ID),
        return_type_(return_type),
        param_types_(param_types) {}

        Type* GetReturnType() const {
            return return_type_;
        }
        void SetReturnType(Type* t) {
            return_type_ = t;
        }
        const std::vector<Type*>& GetParamTypes() const {
            return param_types_;
        }
        std::vector<Type*>& SetParamTypes() {
            return param_types_;
        }

        void Print(std::ostream& os) const override;

    private:
        Type* return_type_;
        std::vector<Type*> param_types_;
    };

    /**
     * @brief Void type (e.g. for functions returning void).
     */
    class VoidType : public Type {
    public:
        VoidType() : Type(TypeID::VOID_TY_ID) {}
        void Print(std::ostream& os) const override {
            os << "void";
        }
    };

    /**
     * @brief Label type (for basic block labels).
     */
    class LabelType : public Type {
    public:
        LabelType() : Type(TypeID::LABEL_TY_ID) {}
        void Print(std::ostream& os) const override {
            os << "label";
        }
    };

} // namespace ir

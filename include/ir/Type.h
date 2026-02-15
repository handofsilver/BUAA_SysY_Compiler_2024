/**
 * @file Type.h
 * @brief LLVM IR type system: TypeID enum and Type hierarchy.
 *
 * Types represent the shape of values (void, label, integer, pointer, function).
 * Used by Value and related IR classes for type checking and IR printing.
 */

#ifndef IR_TYPE_H
#define IR_TYPE_H

#include <vector>

namespace ir {

    /**
     * @brief Enumeration of all type kinds in the IR.
     */
    enum class TypeID { VoidTyID, LabelTyID, IntegerTyID, FunctionTyID, PointerTyID };

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

    protected:
        TypeID type_id_;
    };

    /**
     * @brief Integer type (e.g. i1, i8, i32).
     * Holds the bit width for the integer.
     */
    class IntegerType : public Type {
    public:
        explicit IntegerType(unsigned bits = 32) : Type(TypeID::IntegerTyID), bits_(bits) {}

        unsigned GetBits() const {
            return bits_;
        }
        void SetBits(unsigned bits) {
            bits_ = bits;
        }

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
        Type(TypeID::PointerTyID),
        pointee_type_(pointee_type) {}

        Type* GetPointerType() const {
            return pointee_type_;
        }
        void SetPointerType(Type* t) {
            pointee_type_ = t;
        }

    private:
        Type* pointee_type_;
    };

    /**
     * @brief Function type: return type + parameter types.
     */
    class FunctionType : public Type {
    public:
        FunctionType(Type* return_type, const std::vector<Type*>& param_types) :
        Type(TypeID::FunctionTyID),
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

    private:
        Type* return_type_;
        std::vector<Type*> param_types_;
    };

    /**
     * @brief Void type (e.g. for functions returning void).
     */
    class VoidType : public Type {
    public:
        VoidType() : Type(TypeID::VoidTyID) {}
    };

    /**
     * @brief Label type (for basic block labels).
     */
    class LabelType : public Type {
    public:
        LabelType() : Type(TypeID::LabelTyID) {}
    };

    /**
     * @brief Returns the canonical void type (singleton). Used e.g. for ret void.
     */
    inline Type* GetVoidType() {
        static VoidType v;
        return &v;
    }

} // namespace ir

#endif // IR_TYPE_H

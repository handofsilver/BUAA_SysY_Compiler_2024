/**
 * @file TypeManager.h
 * @brief Flyweight-pattern manager for all IR types.
 *
 * TypeManager is a process-wide singleton that owns every Type object in the IR.
 * Primitive types (i1, i8, i32, void, label) are created once at construction;
 * composite types (PointerType, ArrayType) are cached and reused.
 *
 * All returned raw pointers are non-owning and remain valid for the lifetime of
 * the TypeManager (i.e. the entire process).
 */
#pragma once

#include "ir/Type.h"

#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

namespace ir {

    class TypeManager {
    public:
        /** @brief Access the process-wide singleton instance. */
        static TypeManager& Get();

        // -- Primitive singletons ------------------------------------------------

        IntegerType* GetI32Type() const {
            return i32_type_;
        }
        IntegerType* GetI8Type() const {
            return i8_type_;
        }
        IntegerType* GetI1Type() const {
            return i1_type_;
        }
        VoidType* GetVoidType() const {
            return void_type_;
        }
        LabelType* GetLabelType() const {
            return label_type_;
        }

        // -- Composite types (cached) --------------------------------------------

        /** @brief Get or create a PointerType for the given pointee. */
        PointerType* GetPointerType(Type* pointee_type);

        /**
         * @brief Get or create an ArrayType [num_elements x element_type].
         * Cached by (element_type, num_elements) pair.
         */
        ArrayType* GetArrayType(Type* element_type, unsigned num_elements);

        // Non-copyable / non-movable
        TypeManager(const TypeManager&) = delete;
        TypeManager& operator=(const TypeManager&) = delete;

    private:
        TypeManager();
        ~TypeManager() = default;

        /** @brief Master container: every Type created through this manager lives here. */
        std::vector<std::unique_ptr<Type>> owned_types_;

        // Cached pointers into owned_types_ (non-owning).
        IntegerType* i32_type_;
        IntegerType* i8_type_;
        IntegerType* i1_type_;
        VoidType* void_type_;
        LabelType* label_type_;

        // Composite type caches: key -> non-owning pointer into owned_types_.
        std::unordered_map<Type*, PointerType*> ptr_cache_;
        std::map<std::pair<Type*, unsigned>, ArrayType*> array_cache_;
    };

} // namespace ir

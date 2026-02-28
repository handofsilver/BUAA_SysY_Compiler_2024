/**
 * @file TypeManager.cpp
 * @brief Flyweight-pattern Type manager implementation.
 *
 * Primitive types are created eagerly in the constructor.
 * Composite types (Pointer, Array) are created lazily and cached.
 */
#include "ir/TypeManager.h"

#include <cassert>

namespace ir {

    // -- Singleton accessor ------------------------------------------------------

    TypeManager& TypeManager::Get() {
        static TypeManager instance;
        return instance;
    }

    // -- Constructor: eagerly create primitive singletons ------------------------

    TypeManager::TypeManager() {
        // i32
        auto i32 = std::make_unique<IntegerType>(32);
        i32_type_ = i32.get();
        owned_types_.push_back(std::move(i32));

        // i8
        auto i8 = std::make_unique<IntegerType>(8);
        i8_type_ = i8.get();
        owned_types_.push_back(std::move(i8));

        // i1
        auto i1 = std::make_unique<IntegerType>(1);
        i1_type_ = i1.get();
        owned_types_.push_back(std::move(i1));

        // void
        auto v = std::make_unique<VoidType>();
        void_type_ = v.get();
        owned_types_.push_back(std::move(v));

        // label
        auto lbl = std::make_unique<LabelType>();
        label_type_ = lbl.get();
        owned_types_.push_back(std::move(lbl));
    }

    // -- Composite type factories ------------------------------------------------

    PointerType* TypeManager::GetPointerType(Type* pointee_type) {
        assert(pointee_type);

        auto it = ptr_cache_.find(pointee_type);
        if (it != ptr_cache_.end()) {
            return it->second;
        }

        auto ptr = std::make_unique<PointerType>(pointee_type);
        PointerType* raw = ptr.get();
        owned_types_.push_back(std::move(ptr));
        ptr_cache_[pointee_type] = raw;
        return raw;
    }

    ArrayType* TypeManager::GetArrayType(Type* element_type, unsigned num_elements) {
        assert(element_type && num_elements > 0);

        auto key = std::make_pair(element_type, num_elements);
        auto it = array_cache_.find(key);
        if (it != array_cache_.end()) {
            return it->second;
        }

        auto arr = std::make_unique<ArrayType>(element_type, num_elements);
        ArrayType* raw = arr.get();
        owned_types_.push_back(std::move(arr));
        array_cache_[key] = raw;
        return raw;
    }

} // namespace ir

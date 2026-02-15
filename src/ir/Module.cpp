/**
 * @file Module.cpp
 * @brief Module: type and constant pool; out-of-line destructor.
 */
#include "ir/Module.h"

namespace ir {

    Module::~Module() = default;

    IntegerType* Module::GetI32Type() {
        if (integer_types_.empty()) {
            integer_types_.push_back(std::make_unique<IntegerType>(32));
        }
        return integer_types_[0].get();
    }

    IntegerType* Module::GetI8Type() {
        if (integer_types_.size() < 2u) {
            integer_types_.push_back(std::make_unique<IntegerType>(8));
        }
        return integer_types_[1].get();
    }

    IntegerType* Module::GetI1Type() {
        if (integer_types_.size() < 3u) {
            integer_types_.push_back(std::make_unique<IntegerType>(1));
        }
        return integer_types_[2].get();
    }

    ConstantInt* Module::GetInt32Constant(int64_t value) {
        auto it = const_i32_cache_.find(value);
        if (it != const_i32_cache_.end()) {
            return it->second;
        }
        IntegerType* i32 = GetI32Type();
        auto c = std::make_unique<ConstantInt>("", i32, value);
        ConstantInt* p = c.get();
        constants_.push_back(std::move(c));
        const_i32_cache_[value] = p;
        return p;
    }

} // namespace ir

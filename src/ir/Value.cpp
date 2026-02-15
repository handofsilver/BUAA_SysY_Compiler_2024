/**
 * @file Value.cpp
 * @brief Implementation of Value: use-list and ReplaceAllUsesWith.
 */

#include "ir/Value.h"
#include "ir/Use.h"

namespace ir {

    void Value::ReplaceAllUsesWith(Value* new_val) {
        if (!new_val) {
            return;
        }
        // Iterate over a copy so we can modify use_list_ during the loop.
        auto use_list_copy = use_list_;
        for (Use* use : use_list_copy) {
            RemoveUse(use);
            use->SetValue(new_val);
            new_val->AddUse(use);
        }
    }

} // namespace ir

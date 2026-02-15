/**
 * @file User.cpp
 * @brief Implementation of User: ResizeOperands and SetOperand (use-list maintenance).
 */

#include "ir/User.h"
#include "ir/Use.h"
#include "ir/Value.h"

namespace ir {

    void User::ResizeOperands(size_t n) {
        operands_.resize(n);
        for (size_t i = 0; i < n; ++i) {
            operands_[i].SetUser(this);
            operands_[i].SetOperandNo(static_cast<int>(i));
        }
    }

    void User::SetOperand(int i, Value* val) {
        if (i >= 0 && static_cast<size_t>(i) < operands_.size()) {
            Use& use = operands_[static_cast<size_t>(i)];
            Value* old_val = use.GetValue();
            if (old_val) {
                old_val->RemoveUse(&use);
            }
            use.SetValue(val);
            if (val) {
                val->AddUse(&use);
            }
        }
    }

} // namespace ir

/**
 * @file Constant.cpp
 * @brief PrintAsOperand for ConstantInt (and other constants if needed).
 */
#include "ir/Constant.h"
#include "ir/Type.h"
#include <ostream>

namespace ir {

    void ConstantInt::DefaultPrintAsOperand(std::ostream& os) const {
        if (type_) {
            type_->Print(os);
            os << " " << value_;
        } else {
            os << "i32 " << value_;
        }
    }

} // namespace ir

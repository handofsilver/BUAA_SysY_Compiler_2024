/**
 * @file Type.cpp
 * @brief Print implementations for IR types (LLVM IR text).
 */
#include "ir/Type.h"

namespace ir {

    void IntegerType::Print(std::ostream& os) const {
        os << "i" << bits_;
    }

    void PointerType::Print(std::ostream& os) const {
        if (pointee_type_) {
            pointee_type_->Print(os);
            os << "*";
        } else {
            os << "ptr";
        }
    }

    void ArrayType::Print(std::ostream& os) const {
        os << "[" << num_elements_ << " x ";
        if (element_type_) {
            element_type_->Print(os);
        }
        os << "]";
    }

    void FunctionType::Print(std::ostream& os) const {
        if (return_type_) {
            return_type_->Print(os);
        }
        os << " (";
        for (size_t i = 0; i < param_types_.size(); ++i) {
            if (i != 0) {
                os << ", ";
            }
            if (param_types_[i]) {
                param_types_[i]->Print(os);
            }
        }
        os << ")";
    }

} // namespace ir

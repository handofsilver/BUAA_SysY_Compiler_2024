/**
 * @file GlobalVar.cpp
 * @brief PrintAsOperand and Print for GlobalVar.
 */
#include "ir/GlobalVar.h"
#include "ir/Constant.h"
#include "ir/Type.h"
#include <ostream>

namespace ir {

    void GlobalVar::PrintAsOperand(std::ostream& os) const {
        os << "@" << GetName();
    }

    void GlobalVar::Print(std::ostream& os) const {
        os << "@" << GetName() << " = dso_local ";
        os << (IsConstant() ? "constant " : "global ");
        if (type_) {
            type_->Print(os);
        }
        os << " ";
        if (Constant* init = GetInitializer()) {
            if (ConstantInt* ci = dynamic_cast<ConstantInt*>(init)) {
                os << ci->GetValue();
            } else if (ConstantArray* ca = dynamic_cast<ConstantArray*>(init)) {
                os << "[";
                const auto& elts = ca->GetElements();
                for (size_t i = 0; i < elts.size(); ++i) {
                    if (i != 0) {
                        os << ", ";
                    }
                    if (elts[i]) {
                        elts[i]->PrintAsOperand(os);
                    }
                }
                os << "]";
            } else {
                os << "zeroinitializer";
            }
        } else {
            os << "zeroinitializer";
        }
        os << ", align 4\n";
    }

} // namespace ir

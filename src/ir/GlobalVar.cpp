/**
 * @file GlobalVar.cpp
 * @brief PrintAsOperand and Print for GlobalVar.
 */
#include "ir/GlobalVar.h"
#include "ir/Constant.h"
#include "ir/Type.h"
#include <cassert>
#include <ostream>
namespace ir {

    void GlobalVar::DefaultPrintAsOperand(std::ostream& os) const {
        os << "@" << GetName();
    }

    void GlobalVar::Print(std::ostream& os) const {
        os << "@" << GetName() << " = dso_local ";
        os << (IsConstant() ? "constant " : "global ");

        assert(type_ != nullptr && dynamic_cast<PointerType*>(type_) != nullptr);
        PointerType* pt = dynamic_cast<PointerType*>(type_);
        pt->GetPointeeType()->Print(os);
        os << " ";

        bool is_array = dynamic_cast<ArrayType*>(pt->GetPointeeType()) != nullptr;
        bool is_char = false;
        if (is_array) {
            auto element_type = dynamic_cast<ArrayType*>(pt->GetPointeeType())->GetElementType();
            is_char = dynamic_cast<IntegerType*>(element_type) != nullptr &&
                      dynamic_cast<IntegerType*>(element_type)->GetBits() == 8;
        } else {
            is_char = dynamic_cast<IntegerType*>(pt->GetPointeeType()) != nullptr &&
                      dynamic_cast<IntegerType*>(pt->GetPointeeType())->GetBits() == 8;
        }

        if (Constant* init = GetInitializer()) {
            if (ConstantInt* ci = dynamic_cast<ConstantInt*>(init)) { // integer scalar
                os << ci->GetValue();
            } else if (ConstantArray* ca = dynamic_cast<ConstantArray*>(init)) {
                if (is_char) { // string literal or i8 array
                    os << "c\"";
                    const auto& elts = ca->GetElements();
                    for (size_t i = 0; i < elts.size(); ++i) {
                        auto* ci = dynamic_cast<ConstantInt*>(elts[i]);
                        if (ci && ci->GetValue() != 0) {
                            char ch = static_cast<char>(ci->GetValue());
                            if (ch == '\n') {
                                os << "\\0A";
                            } else {
                                os << ch;
                            }
                        } else {
                            os << "\\00";
                        }
                    }
                    os << "\"";
                } else { // integer array
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
                }
            } else {
                assert(false && "GlobalVar::Print: unhandled initializer type");
            }
        } else {
            os << (is_array ? "zeroinitializer" : "0");
        }
        os << ", align " << (is_char ? "1" : "4");
        os << "\n";
    }

} // namespace ir

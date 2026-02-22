/**
 * @file Argument.cpp
 * @brief PrintAsOperand for Argument (type %name).
 */
#include "ir/Argument.h"
#include <ostream>

namespace ir {

    void Argument::DefaultPrintAsOperand(std::ostream& os) const {
        os << "%" << (GetName().empty() ? "0" : GetName());
    }

} // namespace ir

#include "Symbol.h"
#include <sstream>

std::string ToString(SymbolType type) {
    switch (type) {
        case SymbolType::CONST_CHAR: return "ConstChar";
        case SymbolType::CHAR: return "Char";
        case SymbolType::VOID_FUNC: return "VoidFunc";
        case SymbolType::CONST_INT: return "ConstInt";
        case SymbolType::INT: return "Int";
        case SymbolType::CHAR_FUNC: return "CharFunc";
        case SymbolType::CONST_CHAR_ARRAY: return "ConstCharArray";
        case SymbolType::CHAR_ARRAY: return "CharArray";
        case SymbolType::INT_FUNC: return "IntFunc";
        case SymbolType::CONST_INT_ARRAY: return "ConstIntArray";
        case SymbolType::INT_ARRAY: return "IntArray";
        default: return "";
    }
}

std::string Symbol::FormatForOutput() const {
    std::ostringstream os;
    os << scope_id << " " << name << " " << ToString(type);
    return os.str();
}

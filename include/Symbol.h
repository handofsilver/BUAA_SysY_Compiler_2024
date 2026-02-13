#pragma once

#include "AST.h"
#include <optional>
#include <string>
#include <vector>

// =============================================================================
// Symbol type for output (requirement_3: ConstChar, Int, VoidFunc, etc.)
// =============================================================================
enum class SymbolType {
    CONST_CHAR,
    CHAR,
    VOID_FUNC,
    CONST_INT,
    INT,
    CHAR_FUNC,
    CONST_CHAR_ARRAY,
    CHAR_ARRAY,
    INT_FUNC,
    CONST_INT_ARRAY,
    INT_ARRAY,
};

/** Returns the type name string required by symbol.txt (e.g. "ConstInt", "INT_FUNC"). */
std::string ToString(SymbolType type);

inline bool IsConst(SymbolType type) {
    return type == SymbolType::CONST_INT || type == SymbolType::CONST_CHAR ||
           type == SymbolType::CONST_INT_ARRAY || type == SymbolType::CONST_CHAR_ARRAY;
}

inline bool IsFunc(SymbolType type) {
    return type == SymbolType::INT_FUNC || type == SymbolType::CHAR_FUNC ||
           type == SymbolType::VOID_FUNC;
}

inline bool IsArray(SymbolType type) {
    return type == SymbolType::INT_ARRAY || type == SymbolType::CHAR_ARRAY ||
           type == SymbolType::CONST_INT_ARRAY || type == SymbolType::CONST_CHAR_ARRAY;
}

// =============================================================================
// Symbol: pure semantic info (no AST pointers). Suitable for symbol table and IR.
// =============================================================================
struct Symbol {
    SymbolType type;
    std::string name;
    int scope_id;

    /** For constants: folded value (ConstExp evaluated to int). Empty for non-const. */
    std::vector<int> const_values;

    /** For functions: param types (INT/CHAR, array flag). Empty for non-func. */
    std::vector<std::pair<BType, bool>> param_types;

    /** For 1D array: size (evaluated ConstExp). Empty for scalar. */
    std::optional<int> array_size;

    /** For symbol.txt output: "scope_id name type_name". */
    std::string FormatForOutput() const;
};

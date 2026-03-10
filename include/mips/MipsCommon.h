/**
 * @file MipsCommon.h
 * @brief Shared constants and label-generation utilities for the MIPS backend.
 */
#pragma once

#include <string>

namespace mips {

    /// Indentation prefix for instructions under a label (4 spaces).
    inline constexpr const char* kIndent = "    ";

    /// Generate a MIPS-compatible global label from an IR global name.
    /// Replaces '.' with '_' and prepends "global_" (e.g. ".str.0" -> "global_str_0").
    std::string GlobalLabel(const std::string& ir_name);

    /// Generate a MIPS-compatible block label: "funcname_blockname".
    /// Replaces '.' with '_' in block_name to satisfy MARS label constraints.
    std::string BlockLabel(const std::string& func_name, const std::string& block_name);

    /// Return true if @p name is a runtime library function
    /// (getint, getchar, putint, putch, putstr).
    bool IsLibraryFunction(const std::string& name);

} // namespace mips

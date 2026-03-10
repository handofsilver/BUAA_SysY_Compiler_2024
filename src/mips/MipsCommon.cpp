#include "mips/MipsCommon.h"
#include <algorithm>

namespace mips {

    std::string GlobalLabel(const std::string& ir_name) {
        std::string name = ir_name;
        std::replace(name.begin(), name.end(), '.', '_');
        return (name[0] == '_' ? "global" : "global_") + name;
    }

    std::string BlockLabel(const std::string& func_name, const std::string& block_name) {
        std::string label = block_name;
        std::replace(label.begin(), label.end(), '.', '_');
        return func_name + "_" + label;
    }

    bool IsLibraryFunction(const std::string& name) {
        return name == "getint" || name == "getchar" || name == "putint" || name == "putch" ||
               name == "putstr";
    }

} // namespace mips

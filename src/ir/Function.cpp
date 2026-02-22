/**
 * @file Function.cpp
 * @brief PrintAsOperand and Print (declare/define) for Function.
 */
#include "ir/Function.h"
#include "ir/IRPrintContext.h"
#include "ir/Type.h"
#include <ostream>
#include <string>

namespace ir {

    void Function::DefaultPrintAsOperand(std::ostream& os) const {
        os << "@" << GetName();
    }

    void Function::Print(std::ostream& os) const {
        Type* ty = GetType();
        FunctionType* ft = ty ? dynamic_cast<FunctionType*>(ty) : nullptr;
        if (!ft) {
            return;
        }
        Type* return_type = ft->GetReturnType();
        const auto& param_types = ft->GetParamTypes();

        if (blocks_.empty()) {
            // Declaration only (e.g. getint, putint).
            os << "declare ";
            if (return_type) {
                return_type->Print(os);
            }
            os << " @" << GetName() << "(";
            for (size_t i = 0; i < param_types.size(); ++i) {
                if (i != 0) {
                    os << ", ";
                }
                if (param_types[i]) {
                    param_types[i]->Print(os);
                }
            }
            os << ")\n";
            return;
        }

        // Definition: build print-time SSA/block renumbering, then print.
        IRPrintContext ctx;
        ctx.Build(this);

        os << "define dso_local ";
        if (return_type) {
            return_type->Print(os);
        }
        os << " @" << GetName() << "(";
        for (size_t i = 0; i < args_.size(); ++i) {
            if (i != 0) {
                os << ", ";
            }
            Argument* arg = args_[i].get();
            if (arg && arg->GetType()) {
                arg->GetType()->Print(os);
                std::string name;
                if (ctx.GetSSAName(arg, name)) {
                    os << " %" << name;
                } else {
                    os << " %" << (arg->GetName().empty() ? std::to_string(i) : arg->GetName());
                }
            }
        }
        os << ") #0 {\n";

        for (const auto& block : blocks_) {
            if (block) {
                block->Print(os, &ctx);
            }
        }

        os << "}\n";
    }

} // namespace ir

/**
 * @file IRPrintContext.cpp
 * @brief Build and query print-time SSA and block labels.
 */
#include "ir/IRPrintContext.h"
#include "ir/BasicBlock.h"
#include "ir/Function.h"
#include "ir/Instruction.h"
#include "ir/Type.h"
#include <cctype>
#include <string>

namespace ir {

    namespace {

        /** @brief Returns true if this instruction produces an SSA value (has a %N result). */
        bool InstructionProducesValue(const Instruction* inst) {
            if (dynamic_cast<const StoreInst*>(inst)) {
                return false;
            }
            if (dynamic_cast<const BranchInst*>(inst)) {
                return false;
            }
            if (dynamic_cast<const ReturnInst*>(inst)) {
                return false;
            }
            const CallInst* call = dynamic_cast<const CallInst*>(inst);
            if (call && call->GetType() && call->GetType()->GetTypeId() == TypeID::VOID_TY_ID) {
                return false;
            }
            // PhiInst always produces a value (handled by falling through to return true).
            return true;
        }

        /** @brief Base name for block label: "for.cond.2" -> "for.cond", "entry" -> "entry". */
        std::string GetBlockBaseName(const BasicBlock* bb) {
            const std::string& name = bb->GetName();
            if (name.empty() || name == "entry") {
                return name.empty() ? "0" : "entry";
            }
            std::string::size_type pos = name.find_last_of('.');
            if (pos == std::string::npos || pos + 1 >= name.size()) {
                return name;
            }
            const std::string kSuffix = name.substr(pos + 1);
            for (char c : kSuffix) {
                if (!std::isdigit(static_cast<unsigned char>(c))) {
                    return name;
                }
            }
            return name.substr(0, pos);
        }

    } // namespace

    void IRPrintContext::Build(const Function* func) {
        value_to_name_.clear();
        block_to_label_.clear();
        if (!func) {
            return;
        }
        int next_ssa = 0;
        int block_index = 0;
        const auto& args = func->GetArguments();
        const auto& blocks = func->GetBlocks();
        for (const auto& arg : args) {
            if (arg) {
                value_to_name_[arg.get()] = std::to_string(next_ssa++);
            }
        }
        for (const auto& block : blocks) {
            if (!block) {
                continue;
            }
            BasicBlock* bb = block.get();
            std::string label = (block_index == 0) ?
                                    "entry" :
                                    (GetBlockBaseName(bb) + "." + std::to_string(block_index));
            block_to_label_[bb] = label;
            block_index++;
            for (const auto& inst : bb->GetInstructions()) {
                if (inst && InstructionProducesValue(inst.get())) {
                    value_to_name_[inst.get()] = std::to_string(next_ssa++);
                }
            }
        }
    }

    bool IRPrintContext::GetSSAName(const Value* val, std::string& out) const {
        auto it = value_to_name_.find(val);
        if (it == value_to_name_.end()) {
            return false;
        }
        out = it->second;
        return true;
    }

    bool IRPrintContext::GetBlockLabel(const BasicBlock* bb, std::string& out) const {
        auto it = block_to_label_.find(bb);
        if (it == block_to_label_.end()) {
            return false;
        }
        out = it->second;
        return true;
    }

} // namespace ir

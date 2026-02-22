/**
 * @file BasicBlock.cpp
 * @brief Implementation of BasicBlock: AddInstruction, Print, destructor.
 *
 * Instruction.h is included here so that unique_ptr<Instruction> can be destroyed
 * and so that inst->SetParent(this) can be called.
 */

#include "ir/BasicBlock.h"
#include "ir/Instruction.h"
#include "ir/IRPrintContext.h"
#include <ostream>

namespace ir {

    BasicBlock::~BasicBlock() = default;

    void BasicBlock::DefaultPrintAsOperand(std::ostream& os) const {
        os << "label %" << (GetName().empty() ? "0" : GetName());
    }

    void BasicBlock::Print(std::ostream& os, const IRPrintContext* context) const {
        std::string label;
        if (context && context->GetBlockLabel(this, label)) {
            os << label << ":\n";
        } else {
            os << (GetName().empty() ? "0" : GetName()) << ":\n";
        }
        for (const auto& inst : instructions_) {
            if (inst) {
                inst->Print(os, context);
                os << "\n";
            }
        }
    }

    std::list<std::unique_ptr<Instruction>>& BasicBlock::GetInstructions() {
        return instructions_;
    }

    const std::list<std::unique_ptr<Instruction>>& BasicBlock::GetInstructions() const {
        return instructions_;
    }

    void BasicBlock::AddInstruction(std::unique_ptr<Instruction> inst) {
        if (inst) {
            inst->SetParent(this);
        }
        instructions_.push_back(std::move(inst));
    }

    void BasicBlock::AddInstructionAtFront(std::unique_ptr<Instruction> inst) {
        if (inst) {
            inst->SetParent(this);
        }
        instructions_.push_front(std::move(inst));
    }

} // namespace ir

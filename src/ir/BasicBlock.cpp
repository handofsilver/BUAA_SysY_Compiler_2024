/**
 * @file BasicBlock.cpp
 * @brief Implementation of BasicBlock: AddInstruction (set parent + append), destructor.
 *
 * Instruction.h is included here so that unique_ptr<Instruction> can be destroyed
 * and so that inst->SetParent(this) can be called.
 */

#include "ir/BasicBlock.h"
#include "ir/Instruction.h"

namespace ir {

    BasicBlock::~BasicBlock() = default;

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

} // namespace ir

/**
 * @file BasicBlock.h
 * @brief BasicBlock: a sequence of instructions with a single entry and exit.
 *
 * BasicBlock is a Value (has a label/name and type LabelType). It owns
 * its instructions via unique_ptr list for efficient insertion/removal.
 */
#pragma once

#include "ir/Instruction.h"
#include "ir/Value.h"
#include <list>
#include <memory>
#include <ostream>

namespace ir {

    /**
     * @brief A basic block: linear sequence of instructions, single entry, single exit.
     *
     * Ownership: BasicBlock owns its Instruction objects (list of unique_ptr).
     * Typically owned by a Function.
     */
    class BasicBlock : public Value {
    public:
        BasicBlock() = default;
        explicit BasicBlock(const std::string& name) : Value(name, nullptr) {}
        ~BasicBlock(); // defined in .cpp so Instruction is complete when destroying
                       // list<unique_ptr<Instruction>>

        /** @brief Get the list of instructions (ownership held here). */
        std::list<std::unique_ptr<Instruction>>& GetInstructions();
        const std::list<std::unique_ptr<Instruction>>& GetInstructions() const;

        /** @brief Append instruction to this block; sets inst's parent to this. */
        void AddInstruction(std::unique_ptr<Instruction> inst);

        void PrintAsOperand(std::ostream& os) const override;
        void Print(std::ostream& os) const;

    private:
        std::list<std::unique_ptr<Instruction>> instructions_;
    };

} // namespace ir

/**
 * @file BasicBlock.h
 * @brief BasicBlock: a sequence of instructions with a single entry and exit.
 *
 * BasicBlock is a Value (has a label/name and type LabelType). It owns
 * its instructions via unique_ptr list for efficient insertion/removal.
 */

#ifndef IR_BASICBLOCK_H
#define IR_BASICBLOCK_H

#include "ir/Value.h"
#include <list>
#include <memory>

namespace ir {
    class Instruction;

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
        ~BasicBlock();  // defined in .cpp so Instruction is complete when destroying list<unique_ptr<Instruction>>

        /** @brief Get the list of instructions (ownership held here). */
        std::list<std::unique_ptr<Instruction>>& GetInstructions();
        const std::list<std::unique_ptr<Instruction>>& GetInstructions() const;

        /** @brief Append instruction to this block; sets inst's parent to this. */
        void AddInstruction(std::unique_ptr<Instruction> inst);

    private:
        std::list<std::unique_ptr<Instruction>> instructions_;
    };

} // namespace ir

#endif // IR_BASICBLOCK_H

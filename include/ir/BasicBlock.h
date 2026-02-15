/**
 * @file BasicBlock.h
 * @brief BasicBlock: a sequence of instructions with a single entry and exit.
 *
 * BasicBlock is a Value (has a label/name and type LabelType). It owns
 * its instructions via unique_ptr list for efficient insertion/removal.
 */

#ifndef IR_BASICBLOCK_H
#define IR_BASICBLOCK_H

#include "ir/Instruction.h"
#include "ir/Value.h"

#include <list>
#include <memory>

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

        /** @brief Get the list of instructions (ownership held here). */
        std::list<std::unique_ptr<Instruction>>& GetInstructions() {
            return instructions_;
        }
        const std::list<std::unique_ptr<Instruction>>& GetInstructions() const {
            return instructions_;
        }

    private:
        std::list<std::unique_ptr<Instruction>> instructions_;
    };

} // namespace ir

#endif // IR_BASICBLOCK_H

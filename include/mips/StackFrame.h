/**
 * @file StackFrame.h
 * @brief Computes and manages the MIPS stack frame layout for a single function.
 *
 * Scans the IR function to determine stack slot assignments for:
 *   - $ra save slot (always at offset 0)
 *   - AllocaInst space (scalar 4 bytes, array N * elem_bytes, 4-byte aligned)
 *   - Result slots for value-producing instructions (4 bytes each)
 *   - First 4 argument save slots (spilled from $a0-$a3 in prologue)
 *   - Arguments 5+ live in the caller's frame above the current $sp
 */
#pragma once

#include "ir/Function.h"
#include "ir/Instruction.h"
#include <unordered_map>

namespace mips {

    class StackFrame {
    public:
        explicit StackFrame(const ir::Function& func);

        /// Scan the function and compute all stack slot assignments.
        void Build();

        int GetFrameSize() const {
            return frame_size_;
        }

        /// Get the $sp-relative offset for @p val. Asserts if @p val has no slot.
        int GetOffset(const ir::Value* val) const;

        /// Return true if @p val has an assigned stack slot.
        bool HasSlot(const ir::Value* val) const;

    private:
        const ir::Function& func_;
        int frame_size_ = 0;
        std::unordered_map<const ir::Value*, int> value_offset_;

        void AllocateAllocas();
        void AllocateInstructionSlots();
        void AllocateArgumentSlots();

        /// Return true if the instruction produces a value that needs a result slot.
        static bool ProducesValue(const ir::Instruction* inst);
    };

} // namespace mips

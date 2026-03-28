/**
 * @file InstructionEmitter.h
 * @brief IR instruction → MIPS instruction sequence selection.
 *
 * InstructionEmitter owns the instruction-selection logic for one function.
 * It is constructed per-function by FunctionEmitter and given references to
 * the shared AsmWriter and the function's StackFrame.
 *
 * Public interface:
 *   Emit(inst, block)        — dispatch one IR instruction
 *   EmitPhiMovesForEdge(...) — resolve phi-nodes on a pred→succ edge
 *
 * All EmitXxxInst helpers are private; callers only need Emit().
 */
#pragma once

#include "ir/BasicBlock.h"
#include "ir/Function.h"
#include "ir/Instruction.h"
#include "mips/AsmWriter.h"
#include "mips/MipsOptions.h"
#include "mips/StackFrame.h"

namespace mips {

    class InstructionEmitter {
    public:
        /// @p func is needed only for BlockLabel generation in branch emission.
        InstructionEmitter(AsmWriter& writer, const StackFrame& frame, const ir::Function& func,
                           const MipsOptions& options);

        /// Dispatch @p inst to the appropriate EmitXxxInst method.
        /// @p block      is the containing block (needed for phi-move resolution).
        /// @p next_block is the block that immediately follows in emission order,
        ///               or nullptr if this is the last block.  Used by O5 to
        ///               determine whether a branch target is a fall-through.
        void Emit(const ir::Instruction* inst, const ir::BasicBlock* block,
                  const ir::BasicBlock* next_block = nullptr);

        /// Emit stack-based phi-resolution moves for all edges pred→succ
        /// implied by @p branch leaving @p pred_block.
        void EmitPhiMovesForEdge(const ir::BasicBlock* pred_block, const ir::BranchInst* branch);

        /// Load any IR value into @p reg.
        /// Handles: ConstantInt (li), GlobalVar (la), AllocaInst (addiu $sp+offset),
        /// general stack slots (lw).  nullptr is treated as undef → li reg, 0.
        void LoadValueToReg(const ir::Value* val, const std::string& reg);

    private:
        AsmWriter& writer_;
        const StackFrame& frame_;
        const ir::Function& func_;
        const MipsOptions& options_;

        void EmitBinaryInst(const ir::BinaryInst* inst);
        void EmitLoadInst(const ir::LoadInst* inst);
        void EmitStoreInst(const ir::StoreInst* inst);
        void EmitGetElementPtrInst(const ir::GetElementPtrInst* inst);
        void EmitIcmpInst(const ir::IcmpInst* inst);
        /// @p next_block  see Emit(); nullptr means no fall-through candidate.
        void EmitBranchInst(const ir::BranchInst* inst, const ir::BasicBlock* next_block);
        void EmitZextInst(const ir::ZextInst* inst);
        void EmitTruncInst(const ir::TruncInst* inst);
        void EmitReturnInst(const ir::ReturnInst* inst);
        void EmitCallInst(const ir::CallInst* inst);
        void EmitLibraryCall(const ir::CallInst* inst);

        /// Epilogue sequence emitted inline before each ret.
        void EmitEpilogue();
    };

} // namespace mips

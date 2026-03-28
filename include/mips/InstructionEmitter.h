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

#include <string>
#include <unordered_map>
#include <vector>

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

        /// When enable_reg_alloc: move $a0-$a3 (and lw stack args 5+) into vregs.
        void EmitIncomingArguments();

        /// When enable_reg_alloc: allocate a vreg for every phi (block header, Mem2Reg order)
        /// before any instruction is emitted.  Ensures phi operands on critical edges
        /// (e.g. for.step → header) resolve via UseValue even when the defining block
        /// appears later in linear emission order.
        void ReservePhiVRegsForFunction();

        /// Stack slot offset per vreg id for spilled nodes (same as StackFrame slots).
        const std::vector<int>& VRegSpillSlots() const {
            return vreg_spill_slots_;
        }

    private:
        AsmWriter& writer_;
        const StackFrame& frame_;
        const ir::Function& func_;
        const MipsOptions& options_;

        // --- Virtual registers (enable_reg_alloc only) ---
        int next_vreg_id_ = 0;
        std::unordered_map<const ir::Value*, std::string> value_to_vreg_;
        std::vector<int> vreg_spill_slots_;

        std::string AllocVReg();
        std::string EnsureVRegForValue(const ir::Value* val);
        std::string DefValue(const ir::Instruction* inst);
        std::string UseValue(const ir::Value* val);

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

        void EmitBinaryInstVReg(const ir::BinaryInst* inst);
        void EmitLoadInstVReg(const ir::LoadInst* inst);
        void EmitStoreInstVReg(const ir::StoreInst* inst);
        void EmitGetElementPtrInstVReg(const ir::GetElementPtrInst* inst);
        void EmitIcmpInstVReg(const ir::IcmpInst* inst);
        void EmitBranchInstVReg(const ir::BranchInst* inst, const ir::BasicBlock* next_block);
        void EmitZextInstVReg(const ir::ZextInst* inst);
        void EmitTruncInstVReg(const ir::TruncInst* inst);
        void EmitReturnInstVReg(const ir::ReturnInst* inst);
        void EmitCallInstVReg(const ir::CallInst* inst);
        void EmitLibraryCallVReg(const ir::CallInst* inst);

        /// Epilogue sequence emitted inline before each ret.
        void EmitEpilogue();
    };

} // namespace mips

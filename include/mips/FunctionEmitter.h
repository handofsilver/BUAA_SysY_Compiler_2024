/**
 * @file FunctionEmitter.h
 * @brief Per-function MIPS code emitter: prologue, body (instruction selection), epilogue.
 *
 * Orchestrates StackFrame (layout) and instruction emission for one ir::Function.
 * The single public entry point is Emit().
 */
#pragma once

#include "ir/Function.h"
#include "ir/Instruction.h"
#include "mips/StackFrame.h"
#include <ostream>

namespace mips {

    class FunctionEmitter {
    public:
        FunctionEmitter(std::ostream& os, const ir::Function& func);

        /// Build stack frame and emit the complete function (prologue + body).
        void Emit();

    private:
        std::ostream& os_;
        const ir::Function& func_;
        StackFrame frame_;

        // --- Structure ---
        void EmitPrologue();
        void EmitBody();
        void EmitEpilogue();

        // --- Instruction selection ---
        void EmitInstruction(const ir::Instruction* inst, const ir::BasicBlock* block);
        void EmitBinaryInst(const ir::BinaryInst* inst);
        void EmitLoadInst(const ir::LoadInst* inst);
        void EmitStoreInst(const ir::StoreInst* inst);
        void EmitBranchInst(const ir::BranchInst* inst);
        void EmitReturnInst(const ir::ReturnInst* inst);
        void EmitGetElementPtrInst(const ir::GetElementPtrInst* inst);
        void EmitIcmpInst(const ir::IcmpInst* inst);
        void EmitZextInst(const ir::ZextInst* inst);
        void EmitTruncInst(const ir::TruncInst* inst);
        void EmitCallInst(const ir::CallInst* inst);
        void EmitLibraryCall(const ir::CallInst* inst);

        /// Load any ir::Value into the given MIPS register.
        /// Handles ConstantInt (li), GlobalVar (la), AllocaInst (addiu $sp+offset),
        /// and general stack slots (lw).
        void LoadValueToReg(const ir::Value* val, const std::string& reg);

        /// Emit phi-resolution moves for all edges leaving @p pred_block.
        void EmitPhiMovesBeforeBranch(const ir::BasicBlock* pred_block,
                                      const ir::BranchInst* branch);
    };

} // namespace mips

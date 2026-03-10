/**
 * @file FunctionEmitter.h
 * @brief Per-function MIPS code emitter: prologue, body (instruction selection), epilogue.
 *
 * Orchestrates StackFrame (layout) and InstructionEmitter (instruction selection)
 * for one ir::Function.  The single public entry point is Emit().
 */
#pragma once

#include "ir/Function.h"
#include "mips/AsmWriter.h"
#include "mips/InstructionEmitter.h"
#include "mips/MipsOptions.h"
#include "mips/StackFrame.h"

namespace mips {

    class FunctionEmitter {
    public:
        FunctionEmitter(AsmWriter& writer, const ir::Function& func, const MipsOptions& options);

        /// Build stack frame and emit the complete function (prologue + body).
        void Emit();

    private:
        AsmWriter& writer_;
        const ir::Function& func_;
        const MipsOptions& options_;

        StackFrame frame_;
        InstructionEmitter inst_emitter_;

        void EmitPrologue();
        void EmitBody();
    };

} // namespace mips

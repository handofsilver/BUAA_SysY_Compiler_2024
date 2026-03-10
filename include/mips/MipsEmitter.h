/**
 * @file MipsEmitter.h
 * @brief Top-level MIPS assembly emitter: .data segment, .text segment, function dispatch.
 */
#pragma once

#include "ir/Module.h"
#include "mips/AsmWriter.h"
#include "mips/MipsOptions.h"
#include <ostream>

namespace mips {

    class MipsEmitter {
    public:
        MipsEmitter(std::ostream& os, const ir::Module& module, const MipsOptions& options = {});
        void Emit();

    private:
        AsmWriter writer_;
        const ir::Module& module_;
        MipsOptions options_;

        void EmitDataSegment();
        void EmitTextSegment();
    };

} // namespace mips

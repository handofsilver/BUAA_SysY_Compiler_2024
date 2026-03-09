#pragma once

#include "ir/Module.h"
#include <iostream>

namespace mips {

    class MipsEmitter {
    public:
        MipsEmitter(std::ostream& os, const ir::Module& module) : os_(os), module_(module) {}
        void Emit();

    private:
        std::ostream& os_;
        const ir::Module& module_;

        void EmitDataSegment();
        void EmitTextSegment();
    };
} // namespace mips

/**
 * @file RegAlloc.h
 * @brief Graph-coloring register allocator (Chaitin-Briggs).
 *
 * Entry point for the O7 register allocation optimization.
 * Currently implements only the Build phase (liveness analysis +
 * interference graph construction).  Subsequent phases (Simplify,
 * Coalesce, Freeze, Spill, Select) will be added incrementally.
 */
#pragma once

#include "mips/MipsInst.h"

#include <vector>

namespace mips {

    class RegAllocator {
    public:
        /// Run register allocation on the buffered instruction stream.
        /// Called between EmitBody() and FlushBuffer() in FunctionEmitter.
        ///
        /// Currently: runs Build phase (liveness + interference graph) and
        /// dumps debug info to stderr.  Does not yet rewrite instructions.
        static void Run(std::vector<MipsInst>& buffer);
    };

} // namespace mips

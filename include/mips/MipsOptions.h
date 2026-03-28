/**
 * @file MipsOptions.h
 * @brief Compile-time options and optimization switches for the MIPS backend.
 *
 * All flags default to false (optimizations disabled) so that the baseline
 * backend behaves identically to the initial full-stack-allocation version.
 * Enable individual flags from main.cpp to activate optimizations.
 */
#pragma once

namespace mips {

    struct MipsOptions {
        bool emit_comments = false; // Insert MIPS comments for debugging

        // Optimization switches (to be implemented incrementally).
        bool enable_reg_alloc = false;   // Graph-coloring register allocation
        bool enable_peephole = false;    // Peephole optimization on emitted instructions
        bool enable_mul_div_opt = false; // Strength-reduction for multiply/divide
        bool enable_block_merge = false; // Redundant-jump elimination (O5)
    };

} // namespace mips

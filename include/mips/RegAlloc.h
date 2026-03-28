/**
 * @file RegAlloc.h
 * @brief Graph-coloring register allocator (Chaitin-Briggs).
 *
 * Entry point for the O7 register allocation optimization.
 * Build phase (liveness + interference graph) plus Simplify / Select
 * coloring (no Coalesce / Freeze / spill rewrite yet).
 */
#pragma once

#include "mips/LivenessAnalysis.h"

#include <unordered_set>
#include <vector>

namespace mips {

    /// Result of Simplify + Select: palette indices for allocatable nodes.
    ///
    /// For node id @c i:
    ///   - If @c reg_ids.GetName(i) is not allocatable: @c color_by_node[i] is always -1.
    ///   - If allocatable and successfully colored: @c color_by_node[i] in
    ///     @c [0, kNumPaletteColors).
    ///   - If allocatable but Select could not find a color: @c i is in @c actual_spills
    ///     and @c color_by_node[i] is -1.
    struct ColoringResult {
        /// Number of allocatable physical registers ($t0-$t9, $s0-$s7).
        static constexpr int kNumPaletteColors = 18;

        /// Parallel to @c RegIdMap / @c InterferenceGraph node ids.
        std::vector<int> color_by_node;
        /// Allocatable nodes that failed optimistic coloring (Briggs spill).
        std::unordered_set<int> actual_spills;

        /// Number of allocatable nodes that received a color (not spilled).
        int ColoredAllocatableCount(const RegIdMap& reg_ids) const;
    };

    /// Chaitin-Briggs Simplify + potential spill + Select (no Coalesce/Freeze).
    /// Pre-colored (non-allocatable) nodes never enter the stack; they do not consume
    /// palette colors but are ignored during neighbor color collection.
    ColoringResult SimplifyAndSelect(const InterferenceGraph& ig, const RegIdMap& reg_ids);

    class RegAllocator {
    public:
        /// Run register allocation on the buffered instruction stream.
        /// Called between EmitBody() and FlushBuffer() in FunctionEmitter.
        ///
        /// Build + Simplify/Select + rewrite vregs to physical registers.
        /// @param vreg_spill_slots i-th entry is stack offset for @c $vr<i> (-1 if unused).
        /// @param original_frame_size Stack frame size from @c StackFrame::GetFrameSize() before RA
        ///        (matches prologue @c addiu $sp,-F and epilogue @c addiu $sp,+F).  Used so callee-saved
        ///        adjustment does not touch call-site @c addiu $sp,±kExtraArgArea.
        static void Run(std::vector<MipsInst>& buffer, const std::vector<int>& vreg_spill_slots,
                        int original_frame_size);
    };

} // namespace mips

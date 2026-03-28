/**
 * @file RegAlloc.h
 * @brief Graph-coloring register allocator (Chaitin-Briggs, full pipeline).
 *
 * Entry point for the O7 register allocation optimization.
 * Full Chaitin-Briggs pipeline: Build (liveness + interference graph) →
 * Simplify → Coalesce (George criterion) → Freeze → Spill → Select.
 */
#pragma once

#include "mips/LivenessAnalysis.h"

#include <unordered_set>
#include <vector>

namespace mips {

    /// Coloring result: palette index for every node in the interference graph.
    ///
    /// For node id @c i:
    ///   - If not allocatable: @c color_by_node[i] is always -1.
    ///   - If allocatable and successfully colored: @c color_by_node[i] in
    ///     @c [0, kNumPaletteColors).
    ///   - If allocatable but Select could not find a color: @c i is in @c actual_spills
    ///     and @c color_by_node[i] is -1.
    ///   - If coalesced into another node: @c color_by_node[i] equals the
    ///     representative's color (propagated after Select).
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

    /// Full Chaitin-Briggs coloring: Simplify → Coalesce (George) → Freeze → Spill → Select.
    ///
    /// Coalesced nodes inherit their representative's color; trivial MOVE instructions
    /// are eliminated in RewriteBuffer when dst and src map to the same physical register.
    ColoringResult ColorWithCoalesce(const InterferenceGraph& ig, const RegIdMap& reg_ids);

    class RegAllocator {
    public:
        /// Run register allocation on the buffered instruction stream.
        /// Called between EmitBody() and FlushBuffer() in FunctionEmitter.
        ///
        /// Build → ColorWithCoalesce (Simplify/Coalesce/Freeze/Spill/Select) →
        /// rewrite vregs to physical registers → handle callee-saved saves/restores.
        /// @param vreg_spill_slots i-th entry is stack offset for @c $vr<i> (-1 if unused).
        /// @param original_frame_size Stack frame size from @c StackFrame::GetFrameSize() before RA
        ///        (matches prologue @c addiu $sp,-F and epilogue @c addiu $sp,+F).  Used so
        ///        callee-saved adjustment does not touch call-site @c addiu $sp,±kExtraArgArea.
        static void Run(std::vector<MipsInst>& buffer, const std::vector<int>& vreg_spill_slots,
                        int original_frame_size);
    };

} // namespace mips

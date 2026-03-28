/**
 * @file RegAlloc.cpp
 * @brief Graph-coloring register allocator — Build + Simplify + Select.
 *
 * BuildLiveness() constructs the interference graph; SimplifyAndSelect runs
 * Chaitin-Briggs simplification and optimistic coloring.  The instruction
 * buffer is not modified (analysis / debug only).
 */
#include "mips/RegAlloc.h"

#include <cassert>
#include <iostream>
#include <stack>
#include <vector>

namespace mips {

    namespace {

        /// Palette: 10 caller-saved $t + 8 callee-saved $s (see RegIdMap::IsAllocatable).
        const char* const kAllocatableRegNames[ColoringResult::kNumPaletteColors] = {
            "$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7", "$t8",
            "$t9", "$s0", "$s1", "$s2", "$s3", "$s4", "$s5", "$s6", "$s7",
        };

    } // namespace

    int ColoringResult::ColoredAllocatableCount(const RegIdMap& reg_ids) const {
        int n = 0;
        for (int i = 0; i < reg_ids.Size(); ++i) {
            if (RegIdMap::IsAllocatable(reg_ids.GetName(i)) &&
                color_by_node[static_cast<size_t>(i)] >= 0) {
                ++n;
            }
        }
        return n;
    }

    ColoringResult SimplifyAndSelect(const InterferenceGraph& ig, const RegIdMap& reg_ids) {
        const int kNumNodes = reg_ids.Size();
        assert(kNumNodes == ig.NumNodes());

        std::vector<bool> allocatable(static_cast<size_t>(kNumNodes), false);
        for (int i = 0; i < kNumNodes; ++i) {
            allocatable[static_cast<size_t>(i)] = RegIdMap::IsAllocatable(reg_ids.GetName(i));
        }

        std::vector<int> eff_degree(static_cast<size_t>(kNumNodes));
        for (int i = 0; i < kNumNodes; ++i) {
            eff_degree[static_cast<size_t>(i)] = ig.Degree(i);
        }

        std::vector<bool> on_stack(static_cast<size_t>(kNumNodes), false);
        std::stack<int> select_stack;

        auto any_allocatable_left = [&]() {
            for (int i = 0; i < kNumNodes; ++i) {
                if (allocatable[static_cast<size_t>(i)] && !on_stack[static_cast<size_t>(i)]) {
                    return true;
                }
            }
            return false;
        };

        // Push all allocatable nodes onto the stack: Simplify (degree < K) then spill (max degree).
        while (any_allocatable_left()) {
            bool simplified = true;
            while (simplified) {
                simplified = false;
                for (int u = 0; u < kNumNodes; ++u) {
                    if (!allocatable[static_cast<size_t>(u)] || on_stack[static_cast<size_t>(u)]) {
                        continue;
                    }
                    if (eff_degree[static_cast<size_t>(u)] < ColoringResult::kNumPaletteColors) {
                        select_stack.push(u);
                        on_stack[static_cast<size_t>(u)] = true;
                        for (int nb : ig.Neighbors(u)) {
                            if (!on_stack[static_cast<size_t>(nb)]) {
                                --eff_degree[static_cast<size_t>(nb)];
                            }
                        }
                        simplified = true;
                        break; // Re-scan the entire graph for new simplifiable nodes.
                    }
                }
            }

            if (!any_allocatable_left()) {
                break;
            }

            // Potential spill: choose allocatable node with largest effective degree.
            int victim = -1;
            int best_deg = -1;
            for (int u = 0; u < kNumNodes; ++u) {
                if (!allocatable[static_cast<size_t>(u)] || on_stack[static_cast<size_t>(u)]) {
                    continue;
                }
                const int kDeg = eff_degree[static_cast<size_t>(u)];
                if (kDeg > best_deg) {
                    best_deg = kDeg;
                    victim = u;
                }
            }
            assert(victim >= 0);
            select_stack.push(victim);
            on_stack[static_cast<size_t>(victim)] = true;
            for (int nb : ig.Neighbors(victim)) {
                if (!on_stack[static_cast<size_t>(nb)]) {
                    --eff_degree[static_cast<size_t>(nb)];
                }
            }
        }

        // Select: pop stack, assign first free color among already-colored neighbors.
        std::vector<int> color(static_cast<size_t>(kNumNodes), -1);
        std::unordered_set<int> actual_spills;

        while (!select_stack.empty()) {
            const int kPopped = select_stack.top();
            select_stack.pop();

            std::vector<bool> used(static_cast<size_t>(ColoringResult::kNumPaletteColors), false);
            for (int nb : ig.Neighbors(kPopped)) {
                const int kNeighborColor = color[static_cast<size_t>(nb)];
                if (kNeighborColor >= 0 && kNeighborColor < ColoringResult::kNumPaletteColors) {
                    used[static_cast<size_t>(kNeighborColor)] = true;
                }
            }

            int chosen = -1;
            for (int palette_idx = 0; palette_idx < ColoringResult::kNumPaletteColors;
                 ++palette_idx) {
                if (!used[static_cast<size_t>(palette_idx)]) {
                    chosen = palette_idx;
                    break;
                }
            }

            if (chosen >= 0) {
                color[static_cast<size_t>(kPopped)] = chosen;
            } else {
                actual_spills.insert(kPopped);
            }
        }

        ColoringResult out;
        out.color_by_node = std::move(color);
        out.actual_spills = std::move(actual_spills);
        return out;
    }

    void RegAllocator::Run(std::vector<MipsInst>& buffer) {
        // ── Build phase ──────────────────────────────────────────────
        LivenessResult result = BuildLiveness(buffer);

        // ── Debug dump to stderr ─────────────────────────────────────
        const auto& ig = result.ig;
        const auto& reg_ids = result.reg_ids;
        const int kNumRegs = reg_ids.Size();

        std::cerr << "[RegAlloc] Build complete: " << kNumRegs << " registers, "
                  << result.blocks.size() << " blocks\n";

        for (int i = 0; i < kNumRegs; ++i) {
            if (ig.Degree(i) == 0) {
                continue;
            }
            const auto& name = reg_ids.GetName(i);
            std::cerr << "  " << name << " (deg=" << ig.Degree(i) << ")";
            if (RegIdMap::IsAllocatable(name)) {
                std::cerr << " [alloc]";
            }
            std::cerr << ":";
            for (int nb : ig.Neighbors(i)) {
                std::cerr << " " << reg_ids.GetName(nb);
            }
            std::cerr << "\n";
        }

        // Print move pairs.
        const auto& moves = ig.GetMoves();
        if (!moves.empty()) {
            std::cerr << "[RegAlloc] Moves (" << moves.size() << "):\n";
            for (const auto& [dst, src] : moves) {
                std::cerr << "  " << reg_ids.GetName(dst) << " <- " << reg_ids.GetName(src) << "\n";
            }
        }

        ColoringResult cr = SimplifyAndSelect(ig, reg_ids);

        const int kNumColored = cr.ColoredAllocatableCount(reg_ids);
        const int kNumSpilled = static_cast<int>(cr.actual_spills.size());
        std::cerr << "[RegAlloc] Coloring result: " << kNumColored << " colored, " << kNumSpilled
                  << " spilled (K=" << ColoringResult::kNumPaletteColors << ")\n";

        for (int i = 0; i < kNumRegs; ++i) {
            if (!RegIdMap::IsAllocatable(reg_ids.GetName(i))) {
                continue;
            }
            const int kPaletteIdx = cr.color_by_node[static_cast<size_t>(i)];
            if (cr.actual_spills.count(i) != 0u) {
                std::cerr << "  " << reg_ids.GetName(i) << " -> (spilled)\n";
            } else if (kPaletteIdx >= 0 && kPaletteIdx < ColoringResult::kNumPaletteColors) {
                std::cerr << "  " << reg_ids.GetName(i) << " -> "
                          << kAllocatableRegNames[kPaletteIdx] << " (color " << kPaletteIdx
                          << ")\n";
            }
        }
    }

} // namespace mips

/**
 * @file RegAlloc.cpp
 * @brief Graph-coloring register allocator — Build phase stub.
 *
 * Calls BuildLiveness() and dumps a summary of the interference graph
 * to stderr for manual verification.  Does not modify the instruction
 * buffer (pure analysis).
 */
#include "mips/RegAlloc.h"
#include "mips/LivenessAnalysis.h"

#include <iostream>

namespace mips {

    void RegAllocator::Run(std::vector<MipsInst>& buffer) {
        // ── Build phase ──────────────────────────────────────────────
        LivenessResult result = BuildLiveness(buffer);

        // ── Debug dump to stderr ─────────────────────────────────────
        const auto& ig = result.ig;
        const auto& reg_ids = result.reg_ids;
        int n = reg_ids.Size();

        std::cerr << "[RegAlloc] Build complete: " << n << " registers, " << result.blocks.size()
                  << " blocks\n";

        // Print adjacency (only allocatable nodes with edges).
        for (int i = 0; i < n; ++i) {
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
    }

} // namespace mips

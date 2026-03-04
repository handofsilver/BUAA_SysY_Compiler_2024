/**
 * @file DomTree.h
 * @brief Dominator Tree and Dominance Frontier computation.
 *
 * DomTreeInfo stores the immediate dominator (idom) for every block and the
 * children of each node in the dominator tree.
 *
 * DomFrontierInfo stores the dominance frontier DF[B] for every block B.
 * The dominance frontier is the key input to the Mem2Reg phi-insertion step.
 *
 * Algorithm: Keith Cooper's simple iterative dominator algorithm (2001).
 *   - Near-linear in practice for typical CFG sizes.
 *   - Far simpler to implement correctly than Lengauer-Tarjan.
 *
 * Usage:
 *   pass::CFGInfo    cfg = pass::BuildCFG(func);
 *   pass::DomTreeInfo dom = pass::BuildDomTree(func, cfg);
 *   pass::DomFrontierInfo df = pass::ComputeDomFrontier(func, cfg, dom);
 */
#pragma once

#include "pass/CFGBuilder.h"

#include <unordered_map>
#include <vector>

namespace pass {

    /**
     * @brief Dominator tree: idom map + children map.
     *
     * idom[entry] == entry (convention: entry dominates itself).
     * idom[B] == the unique immediate dominator of B for all other blocks.
     * children[A] == all blocks B such that idom[B] == A.
     */
    struct DomTreeInfo {
        using BlockList = std::vector<ir::BasicBlock*>;

        std::unordered_map<ir::BasicBlock*, ir::BasicBlock*> idom;
        std::unordered_map<ir::BasicBlock*, BlockList> children;

        /** @brief True if A strictly dominates B (A dom B and A != B). */
        bool StrictlyDominates(ir::BasicBlock* a, ir::BasicBlock* b) const;

        /** @brief True if A dominates B (A strictly dom B, or A == B). */
        bool Dominates(ir::BasicBlock* a, ir::BasicBlock* b) const;
    };

    /**
     * @brief Dominance frontier: DF[B] for every block B.
     *
     * DF[B] = { Y | ∃ X ∈ preds(Y) such that B dom X, but B does not strictly dom Y }
     *
     * Interpretation: DF[B] is the set of blocks where B's dominance "ends" —
     * exactly the blocks where phi nodes must be inserted for values defined in B.
     */
    struct DomFrontierInfo {
        using BlockList = std::vector<ir::BasicBlock*>;

        std::unordered_map<ir::BasicBlock*, BlockList> df;

        const BlockList& GetDF(ir::BasicBlock* bb) const;

    private:
        friend DomFrontierInfo ComputeDomFrontier(ir::Function&, const CFGInfo&,
                                                  const DomTreeInfo&);
        static const BlockList kEmpty;
    };

    /**
     * @brief Compute the dominator tree of func using Cooper's iterative algorithm.
     *
     * Requires a pre-built CFGInfo (for predecessor lookups).
     * Precondition: func must have at least one basic block (the entry block).
     */
    DomTreeInfo BuildDomTree(ir::Function& func, const CFGInfo& cfg);

    /**
     * @brief Compute the dominance frontier for all blocks in func.
     *
     * Uses the standard O(V+E) algorithm over the dominator tree.
     * Requires both CFGInfo and DomTreeInfo.
     */
    DomFrontierInfo ComputeDomFrontier(ir::Function& func, const CFGInfo& cfg,
                                       const DomTreeInfo& dom);

} // namespace pass

/**
 * @file DomTree.cpp
 * @brief Dominator tree (Cooper algorithm) + dominance frontier.
 *
 * Cooper, Harvey, Kennedy — "A Simple, Fast Dominance Algorithm" (2001).
 *
 * The algorithm works on Reverse Post-Order (RPO) indices.
 * Blocks are numbered 0..N-1 in RPO; entry = index 0.
 *
 * The core operation is Intersect(b1, b2):
 *   Walk both pointers up the (partially-built) idom tree, always advancing
 *   the one with the *larger* RPO index (i.e. the one "lower" in the tree),
 *   until they meet. The meeting point is the LCA in the current idom tree,
 *   which is the common dominator of b1 and b2.
 */

#include "pass/DomTree.h"
#include "ir/Instruction.h" // BranchInst, ReturnInst

#include <cassert>
#include <chrono>
#include <fstream>
#include <stack>
#include <unordered_set>

namespace pass {

    // ---------------------------------------------------------------------------
    // DomFrontierInfo accessors
    // ---------------------------------------------------------------------------

    const DomFrontierInfo::BlockList DomFrontierInfo::kEmpty{};

    const DomFrontierInfo::BlockList& DomFrontierInfo::GetDF(ir::BasicBlock* bb) const {
        auto it = df.find(bb);
        return it != df.end() ? it->second : kEmpty;
    }

    // ---------------------------------------------------------------------------
    // DomTreeInfo helpers
    // ---------------------------------------------------------------------------

    bool DomTreeInfo::Dominates(ir::BasicBlock* a, ir::BasicBlock* b) const {
        // Walk up the idom chain from b; if we reach a, then a dom b.
        ir::BasicBlock* cur = b;
        while (true) {
            if (cur == a) {
                return true;
            }
            auto it = idom.find(cur);
            if (it == idom.end()) {
                return false; // cur has no idom (unreachable?)
            }
            ir::BasicBlock* parent = it->second;
            if (parent == cur) {
                return false; // reached entry without finding a
            }
            cur = parent;
        }
    }

    bool DomTreeInfo::StrictlyDominates(ir::BasicBlock* a, ir::BasicBlock* b) const {
        return a != b && Dominates(a, b);
    }

    // ---------------------------------------------------------------------------
    // RPO computation (iterative DFS to avoid stack overflow on deep CFGs)
    // ---------------------------------------------------------------------------

    /**
     * Compute Reverse Post-Order of blocks reachable from the entry.
     * Returns blocks in RPO order (entry first), and fills rpo_index[bb] = index.
     */
    static std::vector<ir::BasicBlock*>
    ComputeRPO(ir::Function& func, const CFGInfo& cfg,
               std::unordered_map<ir::BasicBlock*, int>& rpo_index) {
        assert(!func.GetBlocks().empty() && "Function has no basic blocks");
        ir::BasicBlock* entry = func.GetBlocks().front().get();

        // Iterative post-order DFS
        std::vector<ir::BasicBlock*> post_order;
        std::unordered_set<ir::BasicBlock*> visited;
        // DFS stack: (block, iterator into its successors)
        using SuccIter = CFGInfo::BlockList::const_iterator;
        std::stack<std::pair<ir::BasicBlock*, SuccIter>> stk;

        visited.insert(entry);
        const CFGInfo::BlockList& entry_succs = cfg.GetSuccs(entry);
        stk.push({entry, entry_succs.begin()});

        while (!stk.empty()) {
            auto& [bb, it] = stk.top();
            const CFGInfo::BlockList& succs = cfg.GetSuccs(bb);

            // Advance iterator to find an unvisited successor
            if (it != succs.end()) {
                ir::BasicBlock* succ = *it;
                ++it;
                if (visited.find(succ) == visited.end()) {
                    visited.insert(succ);
                    const CFGInfo::BlockList& s_succs = cfg.GetSuccs(succ);
                    stk.push({succ, s_succs.begin()});
                }
            } else {
                // All successors visited: record in post_order
                post_order.push_back(bb);
                stk.pop();
            }
        }

        // RPO = reverse of post_order
        std::vector<ir::BasicBlock*> rpo(post_order.rbegin(), post_order.rend());
        for (int i = 0; i < static_cast<int>(rpo.size()); ++i) {
            rpo_index[rpo[i]] = i;
        }
        return rpo;
    }

    // ---------------------------------------------------------------------------
    // Cooper's iterative dominator algorithm
    // ---------------------------------------------------------------------------

    /**
     * Intersect two nodes in the (partially built) idom tree using RPO indices.
     * Walks both pointers upward until they meet; returns the meeting point.
     */
    static ir::BasicBlock*
    Intersect(ir::BasicBlock* b1, ir::BasicBlock* b2,
              const std::unordered_map<ir::BasicBlock*, ir::BasicBlock*>& idom,
              const std::unordered_map<ir::BasicBlock*, int>& rpo_index) {
        while (b1 != b2) {
            while (rpo_index.at(b1) > rpo_index.at(b2)) {
                b1 = idom.at(b1);
            }
            while (rpo_index.at(b2) > rpo_index.at(b1)) {
                b2 = idom.at(b2);
            }
        }
        return b1;
    }

    DomTreeInfo BuildDomTree(ir::Function& func, const CFGInfo& cfg) {
        DomTreeInfo result;
        auto& idom = result.idom;

        std::unordered_map<ir::BasicBlock*, int> rpo_index;
        std::vector<ir::BasicBlock*> rpo = ComputeRPO(func, cfg, rpo_index);
        assert(!rpo.empty() && "RPO should not be empty");

        ir::BasicBlock* entry = rpo[0];
        // Convention: entry's idom is itself.
        idom[entry] = entry;

        // Sentinel: idom[b] is undefined until we set it.
        // We use "absent from the map" as "undefined".

        bool changed = true;
        while (changed) {
            changed = false;

            // Process blocks in RPO order, skip entry (index 0).
            for (size_t i = 1; i < rpo.size(); ++i) {
                ir::BasicBlock* bb = rpo[i];
                const CFGInfo::BlockList& preds = cfg.GetPreds(bb);

                // Find the first processed predecessor (idom already set).
                ir::BasicBlock* new_idom = nullptr;
                for (ir::BasicBlock* pred : preds) {
                    if (idom.count(pred)) {
                        new_idom = pred;
                        break;
                    }
                }
                // A reachable block must have at least one processed predecessor
                // after the first iteration (guaranteed by RPO ordering).
                assert(new_idom != nullptr && "New IDOM should not be nullptr");

                // Merge remaining processed predecessors.
                for (ir::BasicBlock* pred : preds) {
                    if (pred == new_idom) {
                        continue;
                    }
                    if (idom.count(pred)) {
                        new_idom = Intersect(pred, new_idom, idom, rpo_index);
                    }
                }

                if (idom.find(bb) == idom.end() || idom[bb] != new_idom) {
                    idom[bb] = new_idom;
                    changed = true;
                }
            }
        }

        // Build children map by inverting the idom map (skip entry→entry self-edge).
        for (auto& [bb, dom] : idom) {
            if (bb != entry) {
                result.children[dom].push_back(bb);
            }
            // Ensure bb itself has an entry in children (even if empty).
            result.children.try_emplace(bb);
        }

        return result;
    }

    // ---------------------------------------------------------------------------
    // Dominance Frontier
    // ---------------------------------------------------------------------------

    /**
     * Standard O(V+E) DF computation (Cytron et al. via idom tree):
     *
     * For every CFG edge X → Y:
     *   If X does NOT strictly dom Y, then Y is in the DF of every node Z
     *   on the path from X up the idom tree until (but not including) idom(Y).
     *
     * Equivalently (the efficient form):
     *   for each join point Y (|preds(Y)| >= 2, or always iterate all Y):
     *     for each predecessor X of Y:
     *       runner = X
     *       while runner != idom[Y]:
     *           df[runner].insert(Y)
     *           runner = idom[runner]
     */
    DomFrontierInfo ComputeDomFrontier(ir::Function& func, const CFGInfo& cfg,
                                       const DomTreeInfo& dom) {
        DomFrontierInfo result;

        for (auto& bb_ptr : func.GetBlocks()) {
            result.df.try_emplace(bb_ptr.get()); // ensure every block has an entry
        }

        for (auto& bb_ptr : func.GetBlocks()) {
            ir::BasicBlock* y = bb_ptr.get();
            const CFGInfo::BlockList& preds = cfg.GetPreds(y);

            // DF is only non-trivial at join points (>=2 preds) and merge blocks,
            // but iterating all blocks is correct and simpler.
            if (preds.size() < 2) {
                continue;
            }

            // #region agent log
            if (dom.idom.count(y) == 0) {
                std::ofstream f(".cursor/debug-7a78fe.log", std::ios::app);
                if (f) {
                    f << "{\"sessionId\":\"7a78fe\",\"hypothesisId\":\"H2\",\"location\":\"DomTree."
                         "cpp:ComputeDomFrontier\","
                         "\"message\":\"block_not_in_idom\",\"timestamp\":"
                      << std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count()
                      << ",\"data\":{\"ptr\":\"" << static_cast<void*>(y) << "\"}}\n";
                }
            }
            // #endregion
            ir::BasicBlock* idom_y = dom.idom.at(y);

            for (ir::BasicBlock* x : preds) {
                ir::BasicBlock* runner = x;
                // Walk up the idom tree from x, stopping just before idom(y).
                while (runner != idom_y) {
                    result.df[runner].push_back(y);
                    auto it = dom.idom.find(runner);
                    assert(it != dom.idom.end() && "Block missing from idom map");
                    ir::BasicBlock* parent = it->second;
                    // Guard against infinite loop if entry's idom points to itself.
                    if (parent == runner) {
                        break;
                    }
                    runner = parent;
                }
            }
        }

        return result;
    }

} // namespace pass

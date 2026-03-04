/**
 * @file CFGBuilder.h
 * @brief Control Flow Graph (CFG) representation and builder.
 *
 * CFGInfo is a snapshot of a function's CFG: predecessor and successor sets
 * for each BasicBlock. It is computed on demand at the start of a Pass and
 * discarded afterwards — keeping it out of BasicBlock avoids stale-cache bugs
 * when the IR is modified mid-pass.
 *
 * Usage:
 *   pass::CFGInfo cfg = pass::BuildCFG(func);
 *   for (ir::BasicBlock* pred : cfg.GetPreds(bb)) { ... }
 */
#pragma once

#include "ir/Function.h"

#include <unordered_map>
#include <vector>

namespace pass {

    /**
     * @brief Immutable CFG snapshot: predecessor and successor lists per block.
     *
     * All BasicBlock* pointers inside CFGInfo are non-owning; lifetime is bounded
     * by the Function that was passed to BuildCFG.
     */
    class CFGInfo {
    public:
        using BlockList = std::vector<ir::BasicBlock*>;

        /** @brief Return the successor blocks of bb (blocks bb may jump to). */
        const BlockList& GetSuccs(ir::BasicBlock* bb) const;

        /** @brief Return the predecessor blocks of bb (blocks that may jump to bb). */
        const BlockList& GetPreds(ir::BasicBlock* bb) const;

        /** @brief True if bb has at least one predecessor (i.e. it is reachable or is entry). */
        bool HasPreds(ir::BasicBlock* bb) const;

    private:
        friend CFGInfo BuildCFG(ir::Function& func);

        std::unordered_map<ir::BasicBlock*, BlockList> succs_;
        std::unordered_map<ir::BasicBlock*, BlockList> preds_;

        // Returned by GetSuccs/GetPreds when the key is absent.
        static const BlockList kEmpty;
    };

    /**
     * @brief Build a CFG snapshot for func.
     *
     * Precondition: every basic block in func must have a terminator instruction
     * (BranchInst or ReturnInst) as its last instruction. Violated preconditions
     * trigger an assertion failure.
     */
    CFGInfo BuildCFG(ir::Function& func);

} // namespace pass

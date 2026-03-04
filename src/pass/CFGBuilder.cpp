/**
 * @file CFGBuilder.cpp
 * @brief Implementation of CFG construction from a Function's BranchInst operands.
 *
 * Key insight: BasicBlock is a Value, and BranchInst stores its target blocks
 * as Use-tracked operands. This means every BasicBlock's use_list_ already
 * records which BranchInsts (and therefore which blocks) jump to it.
 *
 * We use the straightforward single-pass approach here:
 *   for each block B:
 *     terminator = B.last_instruction   (asserted to be Branch or Return)
 *     for each successor S of terminator:
 *       succs_[B].push_back(S)
 *       preds_[S].push_back(B)
 *
 * This is O(V+E) and avoids dynamic_cast on the use_list.
 */

#include "pass/CFGBuilder.h"
#include "ir/Instruction.h"

#include <cassert>

namespace pass {

    // Static sentinel: returned by GetSuccs/GetPreds for unknown keys.
    const CFGInfo::BlockList CFGInfo::kEmpty{};

    // ---------------------------------------------------------------------------
    // CFGInfo accessors
    // ---------------------------------------------------------------------------

    const CFGInfo::BlockList& CFGInfo::GetSuccs(ir::BasicBlock* bb) const {
        auto it = succs_.find(bb);
        return it != succs_.end() ? it->second : kEmpty;
    }

    const CFGInfo::BlockList& CFGInfo::GetPreds(ir::BasicBlock* bb) const {
        auto it = preds_.find(bb);
        return it != preds_.end() ? it->second : kEmpty;
    }

    bool CFGInfo::HasPreds(ir::BasicBlock* bb) const {
        auto it = preds_.find(bb);
        return it != preds_.end() && !it->second.empty();
    }

    // ---------------------------------------------------------------------------
    // BuildCFG
    // ---------------------------------------------------------------------------

    /**
     * Extract the successor blocks from a terminator instruction.
     * Returns an empty list for ReturnInst (no successors).
     * Asserts that the instruction IS a terminator.
     */
    static CFGInfo::BlockList GetTerminatorSuccs(ir::Instruction* inst) {
        if (auto* br = dynamic_cast<ir::BranchInst*>(inst)) {
            if (br->IsConditional()) {
                return {br->GetIfTrue(), br->GetIfFalse()};
            } else {
                return {br->GetDest()};
            }
        }
        // Must be a ReturnInst — no successors.
        assert(dynamic_cast<ir::ReturnInst*>(inst) != nullptr &&
               "Last instruction of a BasicBlock must be a terminator "
               "(BranchInst or ReturnInst)");
        return {};
    }

    CFGInfo BuildCFG(ir::Function& func) {
        CFGInfo cfg;

        // Seed every block into the maps so that blocks with no preds/succs
        // (e.g. function entry, return blocks) still have empty-list entries.
        for (auto& bb_ptr : func.GetBlocks()) {
            cfg.succs_[bb_ptr.get()];
            cfg.preds_[bb_ptr.get()];
        }

        for (auto& bb_ptr : func.GetBlocks()) {
            ir::BasicBlock* bb = bb_ptr.get();
            auto& insts = bb->GetInstructions();

            assert(!insts.empty() && "BasicBlock must not be empty");

            ir::Instruction* terminator = insts.back().get();
            CFGInfo::BlockList succs = GetTerminatorSuccs(terminator);

            for (ir::BasicBlock* succ : succs) {
                cfg.succs_[bb].push_back(succ);
                cfg.preds_[succ].push_back(bb);
            }
        }

        return cfg;
    }

} // namespace pass

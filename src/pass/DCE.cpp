/**
 * @file DCE.cpp
 * @brief Dead Code Elimination implementation.
 *
 * Section layout:
 *   § Helpers     — EraseFromParent, IsRoot
 *   § Run         — mark-and-sweep over the function
 */

#include "pass/DCE.h"

#include "ir/BasicBlock.h"
#include "ir/Function.h"
#include "ir/Instruction.h"

#include <algorithm>
#include <cassert>
#include <queue>
#include <unordered_set>
#include <vector>

namespace pass {

    // =========================================================================
    // § Helpers
    // =========================================================================

    /**
     * Safely remove an instruction from its parent block.
     *   1. Null out every operand so the instruction is detached from all
     *      use-lists before the object is destroyed.
     *   2. Erase the owning unique_ptr from the parent block's instruction list.
     *
     * Precondition: the instruction's own use-list must be empty (i.e. RAUW has
     * already been performed on its result, or it never had any uses).
     */
    static void EraseFromParent(ir::Instruction* inst) {
        for (int i = 0; i < static_cast<int>(inst->GetNumOperands()); ++i) {
            inst->SetOperand(i, nullptr);
        }
        ir::BasicBlock* bb = inst->GetParent();
        assert(bb && "Instruction has no parent block");
        auto& insts = bb->GetInstructions();
        auto it = std::find_if(insts.begin(), insts.end(),
                               [inst](const auto& up) { return up.get() == inst; });
        assert(it != insts.end() && "Instruction not found in its parent block");
        insts.erase(it);
    }

    /**
     * Returns true if the instruction has side effects and must be treated as
     * a root of the liveness worklist (i.e. it is unconditionally live).
     *
     * Roots:
     *   ReturnInst  — function exit; return value visible to caller.
     *   StoreInst   — writes to memory; observable by loads and call arguments.
     *   CallInst    — may perform I/O, modify globals, or have other side effects.
     *   BranchInst  — determines control flow; removing it would change execution.
     */
    static bool IsRoot(ir::Instruction* inst) {
        return dynamic_cast<ir::ReturnInst*>(inst) || dynamic_cast<ir::StoreInst*>(inst) ||
               dynamic_cast<ir::CallInst*>(inst) || dynamic_cast<ir::BranchInst*>(inst);
    }

    // =========================================================================
    // § Run
    // =========================================================================

    bool DCEPass::Run(ir::Function& func) {
        if (func.GetBlocks().empty()) {
            return false;
        }

        // ── Step 1: Collect all instructions and build a fast membership set.
        // The set is used during BFS to avoid re-marking instructions from other
        // functions (e.g. the Function* operand of CallInst is not an Instruction).
        std::vector<ir::Instruction*> all_insts;
        std::unordered_set<ir::Instruction*> func_inst_set;

        for (auto& bb_uptr : func.GetBlocks()) {
            for (auto& inst_uptr : bb_uptr->GetInstructions()) {
                ir::Instruction* inst = inst_uptr.get();
                all_insts.push_back(inst);
                func_inst_set.insert(inst);
            }
        }

        // ── Step 2: Seed the worklist with root instructions (always live).
        std::unordered_set<ir::Instruction*> live_set;
        std::queue<ir::Instruction*> worklist;

        for (ir::Instruction* inst : all_insts) {
            if (IsRoot(inst)) {
                live_set.insert(inst);
                worklist.push(inst);
            }
        }

        // ── Step 3: BFS backward through the def-use chains.
        // For each live instruction, every operand that is itself an Instruction
        // in this function is also live (its result is consumed by a live use).
        while (!worklist.empty()) {
            ir::Instruction* inst = worklist.front();
            worklist.pop();

            for (size_t i = 0; i < inst->GetNumOperands(); ++i) {
                ir::Value* operand = inst->GetOperand(i);
                if (!operand) {
                    continue;
                }
                // Only propagate to instructions belonging to this function.
                // Constants, Arguments, GlobalVars, and Function* (CallInst callee)
                // are not Instructions and are ignored by the cast.
                auto* def = dynamic_cast<ir::Instruction*>(operand);
                if (!def) {
                    continue;
                }
                if (!func_inst_set.count(def)) {
                    continue;
                }
                if (live_set.insert(def).second) {
                    // Newly marked live: continue propagating from this definition.
                    worklist.push(def);
                }
            }
        }

        // ── Step 4: Collect dead instructions (not in live_set).
        // Gathering into a separate list avoids iterator invalidation when erasing.
        std::vector<ir::Instruction*> to_erase;
        to_erase.reserve(all_insts.size() - live_set.size());

        for (ir::Instruction* inst : all_insts) {
            if (!live_set.count(inst)) {
                to_erase.push_back(inst);
            }
        }

        // ── Step 5: Erase dead instructions.
        // EraseFromParent nulls operands first (detaches from use-lists), then
        // removes the unique_ptr from the parent block (destroying the object).
        for (ir::Instruction* inst : to_erase) {
            EraseFromParent(inst);
        }

        return !to_erase.empty();
    }

} // namespace pass

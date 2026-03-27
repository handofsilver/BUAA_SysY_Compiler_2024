/**
 * @file Mem2Reg.cpp
 * @brief Mem2Reg pass implementation.
 *
 * Section layout:
 *   § Helpers       — EraseFromParent, GetAllocatedType
 *   § Promotability — IsPromotable, CollectPromotableAllocas
 *   § Phi insertion — GetDefBlocks, InsertPhiNodes
 *   § Rename        — RenameContext, RenameBlock (recursive DFS)
 *   § Run           — orchestrate all steps
 */

#include "pass/Mem2Reg.h"
#include "ir/BasicBlock.h"
#include "ir/Function.h"
#include "ir/Instruction.h"
#include "ir/Module.h"
#include "ir/Type.h"
#include "pass/CFGBuilder.h"
#include "pass/DomTree.h"

#include <algorithm>
#include <cassert>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pass {

    // =============================================================================
    // § Helpers
    // =============================================================================

    /**
     * Properly remove an instruction from its parent block:
     *   1. Null out every operand slot so each value's use-list is updated.
     *   2. Erase the unique_ptr from the parent's instruction list.
     * Must only be called on instructions whose own use-list is already empty
     * (i.e. RAUW has been performed on the instruction's result, if any).
     */
    static void EraseFromParent(ir::Instruction* inst) {
        // Detach operands (only meaningful for User subclasses with operands_)
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

    /** Return the element type allocated by an AllocaInst (the T in alloca T). */
    static ir::Type* GetAllocatedType(ir::AllocaInst* alloca) {
        auto* pt = dynamic_cast<ir::PointerType*>(alloca->GetType());
        assert(pt && "AllocaInst must have PointerType");
        return pt->GetPointeeType();
    }

    // =============================================================================
    // § Promotability
    // =============================================================================

    /**
     * An alloca is promotable if:
     *   - Its allocated type is NOT an array (scalar only).
     *   - Every use is either a LoadInst (loading FROM this ptr) or a StoreInst
     *     (storing TO this ptr, not storing the ptr itself).
     * Arrays and address-taken allocas cannot be eliminated by simple Mem2Reg.
     */
    static bool IsPromotable(ir::AllocaInst* alloca) {
        ir::Type* elem_ty = GetAllocatedType(alloca);
        assert(elem_ty && "Allocated type must be non-null");

        // Arrays are not promotable (multi-dimensional or aggregate).
        if (elem_ty->GetTypeId() == ir::TypeID::ARRAY_TY_ID) {
            return false;
        }

        for (ir::Use* use : alloca->GetUseList()) {
            ir::User* user = use->GetUser();
            if (auto* load = dynamic_cast<ir::LoadInst*>(user)) {
                // Load must load from this alloca (operand 0 = pointer).
                if (load->GetPointerOperand() != alloca) {
                    return false;
                }
            } else if (auto* store = dynamic_cast<ir::StoreInst*>(user)) {
                // Store must write TO this alloca (operand 1 = pointer),
                // not store the alloca's address into something else.
                if (store->GetPointerOperand() != alloca) {
                    return false;
                }
            } else {
                // GEP, call argument, etc. — not promotable.
                return false;
            }
        }
        return true;
    }

    static std::vector<ir::AllocaInst*> CollectPromotableAllocas(ir::Function& func) {
        assert(!func.GetBlocks().empty() && "Function must have at least one block");
        std::vector<ir::AllocaInst*> result;

        // Allocas are always emitted in the entry block by our IRGenVisitor.
        auto& entry_insts = func.GetBlocks().front()->GetInstructions();
        for (auto& inst_ptr : entry_insts) {
            if (auto* alloca = dynamic_cast<ir::AllocaInst*>(inst_ptr.get())) {
                if (IsPromotable(alloca)) {
                    result.push_back(alloca);
                }
            } else {
                // Allocas should be at the top of the entry block
                break;
            }
        }
        return result;
    }

    // =============================================================================
    // § Phi insertion
    // =============================================================================

    /** Collect all blocks that contain a StoreInst writing to alloca. */
    static std::vector<ir::BasicBlock*> GetDefBlocks(ir::AllocaInst* alloca) {
        std::vector<ir::BasicBlock*> result;
        for (ir::Use* use : alloca->GetUseList()) {
            if (auto* store = dynamic_cast<ir::StoreInst*>(use->GetUser())) {
                if (store->GetPointerOperand() == alloca) {
                    ir::BasicBlock* bb = store->GetParent();
                    // De-duplicate (a block might have multiple stores to same alloca).
                    if (std::find(result.begin(), result.end(), bb) == result.end()) {
                        result.push_back(bb);
                    }
                }
            }
        }
        return result;
    }

    /**
     * For one alloca, insert empty phi nodes at dominance frontiers.
     * Fills phi_map[alloca][bb] = newly created PhiInst* in bb.
     * Uses the standard iterative worklist algorithm.
     */
    static void
    InsertPhiNodes(ir::AllocaInst* alloca, ir::Function& func, const DomFrontierInfo& df,
                   std::unordered_map<ir::AllocaInst*,
                                      std::unordered_map<ir::BasicBlock*, ir::PhiInst*>>& phi_map) {
        ir::Type* elem_ty = GetAllocatedType(alloca);

        std::vector<ir::BasicBlock*> def_blocks = GetDefBlocks(alloca);

        std::unordered_set<ir::BasicBlock*> has_phi;
        std::unordered_set<ir::BasicBlock*> on_worklist(def_blocks.begin(), def_blocks.end());
        std::queue<ir::BasicBlock*> worklist;
        for (ir::BasicBlock* bb : def_blocks) {
            worklist.push(bb);
        }

        while (!worklist.empty()) {
            ir::BasicBlock* bb = worklist.front();
            worklist.pop();

            for (ir::BasicBlock* cur_bb : df.GetDF(bb)) {
                if (has_phi.count(cur_bb)) {
                    continue;
                }

                // Create an empty phi (0 incoming) at the front of Y.
                auto phi_inst = std::make_unique<ir::PhiInst>("", elem_ty, cur_bb);
                ir::PhiInst* phi_raw = phi_inst.get();
                cur_bb->AddInstructionAtFront(std::move(phi_inst));

                phi_map[alloca][cur_bb] = phi_raw;
                has_phi.insert(cur_bb);

                // phi itself is a new "definition" of alloca in Y.
                if (!on_worklist.count(cur_bb)) {
                    worklist.push(cur_bb);
                    on_worklist.insert(cur_bb);
                }
            }
        }
    }

    // =============================================================================
    // § Rename pass
    // =============================================================================

    /**
     * Bundle of state threaded through the recursive rename DFS.
     * All fields are non-owning references into the surrounding scope.
     */
    struct RenameContext {
        const std::vector<ir::AllocaInst*>& allocas;
        // phi_map[alloca][bb] = PhiInst* inserted for alloca in bb
        const std::unordered_map<ir::AllocaInst*,
                                 std::unordered_map<ir::BasicBlock*, ir::PhiInst*>>& phi_map;
        // phi_to_alloca: reverse lookup (which alloca does this phi represent?)
        const std::unordered_map<ir::PhiInst*, ir::AllocaInst*>& phi_to_alloca;
        const CFGInfo& cfg;
        const DomTreeInfo& dom;
        // current_val[alloca] = stack of SSA values; back() = current definition
        std::unordered_map<ir::AllocaInst*, std::vector<ir::Value*>>& current_val;
        // Instructions to erase after DFS completes (loads and stores)
        std::vector<ir::Instruction*>& to_erase;
        // Optional: when set, use-before-def loads are replaced with constant 0 instead of leaving
        // uses dangling.
        ir::Module* module = nullptr;
    };

    static void RenameBlock(ir::BasicBlock* bb, RenameContext& ctx) {
        // Track how many values we push in this block so we can pop on exit.
        std::unordered_map<ir::AllocaInst*, int> push_count;

        for (auto& inst_ptr : bb->GetInstructions()) {
            ir::Instruction* inst = inst_ptr.get();

            // ── Phi inserted by Mem2Reg: it defines a new value for its alloca.
            if (auto* phi = dynamic_cast<ir::PhiInst*>(inst)) {
                auto it = ctx.phi_to_alloca.find(phi);
                if (it != ctx.phi_to_alloca.end()) {
                    ir::AllocaInst* alloca = it->second;
                    ctx.current_val[alloca].push_back(phi);
                    push_count[alloca]++;
                }
                continue;
            }

            // ── LoadInst from a promotable alloca: replace with current SSA value.
            if (auto* load = dynamic_cast<ir::LoadInst*>(inst)) {
                for (ir::AllocaInst* alloca : ctx.allocas) {
                    if (load->GetPointerOperand() == alloca) {
                        auto& stack = ctx.current_val[alloca];
                        if (!stack.empty()) {
                            load->ReplaceAllUsesWith(stack.back());
                        } else if (ctx.module) {
                            // Use-before-def: replace with constant 0 so erasing the load does not
                            // leave dangling uses.
                            ir::Value* undef_placeholder = nullptr;
                            ir::Type* load_ty = load->GetType();
                            if (load_ty->GetTypeId() == ir::TypeID::INTEGER_TY_ID) {
                                auto* int_ty = static_cast<ir::IntegerType*>(load_ty);
                                undef_placeholder = (int_ty->GetBits() <= 8) ?
                                                        ctx.module->GetInt8Constant(0) :
                                                        ctx.module->GetInt32Constant(0);
                            }
                            if (undef_placeholder) {
                                load->ReplaceAllUsesWith(undef_placeholder);
                            }
                        }
                        // Mark for deletion regardless (even if stack empty = use-before-def).
                        ctx.to_erase.push_back(load);
                        break;
                    }
                }
                continue;
            }

            // ── StoreInst to a promotable alloca: update current value, delete store.
            if (auto* store = dynamic_cast<ir::StoreInst*>(inst)) {
                for (ir::AllocaInst* alloca : ctx.allocas) {
                    if (store->GetPointerOperand() == alloca) {
                        ir::Value* stored_val = store->GetValueOperand();
                        ctx.current_val[alloca].push_back(stored_val);
                        push_count[alloca]++;
                        ctx.to_erase.push_back(store);
                        break;
                    }
                }
                continue;
            }
        }

        // ── Fill incoming values for phi nodes in each CFG successor.
        for (ir::BasicBlock* succ : ctx.cfg.GetSuccs(bb)) {
            for (ir::AllocaInst* alloca : ctx.allocas) {
                auto phi_it = ctx.phi_map.find(alloca);
                // If the alloca is not in the phi_map, it means it generated no phi nodes.
                // (i.e. no control flow is involved)
                if (phi_it == ctx.phi_map.end()) {
                    continue;
                }
                // Only blocks that are in the dominance frontier of this alloca have phi nodes;
                // succ may not have one, so skip it.
                auto bb_it = phi_it->second.find(succ);
                if (bb_it == phi_it->second.end()) {
                    continue;
                }

                ir::PhiInst* phi = bb_it->second;
                auto& stack = ctx.current_val[alloca];
                // Always add an incoming for every predecessor edge, even when the alloca
                // has no current definition on this path (cur == nullptr).  nullptr is
                // printed as "undef" by PhiInst::Print, which produces valid LLVM IR.
                // Skipping the AddIncoming call when cur is null would leave the phi with
                // fewer incomings than the block has predecessors — that is malformed IR.
                ir::Value* cur = stack.empty() ? nullptr : stack.back();
                phi->AddIncoming(cur, bb);
            }
        }

        // ── Recurse into dominator-tree children.
        auto children_it = ctx.dom.children.find(bb);
        if (children_it != ctx.dom.children.end()) {
            for (ir::BasicBlock* child : children_it->second) {
                RenameBlock(child, ctx);
            }
        }

        // ── Restore: pop all values pushed in this block.
        for (auto& [alloca, count] : push_count) {
            auto& stack = ctx.current_val[alloca];
            for (int i = 0; i < count; ++i) {
                stack.pop_back();
            }
        }
    }

    // =============================================================================
    // § Run
    // =============================================================================

    bool Mem2RegPass::Run(ir::Function& func) {
        return Run(func, nullptr);
    }

    bool Mem2RegPass::Run(ir::Function& func, ir::Module* module) {
        if (func.GetBlocks().empty()) {
            return false;
        }

        // ── Step 1–3: Analysis
        CFGInfo cfg = BuildCFG(func);
        DomTreeInfo dom = BuildDomTree(func, cfg);
        DomFrontierInfo df = ComputeDomFrontier(func, cfg, dom);

        // ── Step 4: Collect promotable allocas
        std::vector<ir::AllocaInst*> allocas = CollectPromotableAllocas(func);
        if (allocas.empty()) {
            return false;
        }

        // ── Step 5: Insert phi nodes
        // phi_map[alloca][bb] = the PhiInst inserted for alloca in bb
        std::unordered_map<ir::AllocaInst*, std::unordered_map<ir::BasicBlock*, ir::PhiInst*>>
            phi_map;
        for (ir::AllocaInst* alloca : allocas) {
            InsertPhiNodes(alloca, func, df, phi_map);
        }

        // Build reverse map: phi_inst* → which alloca it was inserted for.
        std::unordered_map<ir::PhiInst*, ir::AllocaInst*> phi_to_alloca;
        for (auto& [alloca, bb_phi] : phi_map) {
            for (auto& [bb, phi] : bb_phi) {
                phi_to_alloca[phi] = alloca;
            }
        }

        // ── Step 6: SSA rename pass (DFS from entry over dominator tree)
        std::unordered_map<ir::AllocaInst*, std::vector<ir::Value*>> current_val;
        for (ir::AllocaInst* alloca : allocas) {
            current_val[alloca] = {}; // empty stack = no current definition yet
        }

        std::vector<ir::Instruction*> to_erase;

        RenameContext ctx{allocas, phi_map, phi_to_alloca, cfg, dom, current_val, to_erase};
        ctx.module = module;
        RenameBlock(func.GetBlocks().front().get(), ctx);

        // ── Step 6b: Finalize phi operands into User::operands_ (def-use chain).
        for (auto& [alloca, bb_phi] : phi_map) {
            for (auto& [bb, phi] : bb_phi) {
                phi->FinalizeOperands();
            }
        }

        // ── Step 7: Erase dead loads and stores collected during rename.
        for (ir::Instruction* inst : to_erase) {
            EraseFromParent(inst);
        }
        // Erase the alloca instructions themselves.
        for (ir::AllocaInst* alloca : allocas) {
            EraseFromParent(alloca);
        }

        return true; // IR was modified
    }

} // namespace pass

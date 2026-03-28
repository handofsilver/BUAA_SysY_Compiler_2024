/**
 * @file LivenessAnalysis.cpp
 * @brief MIPS-level liveness analysis and interference graph construction.
 *
 * Implementation of the "Build" phase for Chaitin-Briggs register allocation.
 * Pipeline: def/use extraction → block partitioning → CFG → block-level
 * liveness (iterative fixpoint) → instruction-level liveness → interference
 * graph construction.
 */
#include "mips/LivenessAnalysis.h"

#include <algorithm>
#include <cassert>

namespace mips {

    // =====================================================================
    // Helper: filter empty strings and $zero from register lists
    // =====================================================================

    static void PushIfValid(std::vector<std::string>& out, const std::string& reg) {
        if (!reg.empty() && reg != "$zero" && reg != "$0") {
            out.push_back(reg);
        }
    }

    // Caller-saved registers clobbered by JAL (function call).
    static const std::vector<std::string>& CallerSavedRegs() {
        static const std::vector<std::string> kRegs = {
            "$v0", "$v1", "$a0", "$a1", "$a2", "$a3", "$t0", "$t1", "$t2",
            "$t3", "$t4", "$t5", "$t6", "$t7", "$t8", "$t9", "$ra",
        };
        return kRegs;
    }

    // =====================================================================
    // GetDefs / GetUses
    // =====================================================================

    std::vector<std::string> GetDefs(const MipsInst& inst) {
        std::vector<std::string> defs;
        switch (inst.op) {
            // R-type: dst = src1 op src2
            case MipsOpcode::ADDU:
            case MipsOpcode::SUBU:
            case MipsOpcode::MUL:
            case MipsOpcode::AND:
            case MipsOpcode::OR:
            case MipsOpcode::SLT:
            case MipsOpcode::SGT:
            case MipsOpcode::SLE:
            case MipsOpcode::SGE:
            case MipsOpcode::SEQ:
            case MipsOpcode::SNE:
            // Shift: dst = src1 << imm
            case MipsOpcode::SLL:
            case MipsOpcode::SRL:
            case MipsOpcode::SRA:
            // MFLO/MFHI: dst = HI or LO
            case MipsOpcode::MFLO:
            case MipsOpcode::MFHI:
            // I-type: dst = src1 op imm
            case MipsOpcode::ADDIU:
            case MipsOpcode::ANDI:
            // Load: dst = mem[src1 + imm]
            case MipsOpcode::LW:
            case MipsOpcode::LBU:
            // Pseudo-load
            case MipsOpcode::LI:
            case MipsOpcode::LA:
            // Register copy
            case MipsOpcode::MOVE: PushIfValid(defs, inst.dst); break;

            // DIV writes HI:LO implicitly — we don't track them as explicit
            // registers; the MFLO/MFHI that follows will define the dst.
            //
            // Store: no register defined.
            //
            // Control flow — no register def (except JAL, handled below).
            case MipsOpcode::DIV:
            case MipsOpcode::SW:
            case MipsOpcode::SB:
            case MipsOpcode::J:
            case MipsOpcode::JR:
            case MipsOpcode::BNEZ:
            case MipsOpcode::BEQZ: break;

            // JAL: clobbers all caller-saved registers
            case MipsOpcode::JAL: defs = CallerSavedRegs(); break;

            // SYSCALL: may write $v0 (return value)
            case MipsOpcode::SYSCALL: defs.push_back("$v0"); break;

            // Non-instructions
            case MipsOpcode::LABEL:
            case MipsOpcode::DIRECTIVE:
            case MipsOpcode::BLANK:
            case MipsOpcode::RAW: break;
        }
        return defs;
    }

    std::vector<std::string> GetUses(const MipsInst& inst) {
        std::vector<std::string> uses;
        switch (inst.op) {
            // R-type: uses src1, src2
            case MipsOpcode::ADDU:
            case MipsOpcode::SUBU:
            case MipsOpcode::MUL:
            case MipsOpcode::AND:
            case MipsOpcode::OR:
            case MipsOpcode::SLT:
            case MipsOpcode::SGT:
            case MipsOpcode::SLE:
            case MipsOpcode::SGE:
            case MipsOpcode::SEQ:
            case MipsOpcode::SNE:
                PushIfValid(uses, inst.src1);
                PushIfValid(uses, inst.src2);
                break;

            // Shift: uses src1 (rt), imm is shift amount
            case MipsOpcode::SLL:
            case MipsOpcode::SRL:
            case MipsOpcode::SRA: PushIfValid(uses, inst.src1); break;

            // DIV: uses src1, src2
            case MipsOpcode::DIV:
                PushIfValid(uses, inst.src1);
                PushIfValid(uses, inst.src2);
                break;

            // MFLO/MFHI: reads HI/LO implicitly — no explicit register use
            case MipsOpcode::MFLO:
            case MipsOpcode::MFHI: break;

            // I-type (ADDIU, ANDI): uses src1
            // Load (LW, LBU): uses base register (src1)
            case MipsOpcode::ADDIU:
            case MipsOpcode::ANDI:
            case MipsOpcode::LW:
            case MipsOpcode::LBU: PushIfValid(uses, inst.src1); break;

            // Store: uses value register (dst field) and base register (src1)
            case MipsOpcode::SW:
            case MipsOpcode::SB:
                PushIfValid(uses, inst.dst);
                PushIfValid(uses, inst.src1);
                break;

            // Pseudo-load: no register use
            case MipsOpcode::LI:
            case MipsOpcode::LA: break;

            // Register copy: uses src1
            case MipsOpcode::MOVE: PushIfValid(uses, inst.src1); break;

            // Unconditional jump (J): no register use.
            //
            // JAL: arguments may be live in $a0-$a3, but we model this
            // conservatively — the caller-saved clobber in GetDefs is the
            // key correctness mechanism.  We don't add $a0-$a3 as uses here
            // because the argument setup instructions before the JAL already
            // make those registers live through normal def/use chains.
            case MipsOpcode::J:
            case MipsOpcode::JAL: break;

            // JR: uses the register (typically $ra)
            //
            // Conditional branch (BNEZ, BEQZ): uses the condition register
            case MipsOpcode::JR:
            case MipsOpcode::BNEZ:
            case MipsOpcode::BEQZ: PushIfValid(uses, inst.src1); break;

            // SYSCALL: uses $v0 (syscall number) and $a0 (argument)
            case MipsOpcode::SYSCALL:
                uses.push_back("$v0");
                uses.push_back("$a0");
                break;

            // Non-instructions
            case MipsOpcode::LABEL:
            case MipsOpcode::DIRECTIVE:
            case MipsOpcode::BLANK:
            case MipsOpcode::RAW: break;
        }
        return uses;
    }

    // =====================================================================
    // RegIdMap
    // =====================================================================

    int RegIdMap::GetOrCreate(const std::string& name) {
        auto it = name_to_id_.find(name);
        if (it != name_to_id_.end()) {
            return it->second;
        }
        int id = static_cast<int>(id_to_name_.size());
        name_to_id_[name] = id;
        id_to_name_.push_back(name);
        return id;
    }

    int RegIdMap::Get(const std::string& name) const {
        return name_to_id_.at(name);
    }

    const std::string& RegIdMap::GetName(int id) const {
        return id_to_name_.at(id);
    }

    bool RegIdMap::IsAllocatable(const std::string& name) {
        // $t0-$t9, $s0-$s7
        if (name.size() < 3 || name[0] != '$') {
            return false;
        }
        char kind = name[1];
        if (kind == 't') {
            // $t0-$t9
            return name.size() == 3 && name[2] >= '0' && name[2] <= '9';
        }
        if (kind == 's') {
            // $s0-$s7
            return name.size() == 3 && name[2] >= '0' && name[2] <= '7';
        }
        return false;
    }

    // =====================================================================
    // Block partitioning + CFG construction
    // =====================================================================

    /// Partition the flat instruction buffer into basic blocks at LABEL boundaries.
    static std::vector<MipsBlock> PartitionBlocks(const std::vector<MipsInst>& buffer) {
        std::vector<MipsBlock> blocks;
        for (int i = 0; i < static_cast<int>(buffer.size()); ++i) {
            if (buffer[i].op == MipsOpcode::LABEL) {
                if (!blocks.empty()) {
                    blocks.back().end = i;
                }
                MipsBlock blk;
                blk.start = i;
                blk.end = static_cast<int>(buffer.size()); // will be updated
                blk.label = buffer[i].label;
                blocks.push_back(std::move(blk));
            }
        }
        // The last block extends to the end of the buffer.
        if (!blocks.empty()) {
            blocks.back().end = static_cast<int>(buffer.size());
        }
        return blocks;
    }

    /// Find the last one or two real instructions at the end of a block.
    /// Returns (last_insn_idx, second_last_insn_idx).  Either may be -1 if
    /// no such instruction exists.
    static std::pair<int, int> FindTerminators(const std::vector<MipsInst>& buffer,
                                               const MipsBlock& block) {
        int last = -1, second_last = -1;
        for (int i = block.end - 1; i >= block.start; --i) {
            if (!buffer[i].IsInsn()) {
                continue;
            }
            if (last == -1) {
                last = i;
            } else {
                second_last = i;
                break;
            }
        }
        return {last, second_last};
    }

    /// Build the CFG: resolve branch/jump targets to block indices, fill
    /// succs/preds.
    static void BuildCFG(std::vector<MipsBlock>& blocks, const std::vector<MipsInst>& buffer) {
        // label name -> block index
        std::unordered_map<std::string, int> label_to_idx;
        for (int i = 0; i < static_cast<int>(blocks.size()); ++i) {
            label_to_idx[blocks[i].label] = i;
        }

        auto add_edge = [&](int from, int to) {
            blocks[from].succs.push_back(to);
            blocks[to].preds.push_back(from);
        };

        for (int bi = 0; bi < static_cast<int>(blocks.size()); ++bi) {
            auto [last, second_last] = FindTerminators(buffer, blocks[bi]);
            if (last == -1) {
                // Empty block (only a LABEL) — fall through to next block.
                if (bi + 1 < static_cast<int>(blocks.size())) {
                    add_edge(bi, bi + 1);
                }
                continue;
            }

            const MipsInst& last_inst = buffer[last];

            if (last_inst.op == MipsOpcode::JR) {
                // Function return — no successors.
                continue;
            }

            if (last_inst.op == MipsOpcode::J) {
                // Could be a standalone unconditional jump, or the false
                // branch following a conditional branch.
                if (second_last != -1) {
                    const MipsInst& prev = buffer[second_last];
                    if (prev.op == MipsOpcode::BNEZ || prev.op == MipsOpcode::BEQZ) {
                        // Pattern: BNEZ/BEQZ target1; J target2
                        auto it1 = label_to_idx.find(prev.label);
                        auto it2 = label_to_idx.find(last_inst.label);
                        if (it1 != label_to_idx.end()) {
                            add_edge(bi, it1->second);
                        }
                        if (it2 != label_to_idx.end()) {
                            add_edge(bi, it2->second);
                        }
                        continue;
                    }
                }
                // Standalone unconditional jump.
                auto it = label_to_idx.find(last_inst.label);
                if (it != label_to_idx.end()) {
                    add_edge(bi, it->second);
                }
                continue;
            }

            if (last_inst.op == MipsOpcode::BNEZ || last_inst.op == MipsOpcode::BEQZ) {
                // Conditional branch with fall-through (O5 optimization).
                auto it = label_to_idx.find(last_inst.label);
                if (it != label_to_idx.end()) {
                    add_edge(bi, it->second);
                }
                if (bi + 1 < static_cast<int>(blocks.size())) {
                    add_edge(bi, bi + 1);
                }
                continue;
            }

            // No explicit terminator — fall through to next block.
            if (bi + 1 < static_cast<int>(blocks.size())) {
                add_edge(bi, bi + 1);
            }
        }
    }

    // =====================================================================
    // Block-level liveness (iterative dataflow fixpoint)
    // =====================================================================

    /// Compute def[B] and use[B] for each block.
    static void ComputeBlockDefUse(const std::vector<MipsInst>& buffer,
                                   const std::vector<MipsBlock>& blocks, RegIdMap& reg_ids,
                                   std::vector<std::unordered_set<int>>& block_def,
                                   std::vector<std::unordered_set<int>>& block_use) {
        int n = static_cast<int>(blocks.size());
        block_def.resize(n);
        block_use.resize(n);

        for (int bi = 0; bi < n; ++bi) {
            const auto& blk = blocks[bi];
            for (int i = blk.start; i < blk.end; ++i) {
                if (!buffer[i].IsInsn()) {
                    continue;
                }
                // Uses before defs within the block.
                for (const auto& u : GetUses(buffer[i])) {
                    int uid = reg_ids.GetOrCreate(u);
                    if (block_def[bi].count(uid) == 0) {
                        block_use[bi].insert(uid);
                    }
                }
                for (const auto& d : GetDefs(buffer[i])) {
                    int did = reg_ids.GetOrCreate(d);
                    block_def[bi].insert(did);
                }
            }
        }
    }

    /// Compute reverse postorder via DFS from block 0.
    static std::vector<int> ComputeReversePostorder(const std::vector<MipsBlock>& blocks) {
        int n = static_cast<int>(blocks.size());
        if (n == 0) {
            return {};
        }

        std::vector<bool> visited(n, false);
        std::vector<int> postorder;
        postorder.reserve(n);

        // Iterative DFS to avoid deep recursion.
        struct Frame {
            int block;
            int child_idx;
        };
        std::vector<Frame> stack;
        stack.push_back({0, 0});
        visited[0] = true;

        while (!stack.empty()) {
            auto& top = stack.back();
            const auto& succs = blocks[top.block].succs;
            if (top.child_idx < static_cast<int>(succs.size())) {
                int child = succs[top.child_idx++];
                if (!visited[child]) {
                    visited[child] = true;
                    stack.push_back({child, 0});
                }
            } else {
                postorder.push_back(top.block);
                stack.pop_back();
            }
        }

        std::reverse(postorder.begin(), postorder.end());
        return postorder; // this is now reverse postorder
    }

    /// Run the iterative dataflow fixpoint for block-level liveness.
    static void ComputeBlockLiveness(const std::vector<MipsBlock>& blocks,
                                     const std::vector<std::unordered_set<int>>& block_def,
                                     const std::vector<std::unordered_set<int>>& block_use,
                                     std::vector<std::unordered_set<int>>& live_in,
                                     std::vector<std::unordered_set<int>>& live_out) {
        int n = static_cast<int>(blocks.size());
        live_in.resize(n);
        live_out.resize(n);

        // For liveness (backward problem), iterate in reverse of reverse
        // postorder, i.e., postorder.  But the standard approach is to iterate
        // in reverse postorder checking for changes; for a backward dataflow
        // we actually want postorder.  We'll iterate the RPO in reverse.
        std::vector<int> rpo = ComputeReversePostorder(blocks);

        bool changed = true;
        while (changed) {
            changed = false;
            // Iterate in reverse RPO = postorder (good for backward problems).
            for (int idx = static_cast<int>(rpo.size()) - 1; idx >= 0; --idx) {
                int bi = rpo[idx];
                // out[B] = ∪ in[S] for S in succs(B)
                std::unordered_set<int> new_out;
                for (int s : blocks[bi].succs) {
                    for (int r : live_in[s]) {
                        new_out.insert(r);
                    }
                }
                // in[B] = use[B] ∪ (out[B] - def[B])
                std::unordered_set<int> new_in = block_use[bi];
                for (int r : new_out) {
                    if (block_def[bi].count(r) == 0) {
                        new_in.insert(r);
                    }
                }
                if (new_in != live_in[bi]) {
                    changed = true;
                    live_in[bi] = std::move(new_in);
                }
                live_out[bi] = std::move(new_out);
            }
        }
    }

    // =====================================================================
    // Instruction-level liveness
    // =====================================================================

    /// For each instruction index, compute the live-out set (registers live
    /// immediately after that instruction executes).
    static std::vector<std::unordered_set<int>> ComputeInstructionLiveness(
        const std::vector<MipsInst>& buffer, const std::vector<MipsBlock>& blocks,
        const std::vector<std::unordered_set<int>>& block_live_out, RegIdMap& reg_ids) {
        std::vector<std::unordered_set<int>> inst_live_out(buffer.size());

        for (int bi = 0; bi < static_cast<int>(blocks.size()); ++bi) {
            const auto& blk = blocks[bi];
            std::unordered_set<int> live = block_live_out[bi];

            for (int i = blk.end - 1; i >= blk.start; --i) {
                if (!buffer[i].IsInsn()) {
                    continue;
                }

                // Record live-out for this instruction point.
                inst_live_out[i] = live;

                // Remove defs, add uses.
                for (const auto& d : GetDefs(buffer[i])) {
                    live.erase(reg_ids.GetOrCreate(d));
                }
                for (const auto& u : GetUses(buffer[i])) {
                    live.insert(reg_ids.GetOrCreate(u));
                }
            }
        }
        return inst_live_out;
    }

    // =====================================================================
    // Interference graph construction
    // =====================================================================

    InterferenceGraph::InterferenceGraph(int num_nodes) :
    num_nodes_(num_nodes),
    adj_(num_nodes),
    degree_(num_nodes, 0) {}

    void InterferenceGraph::AddEdge(int u, int v) {
        if (u == v) {
            return;
        }
        if (adj_[u].count(v)) {
            return; // already exists
        }
        adj_[u].insert(v);
        adj_[v].insert(u);
        ++degree_[u];
        ++degree_[v];
    }

    bool InterferenceGraph::HasEdge(int u, int v) const {
        return adj_[u].count(v) > 0;
    }

    int InterferenceGraph::Degree(int node) const {
        return degree_[node];
    }

    const std::unordered_set<int>& InterferenceGraph::Neighbors(int node) const {
        return adj_[node];
    }

    void InterferenceGraph::AddMove(int dst, int src) {
        moves_.emplace_back(dst, src);
        move_related_.insert(dst);
        move_related_.insert(src);
    }

    bool InterferenceGraph::IsMoveRelated(int node) const {
        return move_related_.count(node) > 0;
    }

    /// Build the interference graph from instruction-level liveness data.
    static InterferenceGraph
    BuildInterferenceGraph(const std::vector<MipsInst>& buffer,
                           const std::vector<std::unordered_set<int>>& inst_live_out,
                           RegIdMap& reg_ids) {
        InterferenceGraph ig(reg_ids.Size());

        for (int i = 0; i < static_cast<int>(buffer.size()); ++i) {
            if (!buffer[i].IsInsn()) {
                continue;
            }

            auto defs = GetDefs(buffer[i]);
            bool is_move = (buffer[i].op == MipsOpcode::MOVE);

            // For MOVE, record the move pair.
            if (is_move && !buffer[i].dst.empty() && !buffer[i].src1.empty()) {
                int dst_id = reg_ids.GetOrCreate(buffer[i].dst);
                int src_id = reg_ids.GetOrCreate(buffer[i].src1);
                if (dst_id != src_id) {
                    ig.AddMove(dst_id, src_id);
                }
            }

            // For each def d, add edges to all live-out registers except:
            //   - d itself (no self-loops)
            //   - for MOVE: the source register (move-related, no edge)
            for (const auto& d : defs) {
                int d_id = reg_ids.GetOrCreate(d);
                for (int r : inst_live_out[i]) {
                    if (r == d_id) {
                        continue;
                    }
                    if (is_move && !buffer[i].src1.empty() &&
                        r == reg_ids.GetOrCreate(buffer[i].src1)) {
                        continue;
                    }
                    ig.AddEdge(d_id, r);
                }
            }
        }
        return ig;
    }

    // =====================================================================
    // BuildLiveness — orchestrator
    // =====================================================================

    LivenessResult BuildLiveness(const std::vector<MipsInst>& buffer) {
        // 1. Partition into basic blocks.
        auto blocks = PartitionBlocks(buffer);

        // 2. Build the CFG.
        BuildCFG(blocks, buffer);

        // 3. Assign integer IDs to all registers and compute block def/use.
        RegIdMap reg_ids;
        std::vector<std::unordered_set<int>> block_def, block_use;
        ComputeBlockDefUse(buffer, blocks, reg_ids, block_def, block_use);

        // 4. Block-level liveness fixpoint.
        std::vector<std::unordered_set<int>> live_in, live_out;
        ComputeBlockLiveness(blocks, block_def, block_use, live_in, live_out);

        // 5. Instruction-level liveness.
        auto inst_live_out = ComputeInstructionLiveness(buffer, blocks, live_out, reg_ids);

        // 6. Build the interference graph.
        auto ig = BuildInterferenceGraph(buffer, inst_live_out, reg_ids);

        return LivenessResult(std::move(reg_ids), std::move(blocks), std::move(ig));
    }

} // namespace mips

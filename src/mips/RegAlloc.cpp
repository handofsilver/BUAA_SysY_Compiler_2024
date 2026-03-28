/**
 * @file RegAlloc.cpp
 * @brief Graph-coloring register allocator — Build, Simplify/Select, buffer rewrite.
 */
#include "mips/RegAlloc.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <list>
#include <numeric>
#include <set>
#include <stack>
#include <unordered_set>
#include <vector>

namespace mips {

    namespace {

        const char* const kAllocatableRegNames[ColoringResult::kNumPaletteColors] = {
            "$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7", "$t8",
            "$t9", "$s0", "$s1", "$s2", "$s3", "$s4", "$s5", "$s6", "$s7",
        };

        int ParseVRegSuffix(const std::string& name) {
            if (!RegIdMap::IsVirtual(name)) {
                return -1;
            }
            int v = 0;
            for (size_t i = 3; i < name.size(); ++i) {
                assert(std::isdigit(static_cast<unsigned char>(name[i])));
                v = v * 10 + (name[i] - '0');
            }
            return v;
        }

        static MipsInst MakeLw(const std::string& dst, int offset, const std::string& base) {
            MipsInst m;
            m.op = MipsOpcode::LW;
            m.dst = dst;
            m.src1 = base;
            m.imm = offset;
            return m;
        }

        static MipsInst MakeSw(const std::string& src, int offset, const std::string& base) {
            MipsInst m;
            m.op = MipsOpcode::SW;
            m.dst = src;
            m.src1 = base;
            m.imm = offset;
            return m;
        }

        /// True if @p reg is $s0-$s7 after rewrite.
        bool IsCalleeSavedReg(const std::string& reg) {
            return reg.size() == 3 && reg[0] == '$' && reg[1] == 's' && reg[2] >= '0' &&
                   reg[2] <= '7';
        }

    } // namespace

    int ColoringResult::ColoredAllocatableCount(const RegIdMap& reg_ids) const {
        int n = 0;
        for (int i = 0; i < reg_ids.Size(); ++i) {
            const std::string& nm = reg_ids.GetName(i);
            if (RegIdMap::IsVirtual(nm) && actual_spills.count(i) == 0u &&
                color_by_node[static_cast<size_t>(i)] >= 0) {
                ++n;
            }
        }
        return n;
    }

    /// Full Chaitin-Briggs coloring: Simplify → Coalesce (George criterion) → Freeze →
    /// Spill (optimistic) → Select.  Replaces the simpler SimplifyAndSelect.
    ///
    /// Key invariants maintained throughout:
    ///   - adj_work[u]: working adjacency; extended when nodes are merged via Coalesce.
    ///   - eff_degree[u]: count of active (not on_stack, not coalesced) neighbors of u.
    ///   - alias[u]: union-find representative (path-halving compression).
    ///   - pending_moves: moves not yet resolved; entries are erased when coalesced,
    ///     constrained, frozen, or otherwise invalidated.
    ColoringResult ColorWithCoalesce(const InterferenceGraph& ig, const RegIdMap& reg_ids) {
        const int kN = reg_ids.Size();
        const int kK = ColoringResult::kNumPaletteColors;
        assert(kN == ig.NumNodes());

        // ── Pre-color physical registers ($t0-$t9: indices 0–9, $s0-$s7: 10–17) ─────
        std::vector<int> color(static_cast<size_t>(kN), -1);
        std::vector<bool> precolored(static_cast<size_t>(kN), false);
        for (int i = 0; i < kN; ++i) {
            const int kPal = RegIdMap::PaletteIndexOf(reg_ids.GetName(i));
            if (kPal >= 0) {
                color[static_cast<size_t>(i)] = kPal;
                precolored[static_cast<size_t>(i)] = true;
            }
        }

        // Candidate = allocatable virtual register ($vr*) that needs a color assigned.
        // Physical registers are either precolored ($t/$s) or non-candidates ($v0, $sp…).
        auto is_cand = [&](int u) -> bool {
            return !precolored[static_cast<size_t>(u)] &&
                   RegIdMap::IsAllocatable(reg_ids.GetName(u));
        };

        // ── Mutable working graph ─────────────────────────────────────────────────────
        std::vector<std::unordered_set<int>> adj_work(static_cast<size_t>(kN));
        std::vector<int> eff_degree(static_cast<size_t>(kN), 0);
        for (int u = 0; u < kN; ++u) {
            adj_work[static_cast<size_t>(u)] = ig.Neighbors(u);
            eff_degree[static_cast<size_t>(u)] = ig.Degree(u);
        }

        std::vector<bool> on_stack(static_cast<size_t>(kN), false);
        std::vector<bool> coalesced_flag(static_cast<size_t>(kN), false);
        std::vector<int> alias(static_cast<size_t>(kN));
        std::iota(alias.begin(), alias.end(), 0); // alias[i] = i initially
        std::stack<int> sel_stack;

        // Pending moves: not yet coalesced, constrained, or frozen.
        // Entries are erased as they become resolved.
        std::list<std::pair<int, int>> pending_moves(ig.GetMoves().begin(), ig.GetMoves().end());

        // ── Lambdas ───────────────────────────────────────────────────────────────────

        // Union-find with path halving: follows alias chain to its root.
        auto get_alias = [&](int u) -> int {
            while (alias[static_cast<size_t>(u)] != u) {
                // Skip one level per step (path halving).
                alias[static_cast<size_t>(u)] =
                    alias[static_cast<size_t>(alias[static_cast<size_t>(u)])];
                u = alias[static_cast<size_t>(u)];
            }
            return u;
        };

        // Active (non-stacked, non-coalesced) neighbors of u, alias-resolved and
        // deduplicated.  Deduplication is necessary because adj_work may contain stale
        // raw IDs that now share the same representative after prior Coalesce steps.
        auto active_adj = [&](int u) -> std::vector<int> {
            std::unordered_set<int> seen;
            std::vector<int> result;
            for (int t_raw : adj_work[static_cast<size_t>(u)]) {
                const int kT = get_alias(t_raw);
                if (kT != u && !on_stack[static_cast<size_t>(kT)] &&
                    !coalesced_flag[static_cast<size_t>(kT)] && seen.insert(kT).second) {
                    result.push_back(kT);
                }
            }
            return result;
        };

        // Set of candidate nodes that appear in at least one pending move.
        // Recomputed fresh each time to avoid stale move-related state.
        auto move_related_set = [&]() -> std::unordered_set<int> {
            std::unordered_set<int> mr;
            for (const auto& mv : pending_moves) {
                for (int raw : {mv.first, mv.second}) {
                    const int kA = get_alias(raw);
                    if (is_cand(kA) && !on_stack[static_cast<size_t>(kA)] &&
                        !coalesced_flag[static_cast<size_t>(kA)]) {
                        mr.insert(kA);
                    }
                }
            }
            return mr;
        };

        // Decrement eff_degree of active candidate node m (it lost one active neighbor).
        auto decrement_degree = [&](int m) {
            const int kM = get_alias(m);
            if (!is_cand(kM) || on_stack[static_cast<size_t>(kM)] ||
                coalesced_flag[static_cast<size_t>(kM)]) {
                return;
            }
            --eff_degree[static_cast<size_t>(kM)];
        };

        // George OK(t, u): safe to add edge (t, u) without harming colorability.
        // True if t has low degree, t is precolored, or t already interferes with u.
        auto george_ok = [&](int t, int u) -> bool {
            const int kT = get_alias(t);
            return eff_degree[static_cast<size_t>(kT)] < kK ||
                   precolored[static_cast<size_t>(kT)] ||
                   adj_work[static_cast<size_t>(u)].count(kT) > 0;
        };

        // George criterion: all active neighbors of v satisfy george_ok(·, u).
        // If true, merging v into u is safe (cannot make the graph harder to color).
        auto george = [&](int u, int v) -> bool {
            for (int t : active_adj(v)) {
                if (!george_ok(t, u)) {
                    return false;
                }
            }
            return true;
        };

        // Merge v into u: v is absorbed (coalesced_flag set), u survives.
        // For each neighbor t of v:
        //   - Add edge (u, t) if missing → both gain one active neighbor.
        //   - Decrement t's eff_degree → t loses v as an active neighbor.
        // Net eff_degree change for t: 0 if (u,t) is new, -1 if (u,t) already existed.
        auto combine = [&](int u, int v) {
            coalesced_flag[static_cast<size_t>(v)] = true;
            alias[static_cast<size_t>(v)] = u;
            for (int t_raw : adj_work[static_cast<size_t>(v)]) {
                const int kT = get_alias(t_raw);
                if (kT == u || kT == v) {
                    continue;
                }
                const bool kIsNewEdge = adj_work[static_cast<size_t>(u)].insert(kT).second;
                if (kIsNewEdge) {
                    adj_work[static_cast<size_t>(kT)].insert(u);
                    // Both sides gain a new active neighbor.
                    if (!precolored[static_cast<size_t>(u)] && !on_stack[static_cast<size_t>(u)] &&
                        !coalesced_flag[static_cast<size_t>(u)]) {
                        ++eff_degree[static_cast<size_t>(u)];
                    }
                    if (!precolored[static_cast<size_t>(kT)] &&
                        !on_stack[static_cast<size_t>(kT)] &&
                        !coalesced_flag[static_cast<size_t>(kT)]) {
                        ++eff_degree[static_cast<size_t>(kT)];
                    }
                }
                // v is leaving the active graph; kT loses it as an active neighbor.
                decrement_degree(kT);
            }
        };

        // ── Main coloring loop ─────────────────────────────────────────────────────
        //
        // Each outer iteration runs exactly one of four phases (highest-priority first):
        //   1. Simplify: push one non-move-related, degree<K node → guaranteed colorable.
        //   2. Coalesce: try George criterion on pending moves; restart after one success.
        //   3. Freeze:   give up on coalescing for one low-degree move-related node.
        //   4. Spill:    optimistically push the highest-degree node (Briggs may rescue).
        //
        // Termination: each iteration removes ≥1 node from the active set (Simplify/Spill)
        // or eliminates ≥1 pending move (Coalesce/Freeze).
        while (true) {
            // Are there any active candidates left to process?
            {
                bool any_left = false;
                for (int u = 0; u < kN; ++u) {
                    if (is_cand(u) && !on_stack[static_cast<size_t>(u)] &&
                        !coalesced_flag[static_cast<size_t>(u)]) {
                        any_left = true;
                        break;
                    }
                }
                if (!any_left) {
                    break;
                }
            }

            // ── Phase 1: Simplify ─────────────────────────────────────────────────
            // Non-move-related candidates with degree < K are guaranteed colorable by the
            // pigeonhole principle; push one and let the degree cascade restart the loop.
            {
                bool simplified = false;
                const auto kMoveRel = move_related_set();
                for (int u = 0; u < kN; ++u) {
                    if (!is_cand(u) || on_stack[static_cast<size_t>(u)] ||
                        coalesced_flag[static_cast<size_t>(u)] || kMoveRel.count(u)) {
                        continue;
                    }
                    if (eff_degree[static_cast<size_t>(u)] < kK) {
                        sel_stack.push(u);
                        on_stack[static_cast<size_t>(u)] = true;
                        for (int t : active_adj(u)) {
                            decrement_degree(t);
                        }
                        simplified = true;
                        break; // restart: degree decrements may unlock more nodes
                    }
                }
                if (simplified) {
                    continue;
                }
            }

            // ── Phase 2: Coalesce (George criterion) ──────────────────────────────
            // First, remove pending moves that are now trivially resolved or permanently
            // blocked (same alias, stale, both precolored, interfering sides, or involve
            // non-allocatable physical registers like $v0/$a0 that carry no palette color).
            {
                auto it = pending_moves.begin();
                while (it != pending_moves.end()) {
                    const int kX = get_alias(it->first);
                    const int kY = get_alias(it->second);
                    const bool kXGone = on_stack[static_cast<size_t>(kX)] ||
                                        coalesced_flag[static_cast<size_t>(kX)];
                    const bool kYGone = on_stack[static_cast<size_t>(kY)] ||
                                        coalesced_flag[static_cast<size_t>(kY)];
                    // Moves involving non-allocatable physical registers ($v0, $a0, $sp …)
                    // cannot be coalesced: no palette color can be assigned to such a node.
                    const bool kXInvalid = !is_cand(kX) && !precolored[static_cast<size_t>(kX)];
                    const bool kYInvalid = !is_cand(kY) && !precolored[static_cast<size_t>(kY)];
                    if (kX == kY || kXGone || kYGone ||
                        (precolored[static_cast<size_t>(kX)] &&
                         precolored[static_cast<size_t>(kY)]) ||
                        adj_work[static_cast<size_t>(kX)].count(kY) > 0 || kXInvalid || kYInvalid) {
                        it = pending_moves.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
            // Try George coalescing on each surviving move.
            {
                bool coalesced_one = false;
                for (auto it = pending_moves.begin(); it != pending_moves.end(); ++it) {
                    int u = get_alias(it->first);
                    int v = get_alias(it->second);
                    // Convention: if v is precolored (fixed color), swap so u is the
                    // fixed-color survivor.  This preserves precolored nodes' palette slots.
                    if (precolored[static_cast<size_t>(v)]) {
                        std::swap(u, v);
                    }
                    // Try George with u surviving (checks v's neighbors).
                    if (george(u, v)) {
                        pending_moves.erase(it);
                        combine(u, v);
                        coalesced_one = true;
                        break;
                    }
                    // For two non-precolored virtual nodes, also try the reversed direction.
                    if (!precolored[static_cast<size_t>(u)] && george(v, u)) {
                        pending_moves.erase(it);
                        combine(v, u); // v survives, u is absorbed
                        coalesced_one = true;
                        break;
                    }
                }
                if (coalesced_one) {
                    continue; // restart: new simplifiable nodes may have appeared
                }
            }

            // ── Phase 3: Freeze ───────────────────────────────────────────────────
            // No pending move can be coalesced right now.  Find a low-degree move-related
            // candidate, discard its pending moves, and let Simplify handle it next round.
            {
                bool frozen = false;
                const auto kMoveRel = move_related_set();
                for (int u = 0; u < kN; ++u) {
                    if (!is_cand(u) || on_stack[static_cast<size_t>(u)] ||
                        coalesced_flag[static_cast<size_t>(u)]) {
                        continue;
                    }
                    if (kMoveRel.count(u) && eff_degree[static_cast<size_t>(u)] < kK) {
                        // Remove all pending moves that reference u.
                        auto mit = pending_moves.begin();
                        while (mit != pending_moves.end()) {
                            const int kA = get_alias(mit->first);
                            const int kB = get_alias(mit->second);
                            if (kA == u || kB == u) {
                                mit = pending_moves.erase(mit);
                            } else {
                                ++mit;
                            }
                        }
                        frozen = true;
                        break; // u is now non-move-related and will be simplified next round
                    }
                }
                if (frozen) {
                    continue;
                }
            }

            // ── Phase 4: Spill (optimistic Briggs) ────────────────────────────────
            // All remaining candidates have degree ≥ K.  Select the highest-degree node as
            // a potential spill: remove its moves, then push it optimistically.
            // Briggs' Select phase may still find a valid color (not all neighbors may use
            // distinct colors), so this does not guarantee an actual spill.
            {
                int victim = -1;
                int best_deg = -1;
                for (int u = 0; u < kN; ++u) {
                    if (!is_cand(u) || on_stack[static_cast<size_t>(u)] ||
                        coalesced_flag[static_cast<size_t>(u)]) {
                        continue;
                    }
                    if (eff_degree[static_cast<size_t>(u)] > best_deg) {
                        best_deg = eff_degree[static_cast<size_t>(u)];
                        victim = u;
                    }
                }
                assert(victim >= 0);
                // Remove victim's pending moves to unblock its neighbors.
                {
                    auto mit = pending_moves.begin();
                    while (mit != pending_moves.end()) {
                        const int kA = get_alias(mit->first);
                        const int kB = get_alias(mit->second);
                        if (kA == victim || kB == victim) {
                            { mit = pending_moves.erase(mit); }
                        } else {
                            ++mit;
                        }
                    }
                }
                sel_stack.push(victim);
                on_stack[static_cast<size_t>(victim)] = true;
                for (int t : active_adj(victim)) {
                    decrement_degree(t);
                }
            }
        }

        // ── Select: assign colors from the stack (Briggs optimistic coloring) ───────
        std::unordered_set<int> actual_spills;
        while (!sel_stack.empty()) {
            const int kU = sel_stack.top();
            sel_stack.pop();
            // Precolored nodes never enter the stack; guard for robustness.
            if (precolored[static_cast<size_t>(kU)]) {
                continue;
            }

            // Collect colors already used by neighbors (using alias to handle coalesced nodes).
            std::vector<bool> used(static_cast<size_t>(kK), false);
            for (int nb_raw : adj_work[static_cast<size_t>(kU)]) {
                const int kNb = get_alias(nb_raw);
                const int kNbColor = color[static_cast<size_t>(kNb)];
                if (kNbColor >= 0 && kNbColor < kK) {
                    used[static_cast<size_t>(kNbColor)] = true;
                }
            }
            // Assign the first available palette color.
            int chosen = -1;
            for (int c = 0; c < kK; ++c) {
                if (!used[static_cast<size_t>(c)]) {
                    chosen = c;
                    break;
                }
            }
            if (chosen >= 0) {
                color[static_cast<size_t>(kU)] = chosen;
            } else {
                actual_spills.insert(kU); // optimistic coloring failed → actual spill
            }
        }

        // Propagate colors to coalesced nodes: they share their representative's color.
        for (int i = 0; i < kN; ++i) {
            if (coalesced_flag[static_cast<size_t>(i)]) {
                color[static_cast<size_t>(i)] = color[static_cast<size_t>(get_alias(i))];
            }
        }

        ColoringResult out;
        out.color_by_node = std::move(color);
        out.actual_spills = std::move(actual_spills);
        return out;
    }

    static std::string MapRegName(const std::string& reg, const RegIdMap& reg_ids,
                                  const ColoringResult& cr, const std::vector<int>& spill_slots,
                                  std::vector<MipsInst>& prefix, int& scratch_used, bool is_use) {
        if (reg.empty() || !RegIdMap::IsVirtual(reg)) {
            return reg;
        }
        const int kNode = reg_ids.Get(reg);
        const int kVid = ParseVRegSuffix(reg);
        if (cr.actual_spills.count(kNode) != 0u) {
            assert(kVid >= 0 && static_cast<size_t>(kVid) < spill_slots.size());
            const int kOff = spill_slots[static_cast<size_t>(kVid)];
            assert(kOff >= 0);
            const std::string kTmp = scratch_used == 0 ? "$k0" : "$k1";
            assert(scratch_used < 2 && "At most two spilled operands per instruction");
            ++scratch_used;
            if (is_use) {
                prefix.push_back(MakeLw(kTmp, kOff, "$sp"));
            }
            return kTmp;
        }
        const int kColor = cr.color_by_node[static_cast<size_t>(kNode)];
        assert(kColor >= 0 && kColor < ColoringResult::kNumPaletteColors);
        return kAllocatableRegNames[kColor];
    }

    static std::string MapDefReg(const std::string& reg, const RegIdMap& reg_ids,
                                 const ColoringResult& cr) {
        if (reg.empty() || !RegIdMap::IsVirtual(reg)) {
            return reg;
        }
        const int kNode = reg_ids.Get(reg);
        if (cr.actual_spills.count(kNode) != 0u) {
            return "$k0";
        }
        const int kColor = cr.color_by_node[static_cast<size_t>(kNode)];
        assert(kColor >= 0 && kColor < ColoringResult::kNumPaletteColors);
        return kAllocatableRegNames[kColor];
    }

    static void SpillStoreIfNeeded(const std::string& orig_vreg, const std::string& phys_used,
                                   const RegIdMap& reg_ids, const ColoringResult& cr,
                                   const std::vector<int>& spill_slots,
                                   std::vector<MipsInst>& suffix) {
        if (orig_vreg.empty() || !RegIdMap::IsVirtual(orig_vreg)) {
            return;
        }
        const int kNode = reg_ids.Get(orig_vreg);
        if (cr.actual_spills.count(kNode) == 0u) {
            return;
        }
        const int kVid = ParseVRegSuffix(orig_vreg);
        const int kOff = spill_slots[static_cast<size_t>(kVid)];
        suffix.push_back(MakeSw(phys_used, kOff, "$sp"));
    }

    static void AppendRewritten(std::vector<MipsInst>& out, std::vector<MipsInst>& prefix,
                                MipsInst& m, const std::string& orig_dst_vreg,
                                const RegIdMap& reg_ids, const ColoringResult& cr,
                                const std::vector<int>& spill_slots) {
        out.insert(out.end(), prefix.begin(), prefix.end());
        out.push_back(m);
        SpillStoreIfNeeded(orig_dst_vreg, "$k0", reg_ids, cr, spill_slots, out);
    }

    static std::vector<MipsInst> RewriteInstruction(const MipsInst& inst, const RegIdMap& reg_ids,
                                                    const ColoringResult& cr,
                                                    const std::vector<int>& spill_slots) {
        std::vector<MipsInst> prefix;
        std::vector<MipsInst> out;
        int scratch = 0;

        if (!inst.IsInsn()) {
            out.push_back(inst);
            return out;
        }

        MipsInst m = inst;

        switch (inst.op) {
            case MipsOpcode::DIV: {
                scratch = 0;
                m.src1 = MapRegName(inst.src1, reg_ids, cr, spill_slots, prefix, scratch, true);
                m.src2 = MapRegName(inst.src2, reg_ids, cr, spill_slots, prefix, scratch, true);
                out.insert(out.end(), prefix.begin(), prefix.end());
                out.push_back(m);
                return out;
            }
            case MipsOpcode::MFLO:
            case MipsOpcode::MFHI:
                scratch = 0;
                m.dst = MapDefReg(inst.dst, reg_ids, cr);
                out.insert(out.end(), prefix.begin(), prefix.end());
                out.push_back(m);
                SpillStoreIfNeeded(inst.dst, m.dst, reg_ids, cr, spill_slots, out);
                return out;
            case MipsOpcode::SYSCALL:
            case MipsOpcode::J:
            case MipsOpcode::JAL:
            case MipsOpcode::JR: out.push_back(inst); return out;
            case MipsOpcode::BNEZ:
            case MipsOpcode::BEQZ:
                // Condition lives in src1 (see AsmWriter::EmitBnez); must rewrite vreg like other
                // uses.
                scratch = 0;
                m.src1 = MapRegName(inst.src1, reg_ids, cr, spill_slots, prefix, scratch, true);
                out.insert(out.end(), prefix.begin(), prefix.end());
                out.push_back(m);
                return out;
            case MipsOpcode::LI:
            case MipsOpcode::LA:
                m.dst = MapDefReg(inst.dst, reg_ids, cr);
                out.push_back(m);
                SpillStoreIfNeeded(inst.dst, m.dst, reg_ids, cr, spill_slots, out);
                return out;
            case MipsOpcode::ADDIU:
            case MipsOpcode::ANDI:
            case MipsOpcode::LW:
            case MipsOpcode::LBU:
                scratch = 0;
                m.dst = MapDefReg(inst.dst, reg_ids, cr);
                m.src1 = MapRegName(inst.src1, reg_ids, cr, spill_slots, prefix, scratch, true);
                AppendRewritten(out, prefix, m, inst.dst, reg_ids, cr, spill_slots);
                return out;
            case MipsOpcode::SW:
            case MipsOpcode::SB:
                scratch = 0;
                m.dst = MapRegName(inst.dst, reg_ids, cr, spill_slots, prefix, scratch, true);
                m.src1 = MapRegName(inst.src1, reg_ids, cr, spill_slots, prefix, scratch, true);
                out.insert(out.end(), prefix.begin(), prefix.end());
                out.push_back(m);
                return out;
            case MipsOpcode::MOVE:
            case MipsOpcode::SLL:
            case MipsOpcode::SRL:
            case MipsOpcode::SRA:
                scratch = 0;
                m.dst = MapDefReg(inst.dst, reg_ids, cr);
                m.src1 = MapRegName(inst.src1, reg_ids, cr, spill_slots, prefix, scratch, true);
                // Coalesce elimination: if dst and src resolve to the same physical register
                // and no spill loads are needed (prefix empty), this MOVE is a no-op.
                // $k0/$k1 are scratch registers used only for spill sequences; a MOVE between
                // two scratch registers might still need the downstream sw, so skip elimination
                // for those — the prefix/suffix logic in AppendRewritten handles them correctly.
                if (inst.op == MipsOpcode::MOVE && m.dst == m.src1 && prefix.empty() &&
                    m.dst != "$k0" && m.dst != "$k1") {
                    return out; // out is empty: trivial move eliminated
                }
                AppendRewritten(out, prefix, m, inst.dst, reg_ids, cr, spill_slots);
                return out;
            default:
                // R-type binary / logical
                scratch = 0;
                m.dst = MapDefReg(inst.dst, reg_ids, cr);
                m.src1 = MapRegName(inst.src1, reg_ids, cr, spill_slots, prefix, scratch, true);
                m.src2 = MapRegName(inst.src2, reg_ids, cr, spill_slots, prefix, scratch, true);
                AppendRewritten(out, prefix, m, inst.dst, reg_ids, cr, spill_slots);
                return out;
        }
    }

    static void RewriteBuffer(std::vector<MipsInst>& buffer, const RegIdMap& reg_ids,
                              const ColoringResult& cr, const std::vector<int>& spill_slots) {
        std::vector<MipsInst> new_buf;
        new_buf.reserve(buffer.size() * 2);
        for (const auto& inst : buffer) {
            auto chunk = RewriteInstruction(inst, reg_ids, cr, spill_slots);
            new_buf.insert(new_buf.end(), chunk.begin(), chunk.end());
        }
        buffer = std::move(new_buf);
    }

    /// Bump non-negative @c offset($sp) loads/stores by @p delta, and extend the main function
    /// prologue/epilogue @c addiu $sp,$sp,±F by @p delta.  Does not modify call-sequence
    /// @c addiu $sp,$sp,±kExtraArgArea (where magnitude differs from @p original_frame_size).
    static void AdjustStackFrameForCalleeSaved(std::vector<MipsInst>& buf, int delta,
                                               int original_frame_size) {
        if (delta == 0) {
            return;
        }
        for (auto& mips_inst : buf) {
            if (!mips_inst.IsInsn()) {
                continue;
            }
            auto bump_if_sp = [&](const std::string& base, int64_t& imm) {
                if (base == "$sp" && imm >= 0) {
                    imm += delta;
                }
            };
            switch (mips_inst.op) {
                case MipsOpcode::LW:
                case MipsOpcode::LBU:
                case MipsOpcode::SW:
                case MipsOpcode::SB: bump_if_sp(mips_inst.src1, mips_inst.imm); break;
                case MipsOpcode::ADDIU:
                    if (mips_inst.dst == "$sp" && mips_inst.src1 == "$sp") {
                        if (mips_inst.imm == -original_frame_size) {
                            mips_inst.imm -= delta;
                        } else if (mips_inst.imm == original_frame_size) {
                            mips_inst.imm += delta;
                        }
                    } else {
                        // Alloca / frame-relative address: addiu $reg, $sp, off — bump like lw/sw.
                        bump_if_sp(mips_inst.src1, mips_inst.imm);
                    }
                    break;
                default: break;
            }
        }
    }

    static void ApplyCalleeSaved(std::vector<MipsInst>& buf, int original_frame_size) {
        std::set<std::string> used_s;
        for (const auto& m : buf) {
            if (!m.IsInsn()) {
                continue;
            }
            auto consider = [&](const std::string& r) {
                if (IsCalleeSavedReg(r)) {
                    used_s.insert(r);
                }
            };
            consider(m.dst);
            consider(m.src1);
            consider(m.src2);
        }
        if (used_s.empty()) {
            return;
        }

        const int kCalleeSavedCount = static_cast<int>(used_s.size());
        const int kStackDelta = 4 * kCalleeSavedCount;
        std::vector<std::string> ordered(used_s.begin(), used_s.end());
        std::sort(ordered.begin(), ordered.end());

        // First: grow the compiler-emitted frame and slide $ra / locals upward by kStackDelta.
        // Then: emit sw $s* at 0..kStackDelta-4 below the (bumped) $ra slot — insert sits between
        // prologue addiu and sw $ra (see FunctionEmitter::EmitPrologue order).
        AdjustStackFrameForCalleeSaved(buf, kStackDelta, original_frame_size);

        for (size_t i = 0; i < buf.size(); ++i) {
            if (buf[i].op == MipsOpcode::ADDIU && buf[i].dst == "$sp" && buf[i].src1 == "$sp" &&
                buf[i].imm < 0) {
                const int kInsertAt = static_cast<int>(i) + 1;
                for (int j = 0; j < kCalleeSavedCount; ++j) {
                    buf.insert(buf.begin() + kInsertAt + j,
                               MakeSw(ordered[static_cast<size_t>(j)], j * 4, "$sp"));
                }
                break;
            }
        }

        for (size_t i = 0; i < buf.size(); ++i) {
            if (buf[i].op != MipsOpcode::LW || buf[i].dst != "$ra" || buf[i].src1 != "$sp") {
                continue;
            }
            std::vector<MipsInst> loads;
            for (int j = kCalleeSavedCount - 1; j >= 0; --j) {
                loads.push_back(MakeLw(ordered[static_cast<size_t>(j)], j * 4, "$sp"));
            }
            buf.insert(buf.begin() + static_cast<int>(i), loads.begin(), loads.end());
            i += loads.size();
        }
    }

    void RegAllocator::Run(std::vector<MipsInst>& buffer, const std::vector<int>& vreg_spill_slots,
                           int original_frame_size) {
        LivenessResult result = BuildLiveness(buffer);

        const auto& ig = result.ig;
        const auto& reg_ids = result.reg_ids;
        const int kNumRegs = reg_ids.Size();

        std::cerr << "[RegAlloc] Build complete: " << kNumRegs << " registers, "
                  << result.blocks.size() << " blocks\n";

        ColoringResult cr = ColorWithCoalesce(ig, reg_ids);

        if (!cr.actual_spills.empty()) {
            std::cerr << "[RegAlloc] warning: " << cr.actual_spills.size()
                      << " spilled (experimental spill rewrite)\n";
        }

        RewriteBuffer(buffer, reg_ids, cr, vreg_spill_slots);
        ApplyCalleeSaved(buffer, original_frame_size);

        const int kNumColored = cr.ColoredAllocatableCount(reg_ids);
        std::cerr << "[RegAlloc] Coloring: " << kNumColored
                  << " virtual regs colored (K=" << ColoringResult::kNumPaletteColors << ")\n";
    }

} // namespace mips

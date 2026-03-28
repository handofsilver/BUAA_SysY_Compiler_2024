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

    ColoringResult SimplifyAndSelect(const InterferenceGraph& ig, const RegIdMap& reg_ids) {
        const int kNumNodes = reg_ids.Size();
        assert(kNumNodes == ig.NumNodes());

        std::vector<int> color(static_cast<size_t>(kNumNodes), -1);
        std::vector<bool> precolored(static_cast<size_t>(kNumNodes), false);
        for (int i = 0; i < kNumNodes; ++i) {
            const int kPaletteIndex = RegIdMap::PaletteIndexOf(reg_ids.GetName(i));
            if (kPaletteIndex >= 0) {
                color[static_cast<size_t>(i)] = kPaletteIndex;
                precolored[static_cast<size_t>(i)] = true;
            }
        }

        std::vector<bool> need_simplify(static_cast<size_t>(kNumNodes), false);
        for (int i = 0; i < kNumNodes; ++i) {
            need_simplify[static_cast<size_t>(i)] =
                RegIdMap::IsAllocatable(reg_ids.GetName(i)) && !precolored[static_cast<size_t>(i)];
        }

        std::vector<int> eff_degree(static_cast<size_t>(kNumNodes));
        for (int i = 0; i < kNumNodes; ++i) {
            eff_degree[static_cast<size_t>(i)] = ig.Degree(i);
        }

        std::vector<bool> on_stack(static_cast<size_t>(kNumNodes), false);
        std::stack<int> select_stack;

        auto any_left = [&]() {
            for (int i = 0; i < kNumNodes; ++i) {
                if (need_simplify[static_cast<size_t>(i)] && !on_stack[static_cast<size_t>(i)]) {
                    return true;
                }
            }
            return false;
        };

        while (any_left()) {
            bool simplified = true;
            while (simplified) {
                simplified = false;
                for (int u = 0; u < kNumNodes; ++u) {
                    if (!need_simplify[static_cast<size_t>(u)] ||
                        on_stack[static_cast<size_t>(u)]) {
                        continue;
                    }
                    if (eff_degree[static_cast<size_t>(u)] < ColoringResult::kNumPaletteColors) {
                        select_stack.push(u);
                        on_stack[static_cast<size_t>(u)] = true;
                        for (int nb : ig.Neighbors(u)) {
                            if (need_simplify[static_cast<size_t>(nb)] &&
                                !on_stack[static_cast<size_t>(nb)]) {
                                --eff_degree[static_cast<size_t>(nb)];
                            }
                        }
                        simplified = true;
                        break;
                    }
                }
            }

            if (!any_left()) {
                break;
            }

            int victim = -1;
            int best_deg = -1;
            for (int u = 0; u < kNumNodes; ++u) {
                if (!need_simplify[static_cast<size_t>(u)] || on_stack[static_cast<size_t>(u)]) {
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
                if (need_simplify[static_cast<size_t>(nb)] && !on_stack[static_cast<size_t>(nb)]) {
                    --eff_degree[static_cast<size_t>(nb)];
                }
            }
        }

        std::unordered_set<int> actual_spills;

        while (!select_stack.empty()) {
            const int kPopped = select_stack.top();
            select_stack.pop();

            // Precolored ($t0-$t9 palette nodes) never enter need_simplify; guard for robustness.
            if (precolored[static_cast<size_t>(kPopped)]) {
                continue;
            }

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
            case MipsOpcode::JR:
                out.push_back(inst);
                return out;
            case MipsOpcode::BNEZ:
            case MipsOpcode::BEQZ:
                // Condition lives in src1 (see AsmWriter::EmitBnez); must rewrite vreg like other uses.
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

        ColoringResult cr = SimplifyAndSelect(ig, reg_ids);

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

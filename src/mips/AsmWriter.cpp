/**
 * @file AsmWriter.cpp
 * @brief Implementation of AsmWriter — structured instruction emission and O4 peephole.
 *
 * Section layout:
 *   § Routing + serialization
 *   § Labels / directives
 *   § .data helpers
 *   § Typed instruction emission
 *   § Peephole buffer — BeginBuffer, FlushBuffer, RunPeephole
 */

#include "mips/AsmWriter.h"
#include "mips/MipsCommon.h"

#include <cassert>

namespace mips {

    // =========================================================================
    // § Routing + serialization
    // =========================================================================

    void AsmWriter::Route(MipsInst inst) {
        if (buffering_) {
            buf_.push_back(std::move(inst));
        } else {
            os_ << Serialize(inst) << "\n";
        }
    }

    /**
     * Convert a MipsInst to its text representation.
     *
     * All instruction opcodes are left-padded to 6 characters to match the
     * formatting established by the original string-based emitters.
     */
    std::string AsmWriter::Serialize(const MipsInst& inst) {
        // Pad an opcode name to 6 characters (e.g. "addu" → "addu  ").
        auto pad = [](const char* name) -> std::string {
            std::string s(name);
            s.resize(6, ' ');
            return s;
        };

        const std::string kInd(kIndent);

        switch (inst.op) {
            // ── R-type: dst, src1, src2 ─────────────────────────────────
            case MipsOpcode::ADDU:
                return kInd + pad("addu") + inst.dst + ", " + inst.src1 + ", " + inst.src2;
            case MipsOpcode::SUBU:
                return kInd + pad("subu") + inst.dst + ", " + inst.src1 + ", " + inst.src2;
            case MipsOpcode::MUL:
                return kInd + pad("mul") + inst.dst + ", " + inst.src1 + ", " + inst.src2;
            case MipsOpcode::AND:
                return kInd + pad("and") + inst.dst + ", " + inst.src1 + ", " + inst.src2;
            case MipsOpcode::OR:
                return kInd + pad("or") + inst.dst + ", " + inst.src1 + ", " + inst.src2;
            case MipsOpcode::SLT:
                return kInd + pad("slt") + inst.dst + ", " + inst.src1 + ", " + inst.src2;
            case MipsOpcode::SGT:
                return kInd + pad("sgt") + inst.dst + ", " + inst.src1 + ", " + inst.src2;
            case MipsOpcode::SLE:
                return kInd + pad("sle") + inst.dst + ", " + inst.src1 + ", " + inst.src2;
            case MipsOpcode::SGE:
                return kInd + pad("sge") + inst.dst + ", " + inst.src1 + ", " + inst.src2;
            case MipsOpcode::SEQ:
                return kInd + pad("seq") + inst.dst + ", " + inst.src1 + ", " + inst.src2;
            case MipsOpcode::SNE:
                return kInd + pad("sne") + inst.dst + ", " + inst.src1 + ", " + inst.src2;

            // ── Shift: dst, src1, imm ───────────────────────────────────
            case MipsOpcode::SLL:
                return kInd + pad("sll") + inst.dst + ", " + inst.src1 + ", " +
                       std::to_string(inst.imm);
            case MipsOpcode::SRL:
                return kInd + pad("srl") + inst.dst + ", " + inst.src1 + ", " +
                       std::to_string(inst.imm);
            case MipsOpcode::SRA:
                return kInd + pad("sra") + inst.dst + ", " + inst.src1 + ", " +
                       std::to_string(inst.imm);

            // ── Division ────────────────────────────────────────────────
            case MipsOpcode::DIV: return kInd + pad("div") + inst.src1 + ", " + inst.src2;
            case MipsOpcode::MFLO: return kInd + pad("mflo") + inst.dst;
            case MipsOpcode::MFHI: return kInd + pad("mfhi") + inst.dst;

            // ── I-type arithmetic ───────────────────────────────────────
            case MipsOpcode::ADDIU:
                return kInd + pad("addiu") + inst.dst + ", " + inst.src1 + ", " +
                       std::to_string(inst.imm);
            case MipsOpcode::ANDI:
                return kInd + pad("andi") + inst.dst + ", " + inst.src1 + ", " +
                       std::to_string(inst.imm);

            // ── Memory: reg, offset(base) ───────────────────────────────
            case MipsOpcode::LW:
                return kInd + pad("lw") + inst.dst + ", " + std::to_string(inst.imm) + "(" +
                       inst.src1 + ")";
            case MipsOpcode::SW:
                return kInd + pad("sw") + inst.dst + ", " + std::to_string(inst.imm) + "(" +
                       inst.src1 + ")";
            case MipsOpcode::LBU:
                return kInd + pad("lbu") + inst.dst + ", " + std::to_string(inst.imm) + "(" +
                       inst.src1 + ")";
            case MipsOpcode::SB:
                return kInd + pad("sb") + inst.dst + ", " + std::to_string(inst.imm) + "(" +
                       inst.src1 + ")";

            // ── Pseudo-load ─────────────────────────────────────────────
            case MipsOpcode::LI:
                return kInd + pad("li") + inst.dst + ", " + std::to_string(inst.imm);
            case MipsOpcode::LA: return kInd + pad("la") + inst.dst + ", " + inst.label;

            // ── Control flow ────────────────────────────────────────────
            case MipsOpcode::J: return kInd + pad("j") + inst.label;
            case MipsOpcode::JAL: return kInd + pad("jal") + inst.label;
            case MipsOpcode::JR: return kInd + pad("jr") + inst.src1;
            case MipsOpcode::BNEZ: return kInd + pad("bnez") + inst.src1 + ", " + inst.label;
            case MipsOpcode::BEQZ: return kInd + pad("beqz") + inst.src1 + ", " + inst.label;

            // ── Register copy ───────────────────────────────────────────
            case MipsOpcode::MOVE: return kInd + pad("move") + inst.dst + ", " + inst.src1;

            // ── System ──────────────────────────────────────────────────
            case MipsOpcode::SYSCALL: return kInd + "syscall";

            // ── Pseudo / non-instruction ────────────────────────────────
            case MipsOpcode::LABEL: return inst.label + ":";
            case MipsOpcode::DIRECTIVE: return inst.raw;
            case MipsOpcode::BLANK: return "";
            case MipsOpcode::RAW: return kInd + inst.raw;
        }
        return ""; // unreachable
    }

    // =========================================================================
    // § Labels and directives
    // =========================================================================

    void AsmWriter::EmitLabel(const std::string& label) {
        Route({MipsOpcode::LABEL, {}, {}, {}, 0, label, {}});
    }

    void AsmWriter::EmitDirective(const std::string& directive) {
        Route({MipsOpcode::DIRECTIVE, {}, {}, {}, 0, {}, directive});
    }

    void AsmWriter::EmitBlankLine() {
        Route({MipsOpcode::BLANK});
    }

    // =========================================================================
    // § .data segment helpers
    // =========================================================================

    void AsmWriter::EmitWord(int64_t value) {
        Route({MipsOpcode::DIRECTIVE, {}, {}, {}, 0, {}, ".word " + std::to_string(value)});
    }

    void AsmWriter::EmitWordList(const std::vector<int64_t>& values) {
        std::string line = ".word ";
        for (size_t i = 0; i < values.size(); ++i) {
            if (i != 0) {
                line += ", ";
            }
            line += std::to_string(values[i]);
        }
        Route({MipsOpcode::DIRECTIVE, {}, {}, {}, 0, {}, std::move(line)});
    }

    void AsmWriter::EmitAsciiz(const std::string& str) {
        Route({MipsOpcode::DIRECTIVE, {}, {}, {}, 0, {}, ".asciiz \"" + str + "\""});
    }

    void AsmWriter::EmitSpace(int bytes) {
        Route({MipsOpcode::DIRECTIVE, {}, {}, {}, 0, {}, ".space " + std::to_string(bytes)});
    }

    // =========================================================================
    // § Typed instruction emission
    // =========================================================================

    // ── R-type ──────────────────────────────────────────────────────────────

    void AsmWriter::EmitAddu(const std::string& dst, const std::string& src1,
                             const std::string& src2) {
        Route({MipsOpcode::ADDU, dst, src1, src2});
    }

    void AsmWriter::EmitSubu(const std::string& dst, const std::string& src1,
                             const std::string& src2) {
        Route({MipsOpcode::SUBU, dst, src1, src2});
    }

    void AsmWriter::EmitMul(const std::string& dst, const std::string& src1,
                            const std::string& src2) {
        Route({MipsOpcode::MUL, dst, src1, src2});
    }

    void AsmWriter::EmitAnd(const std::string& dst, const std::string& src1,
                            const std::string& src2) {
        Route({MipsOpcode::AND, dst, src1, src2});
    }

    void AsmWriter::EmitOr(const std::string& dst, const std::string& src1,
                           const std::string& src2) {
        Route({MipsOpcode::OR, dst, src1, src2});
    }

    void AsmWriter::EmitSlt(const std::string& dst, const std::string& src1,
                            const std::string& src2) {
        Route({MipsOpcode::SLT, dst, src1, src2});
    }

    void AsmWriter::EmitSgt(const std::string& dst, const std::string& src1,
                            const std::string& src2) {
        Route({MipsOpcode::SGT, dst, src1, src2});
    }

    void AsmWriter::EmitSle(const std::string& dst, const std::string& src1,
                            const std::string& src2) {
        Route({MipsOpcode::SLE, dst, src1, src2});
    }

    void AsmWriter::EmitSge(const std::string& dst, const std::string& src1,
                            const std::string& src2) {
        Route({MipsOpcode::SGE, dst, src1, src2});
    }

    void AsmWriter::EmitSeq(const std::string& dst, const std::string& src1,
                            const std::string& src2) {
        Route({MipsOpcode::SEQ, dst, src1, src2});
    }

    void AsmWriter::EmitSne(const std::string& dst, const std::string& src1,
                            const std::string& src2) {
        Route({MipsOpcode::SNE, dst, src1, src2});
    }

    // ── Shift ───────────────────────────────────────────────────────────────

    void AsmWriter::EmitSll(const std::string& dst, const std::string& src, int shamt) {
        Route({MipsOpcode::SLL, dst, src, {}, shamt});
    }

    void AsmWriter::EmitSrl(const std::string& dst, const std::string& src, int shamt) {
        Route({MipsOpcode::SRL, dst, src, {}, shamt});
    }

    void AsmWriter::EmitSra(const std::string& dst, const std::string& src, int shamt) {
        Route({MipsOpcode::SRA, dst, src, {}, shamt});
    }

    // ── Division ────────────────────────────────────────────────────────────

    void AsmWriter::EmitDiv(const std::string& src1, const std::string& src2) {
        Route({MipsOpcode::DIV, {}, src1, src2});
    }

    void AsmWriter::EmitMflo(const std::string& dst) {
        Route({MipsOpcode::MFLO, dst});
    }

    void AsmWriter::EmitMfhi(const std::string& dst) {
        Route({MipsOpcode::MFHI, dst});
    }

    // ── I-type arithmetic ───────────────────────────────────────────────────

    void AsmWriter::EmitAddiu(const std::string& dst, const std::string& src, int imm) {
        Route({MipsOpcode::ADDIU, dst, src, {}, imm});
    }

    void AsmWriter::EmitAndi(const std::string& dst, const std::string& src, int64_t imm) {
        Route({MipsOpcode::ANDI, dst, src, {}, imm});
    }

    // ── Memory (general base register) ──────────────────────────────────────

    void AsmWriter::EmitLw(const std::string& dst, int offset, const std::string& base) {
        Route({MipsOpcode::LW, dst, base, {}, offset});
    }

    void AsmWriter::EmitSw(const std::string& src, int offset, const std::string& base) {
        Route({MipsOpcode::SW, src, base, {}, offset});
    }

    void AsmWriter::EmitLbu(const std::string& dst, int offset, const std::string& base) {
        Route({MipsOpcode::LBU, dst, base, {}, offset});
    }

    void AsmWriter::EmitSb(const std::string& src, int offset, const std::string& base) {
        Route({MipsOpcode::SB, src, base, {}, offset});
    }

    // ── Memory ($sp-relative convenience) ───────────────────────────────────

    void AsmWriter::EmitLwSp(const std::string& reg, int offset) {
        EmitLw(reg, offset, "$sp");
    }

    void AsmWriter::EmitSwSp(const std::string& reg, int offset) {
        EmitSw(reg, offset, "$sp");
    }

    // ── Pseudo-load ─────────────────────────────────────────────────────────

    void AsmWriter::EmitLi(const std::string& reg, int64_t imm) {
        Route({MipsOpcode::LI, reg, {}, {}, imm});
    }

    void AsmWriter::EmitLa(const std::string& reg, const std::string& label) {
        Route({MipsOpcode::LA, reg, {}, {}, 0, label});
    }

    // ── Control flow ────────────────────────────────────────────────────────

    void AsmWriter::EmitJ(const std::string& label) {
        Route({MipsOpcode::J, {}, {}, {}, 0, label});
    }

    void AsmWriter::EmitJal(const std::string& label) {
        Route({MipsOpcode::JAL, {}, {}, {}, 0, label});
    }

    void AsmWriter::EmitJr(const std::string& reg) {
        Route({MipsOpcode::JR, {}, reg});
    }

    void AsmWriter::EmitBnez(const std::string& reg, const std::string& label) {
        Route({MipsOpcode::BNEZ, {}, reg, {}, 0, label});
    }

    void AsmWriter::EmitBeqz(const std::string& reg, const std::string& label) {
        Route({MipsOpcode::BEQZ, {}, reg, {}, 0, label});
    }

    // ── Register copy ───────────────────────────────────────────────────────

    void AsmWriter::EmitMove(const std::string& dst, const std::string& src) {
        Route({MipsOpcode::MOVE, dst, src});
    }

    // ── System ──────────────────────────────────────────────────────────────

    void AsmWriter::EmitSyscall() {
        Route({MipsOpcode::SYSCALL});
    }

    // ── Raw / pre-formatted ─────────────────────────────────────────────────

    void AsmWriter::EmitInsn(const std::string& text) {
        Route({MipsOpcode::RAW, {}, {}, {}, 0, {}, text});
    }

    // =========================================================================
    // § Peephole buffer
    // =========================================================================

    void AsmWriter::BeginBuffer() {
        assert(!buffering_ && "BeginBuffer() called while already buffering");
        buffering_ = true;
        buf_.clear();
    }

    void AsmWriter::FlushBuffer() {
        assert(buffering_ && "FlushBuffer() called without a matching BeginBuffer()");
        RunPeephole();
        for (const auto& inst : buf_) {
            os_ << Serialize(inst) << "\n";
        }
        buf_.clear();
        buffering_ = false;
    }

    // =========================================================================
    // § Peephole implementation
    // =========================================================================

    /**
     * Iterate peephole rules on buf_ until convergence (no further changes).
     *
     * Rules applied (in order, per iteration):
     *
     *   Rule P2 — sw/lw pair → sw/move:
     *     If two consecutive real instructions are:
     *       [i  ]: sw $R,  X($sp)
     *       [i+1]: lw $R', X($sp)    (same stack offset X)
     *     then the lw is redundant — $R still holds the value just written.
     *     Replace [i+1] with "move $R', $R".
     *
     *   Rule P3 — self-move elimination:
     *     If an instruction is "move $R, $R", it is a no-op.  Delete it.
     *
     * Both rules are applied together in a convergence loop because P2 can
     * produce new P3 candidates, and erasing P3 lines can bring new sw/lw
     * pairs into adjacency for P2.
     */
    void AsmWriter::RunPeephole() {
        bool changed = true;
        while (changed) {
            changed = false;

            // ── Rule P2: sw + lw (same $sp offset) → sw + move ──────────
            for (size_t i = 0; i + 1 < buf_.size(); ++i) {
                if (!buf_[i].IsInsn() || !buf_[i + 1].IsInsn()) {
                    continue;
                }

                if (buf_[i].op != MipsOpcode::SW || buf_[i].src1 != "$sp") {
                    continue;
                }
                if (buf_[i + 1].op != MipsOpcode::LW || buf_[i + 1].src1 != "$sp") {
                    continue;
                }
                if (buf_[i].imm != buf_[i + 1].imm) {
                    continue;
                }

                // Replace the load with a register copy.
                buf_[i + 1] = {MipsOpcode::MOVE, buf_[i + 1].dst, buf_[i].dst};
                changed = true;
            }

            // ── Rule P3: move $R, $R → delete ───────────────────────────
            for (size_t i = 0; i < buf_.size(); /* incremented inside */) {
                if (buf_[i].op == MipsOpcode::MOVE && buf_[i].dst == buf_[i].src1) {
                    buf_.erase(buf_.begin() + static_cast<ptrdiff_t>(i));
                    changed = true;
                } else {
                    ++i;
                }
            }
        }
    }

} // namespace mips

/**
 * @file AsmWriter.h
 * @brief MIPS assembly output helper — centralizes all text emission.
 *
 * All MIPS output must go through AsmWriter rather than scattering `os_ <<`
 * across emitters.  This single choke-point enables:
 *   - Consistent indentation and formatting.
 *   - Buffered / peephole mode (O4 optimization):
 *       BeginBuffer()  — start collecting MipsInst for one function.
 *       FlushBuffer()  — run peephole rules on the collected instructions,
 *                        then serialize and write the result to the output.
 *     Between the two calls, every EmitXxx method routes through the buffer
 *     so that the output order is preserved.
 *     Labels act as natural peephole barriers (IsInsn() == false).
 *
 * Step 1 of O7 (register allocation) refactored the internal buffer from
 * vector<string> to vector<MipsInst>.  All typed EmitXxx methods construct
 * a MipsInst; the peephole pass matches on structured fields instead of
 * parsing strings; serialization to text happens in Serialize().
 */
#pragma once

#include "mips/MipsInst.h"

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace mips {

    class AsmWriter {
    public:
        explicit AsmWriter(std::ostream& os) : os_(os) {}

        // -----------------------------------------------------------------
        // Labels and directives
        // -----------------------------------------------------------------

        /// Emit a label line, e.g. "foo:\n".
        void EmitLabel(const std::string& label);

        /// Emit an assembler directive, e.g. ".data\n" or ".text\n".
        void EmitDirective(const std::string& directive);

        /// Emit a blank line.
        void EmitBlankLine();

        // -----------------------------------------------------------------
        // .data segment helpers
        // -----------------------------------------------------------------

        /// Emit ".word <value>\n" (scalar initializer).
        void EmitWord(int64_t value);

        /// Emit ".word v0, v1, ...\n" (array initializer).
        void EmitWordList(const std::vector<int64_t>& values);

        /// Emit ".asciiz \"<str>\"\n".
        void EmitAsciiz(const std::string& str);

        /// Emit ".space <bytes>\n".
        void EmitSpace(int bytes);

        // -----------------------------------------------------------------
        // Instruction-level emission (typed — preferred for .text segment)
        // -----------------------------------------------------------------

        // ── R-type: dst = src1 op src2 ──────────────────────────────────

        void EmitAddu(const std::string& dst, const std::string& src1, const std::string& src2);
        void EmitSubu(const std::string& dst, const std::string& src1, const std::string& src2);
        void EmitMul(const std::string& dst, const std::string& src1, const std::string& src2);
        void EmitAnd(const std::string& dst, const std::string& src1, const std::string& src2);
        void EmitOr(const std::string& dst, const std::string& src1, const std::string& src2);
        void EmitSlt(const std::string& dst, const std::string& src1, const std::string& src2);
        void EmitSgt(const std::string& dst, const std::string& src1, const std::string& src2);
        void EmitSle(const std::string& dst, const std::string& src1, const std::string& src2);
        void EmitSge(const std::string& dst, const std::string& src1, const std::string& src2);
        void EmitSeq(const std::string& dst, const std::string& src1, const std::string& src2);
        void EmitSne(const std::string& dst, const std::string& src1, const std::string& src2);

        // ── Shift: dst = src op shamt ───────────────────────────────────

        void EmitSll(const std::string& dst, const std::string& src, int shamt);
        void EmitSrl(const std::string& dst, const std::string& src, int shamt);
        void EmitSra(const std::string& dst, const std::string& src, int shamt);

        // ── Division: HI:LO = src1 / src2 ──────────────────────────────

        void EmitDiv(const std::string& src1, const std::string& src2);
        void EmitMflo(const std::string& dst);
        void EmitMfhi(const std::string& dst);

        // ── I-type arithmetic ───────────────────────────────────────────

        /// Emit "addiu <dst>, <src>, <imm>\n".
        void EmitAddiu(const std::string& dst, const std::string& src, int imm);

        /// Emit "andi <dst>, <src>, <imm>\n".
        void EmitAndi(const std::string& dst, const std::string& src, int64_t imm);

        // ── Memory (general base register) ──────────────────────────────

        void EmitLw(const std::string& dst, int offset, const std::string& base);
        void EmitSw(const std::string& src, int offset, const std::string& base);
        void EmitLbu(const std::string& dst, int offset, const std::string& base);
        void EmitSb(const std::string& src, int offset, const std::string& base);

        // ── Memory ($sp-relative convenience wrappers) ──────────────────

        /// Convenience: emit "lw <reg>, <offset>($sp)\n".
        void EmitLwSp(const std::string& reg, int offset);

        /// Convenience: emit "sw <reg>, <offset>($sp)\n".
        void EmitSwSp(const std::string& reg, int offset);

        // ── Pseudo-load ─────────────────────────────────────────────────

        /// Emit "li <reg>, <imm>\n".
        void EmitLi(const std::string& reg, int64_t imm);

        /// Emit "la <reg>, <label>\n".
        void EmitLa(const std::string& reg, const std::string& label);

        // ── Control flow ────────────────────────────────────────────────

        void EmitJ(const std::string& label);
        void EmitJal(const std::string& label);
        void EmitJr(const std::string& reg);
        void EmitBnez(const std::string& reg, const std::string& label);
        void EmitBeqz(const std::string& reg, const std::string& label);

        // ── Register copy ───────────────────────────────────────────────

        /// Emit "move <dst>, <src>\n".
        void EmitMove(const std::string& dst, const std::string& src);

        // ── System ──────────────────────────────────────────────────────

        /// Emit "syscall\n".
        void EmitSyscall();

        // -----------------------------------------------------------------
        // Raw / pre-formatted emission (for .data segment and edge cases)
        // -----------------------------------------------------------------

        /// Emit a raw instruction string with standard indentation.
        /// Creates a RAW MipsInst — use typed methods above whenever possible.
        void EmitInsn(const std::string& text);

        // -----------------------------------------------------------------
        // Peephole buffer (O4 optimization)
        // -----------------------------------------------------------------

        /// Start buffering all subsequent MipsInst for one function.
        /// Must be paired with exactly one FlushBuffer() call.
        void BeginBuffer();

        /// Apply peephole optimization rules to the buffered instructions,
        /// then serialize and write the optimized sequence to os_.
        void FlushBuffer();

        // -----------------------------------------------------------------
        // Structured buffer access (for future register allocation passes)
        // -----------------------------------------------------------------

        /// Direct read access to the instruction buffer (valid between
        /// BeginBuffer and FlushBuffer).
        const std::vector<MipsInst>& GetBuffer() const {
            return buf_;
        }

        /// Mutable access — allows register allocation to rewrite instructions.
        std::vector<MipsInst>& GetBuffer() {
            return buf_;
        }

    private:
        std::ostream& os_;

        // ── Peephole buffer ───────────────────────────────────────────────
        bool buffering_ = false;
        std::vector<MipsInst> buf_;

        /// Route @p inst to buf_ (when buffering) or directly serialize to os_.
        void Route(MipsInst inst);

        /// Convert a MipsInst to its text representation.
        static std::string Serialize(const MipsInst& inst);

        /// Apply peephole rules on buf_ until no further reduction is possible.
        void RunPeephole();
    };

} // namespace mips

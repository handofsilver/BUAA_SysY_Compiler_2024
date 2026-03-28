/**
 * @file AsmWriter.h
 * @brief MIPS assembly output helper — centralizes all text emission.
 *
 * All MIPS output must go through AsmWriter rather than scattering `os_ <<`
 * across emitters.  This single choke-point enables:
 *   - Consistent indentation and formatting.
 *   - Buffered / peephole mode (O4 optimization):
 *       BeginBuffer()  — start collecting lines for one function.
 *       FlushBuffer()  — run peephole rules on the collected lines, then
 *                        write the result to the output stream.
 *     Between the two calls, every EmitXxx method (including EmitLabel) routes
 *     through the buffer so that the output order is preserved.
 *     Labels act as natural peephole barriers: patterns are only applied to
 *     pairs of consecutively buffered instruction lines.
 */
#pragma once

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
        // Instruction-level emission
        // -----------------------------------------------------------------

        /// Emit a raw instruction string with standard indentation.
        /// @p text should NOT include leading whitespace or trailing newline.
        void EmitInsn(const std::string& text);

        /// Convenience: emit "li <reg>, <imm>\n".
        void EmitLi(const std::string& reg, int64_t imm);

        /// Convenience: emit "la <reg>, <label>\n".
        void EmitLa(const std::string& reg, const std::string& label);

        /// Convenience: emit "move <dst>, <src>\n".
        void EmitMove(const std::string& dst, const std::string& src);

        /// Convenience: emit "sw <reg>, <offset>($sp)\n".
        void EmitSwSp(const std::string& reg, int offset);

        /// Convenience: emit "lw <reg>, <offset>($sp)\n".
        void EmitLwSp(const std::string& reg, int offset);

        /// Convenience: emit "addiu <dst>, <src>, <imm>\n".
        void EmitAddiu(const std::string& dst, const std::string& src, int imm);

        /// Emit "syscall\n".
        void EmitSyscall();

        // -----------------------------------------------------------------
        // Peephole buffer (O4 optimization)
        // -----------------------------------------------------------------

        /// Start buffering all subsequent lines (instructions + labels) for one
        /// function.  Must be paired with exactly one FlushBuffer() call.
        /// Precondition: not already buffering.
        void BeginBuffer();

        /// Apply peephole optimization rules to the buffered lines, then write
        /// the optimized sequence to os_ and reset the buffer.
        /// Precondition: BeginBuffer() was called before this.
        void FlushBuffer();

    private:
        std::ostream& os_;

        // ── Peephole buffer ───────────────────────────────────────────────
        // Invariant: buffering_ == true  iff  buf_ is being accumulated.
        // Lines are stored without their trailing '\n'; it is added at flush.
        bool buffering_ = false;
        std::vector<std::string> buf_;

        /// Route @p line to buf_ (when buffering) or directly to os_ + '\n'.
        void RawLine(std::string line);

        /// Apply peephole rules on buf_ until no further reduction is possible.
        void RunPeephole();

        // ── Peephole pattern parsers ──────────────────────────────────────
        // All return false if @p line does not match the expected format.
        // Formats are fixed by the Emit* methods above, so matching is exact.

        /// True iff @p line is an instruction line (starts with kIndent).
        /// Label lines and blank lines are treated as peephole barriers.
        static bool IsInsnLine(const std::string& line);

        /// Parse "    sw    $REG, OFFSET($sp)" → out_reg, out_off.
        static bool ParseSwSp(const std::string& line, std::string& out_reg, int& out_off);

        /// Parse "    lw    $REG, OFFSET($sp)" → out_reg, out_off.
        static bool ParseLwSp(const std::string& line, std::string& out_reg, int& out_off);

        /// Parse "    move  $DST, $SRC" → out_dst, out_src.
        static bool ParseMove(const std::string& line, std::string& out_dst, std::string& out_src);
    };

} // namespace mips

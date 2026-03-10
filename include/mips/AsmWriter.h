/**
 * @file AsmWriter.h
 * @brief MIPS assembly output helper — centralizes all text emission.
 *
 * All MIPS output must go through AsmWriter rather than scattering `os_ <<`
 * across emitters. This single choke-point enables:
 *   - Consistent indentation and formatting.
 *   - Future buffered/peephole mode (buffer instructions, optimize, then flush).
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

        /// Emit a label line, e.g.  "foo:\n".
        void EmitLabel(const std::string& label);

        /// Emit an assembler directive, e.g.  ".data\n"  or  ".text\n".
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

    private:
        std::ostream& os_;
    };

} // namespace mips

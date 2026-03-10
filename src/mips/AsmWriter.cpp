#include "mips/AsmWriter.h"
#include "mips/MipsCommon.h"

namespace mips {

    void AsmWriter::EmitLabel(const std::string& label) {
        os_ << label << ":\n";
    }

    void AsmWriter::EmitDirective(const std::string& directive) {
        os_ << directive << "\n";
    }

    void AsmWriter::EmitBlankLine() {
        os_ << "\n";
    }

    // -----------------------------------------------------------------
    // .data segment helpers
    // -----------------------------------------------------------------

    void AsmWriter::EmitWord(int64_t value) {
        os_ << ".word " << value << "\n";
    }

    void AsmWriter::EmitWordList(const std::vector<int64_t>& values) {
        os_ << ".word ";
        for (size_t i = 0; i < values.size(); ++i) {
            if (i != 0) {
                os_ << ", ";
            }
            os_ << values[i];
        }
        os_ << "\n";
    }

    void AsmWriter::EmitAsciiz(const std::string& str) {
        os_ << ".asciiz \"" << str << "\"\n";
    }

    void AsmWriter::EmitSpace(int bytes) {
        os_ << ".space " << bytes << "\n";
    }

    // -----------------------------------------------------------------
    // Instruction-level emission
    // -----------------------------------------------------------------

    void AsmWriter::EmitInsn(const std::string& text) {
        os_ << kIndent << text << "\n";
    }

    void AsmWriter::EmitLi(const std::string& reg, int64_t imm) {
        os_ << kIndent << "li    " << reg << ", " << imm << "\n";
    }

    void AsmWriter::EmitLa(const std::string& reg, const std::string& label) {
        os_ << kIndent << "la    " << reg << ", " << label << "\n";
    }

    void AsmWriter::EmitMove(const std::string& dst, const std::string& src) {
        os_ << kIndent << "move  " << dst << ", " << src << "\n";
    }

    void AsmWriter::EmitSwSp(const std::string& reg, int offset) {
        os_ << kIndent << "sw    " << reg << ", " << offset << "($sp)\n";
    }

    void AsmWriter::EmitLwSp(const std::string& reg, int offset) {
        os_ << kIndent << "lw    " << reg << ", " << offset << "($sp)\n";
    }

    void AsmWriter::EmitAddiu(const std::string& dst, const std::string& src, int imm) {
        if (imm < 0) {
            os_ << kIndent << "addiu " << dst << ", " << src << ", -" << (-imm) << "\n";
        } else {
            os_ << kIndent << "addiu " << dst << ", " << src << ", " << imm << "\n";
        }
    }

    void AsmWriter::EmitSyscall() {
        os_ << kIndent << "syscall\n";
    }

} // namespace mips

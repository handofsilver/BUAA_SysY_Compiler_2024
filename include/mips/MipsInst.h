/**
 * @file MipsInst.h
 * @brief Structured representation of a MIPS instruction.
 *
 * Replaces the old string-based instruction buffer in AsmWriter.  Every
 * EmitXxx method now constructs a MipsInst; the peephole pass operates on
 * structured fields; serialization to text happens once at flush time.
 *
 * This representation is also the foundation for register-allocation
 * analyses (liveness, interference graph) that need accurate def/use sets
 * per instruction.
 */
#pragma once

#include <string>

namespace mips {

    /// MIPS opcode enum covering all instructions the backend emits.
    enum class MipsOpcode {
        // R-type: dst = src1 op src2
        ADDU,
        SUBU,
        MUL,
        AND,
        OR,
        SLT,
        SGT,
        SLE,
        SGE,
        SEQ,
        SNE,
        // Shift: dst = src1 op imm (shift amount)
        SLL,
        SRL,
        SRA,
        // Division: HI:LO = src1 / src2 (no explicit dst)
        DIV,
        MFLO,
        MFHI,
        // I-type arithmetic: dst = src1 op imm
        ADDIU,
        ANDI,
        // Memory: LW dst, imm(src1); SW dst, imm(src1)
        LW,
        SW,
        LBU,
        SB,
        // Pseudo-load
        LI,
        LA,
        // Control flow
        J,
        JAL,
        JR,
        BNEZ,
        BEQZ,
        // Register copy
        MOVE,
        // System
        SYSCALL,
        // Non-instruction markers
        LABEL,     // label definition (label field holds the name without ':')
        DIRECTIVE, // assembler directive (raw field holds full text)
        BLANK,     // blank line
        RAW,       // pre-formatted instruction text (raw field, indented on output)
    };

    /// Structured representation of a single MIPS instruction or pseudo-op.
    ///
    /// Field conventions by opcode group:
    ///   R-type (ADDU etc.):  dst=rd, src1=rs, src2=rt
    ///   Shift  (SLL etc.):   dst=rd, src1=rt, imm=shamt
    ///   DIV:                 src1=rs, src2=rt
    ///   MFLO/MFHI:           dst=rd
    ///   I-arith (ADDIU etc.):dst=rt, src1=rs, imm=immediate
    ///   Load   (LW, LBU):   dst=rt, src1=base, imm=offset
    ///   Store  (SW, SB):     dst=rt (value register), src1=base, imm=offset
    ///   LI:                  dst=rd, imm=value
    ///   LA:                  dst=rd, label=target
    ///   MOVE:                dst=rd, src1=rs
    ///   J/JAL:               label=target
    ///   JR:                  src1=rs
    ///   BNEZ/BEQZ:           src1=rs, label=target
    ///   SYSCALL:             (no operands)
    ///   LABEL:               label=name
    ///   DIRECTIVE/RAW:       raw=text
    ///   BLANK:               (empty)
    struct MipsInst {
        MipsOpcode op = MipsOpcode::RAW;
        std::string dst;   ///< destination / value register
        std::string src1;  ///< first source / base register
        std::string src2;  ///< second source register
        int64_t imm = 0;   ///< immediate / offset / shift amount
        std::string label; ///< label name or branch target
        std::string raw;   ///< pre-formatted text (for DIRECTIVE, RAW)

        /// True if this entry represents a real instruction (not label/directive/blank).
        bool IsInsn() const {
            return op != MipsOpcode::LABEL && op != MipsOpcode::DIRECTIVE &&
                   op != MipsOpcode::BLANK && op != MipsOpcode::RAW;
        }
    };

} // namespace mips

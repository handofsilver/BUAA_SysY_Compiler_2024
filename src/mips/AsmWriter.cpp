/**
 * @file AsmWriter.cpp
 * @brief Implementation of AsmWriter — assembly line emission and O4 peephole.
 *
 * Section layout:
 *   § Private helper  — RawLine
 *   § Labels/directives
 *   § .data helpers
 *   § Instructions
 *   § Peephole buffer — BeginBuffer, FlushBuffer
 *   § Peephole impl   — RunPeephole, IsInsnLine, ParseSwSp, ParseLwSp, ParseMove
 */

#include "mips/AsmWriter.h"
#include "mips/MipsCommon.h"

#include <cassert>

namespace mips {

    // =========================================================================
    // § Private helper
    // =========================================================================

    /**
     * Route @p line to the peephole buffer or directly to os_.
     * The trailing newline is NOT part of stored lines; it is added at flush
     * time (or immediately when not buffering).
     */
    void AsmWriter::RawLine(std::string line) {
        if (buffering_) {
            buf_.push_back(std::move(line));
        } else {
            os_ << line << "\n";
        }
    }

    // =========================================================================
    // § Labels and directives
    // =========================================================================

    void AsmWriter::EmitLabel(const std::string& label) {
        // Labels are buffered alongside instructions so that block ordering is
        // preserved.  In RunPeephole they act as barriers (IsInsnLine = false).
        RawLine(label + ":");
    }

    void AsmWriter::EmitDirective(const std::string& directive) {
        RawLine(directive);
    }

    void AsmWriter::EmitBlankLine() {
        RawLine("");
    }

    // =========================================================================
    // § .data segment helpers
    // =========================================================================

    void AsmWriter::EmitWord(int64_t value) {
        RawLine(".word " + std::to_string(value));
    }

    void AsmWriter::EmitWordList(const std::vector<int64_t>& values) {
        std::string line = ".word ";
        for (size_t i = 0; i < values.size(); ++i) {
            if (i != 0) {
                line += ", ";
            }
            line += std::to_string(values[i]);
        }
        RawLine(std::move(line));
    }

    void AsmWriter::EmitAsciiz(const std::string& str) {
        RawLine(".asciiz \"" + str + "\"");
    }

    void AsmWriter::EmitSpace(int bytes) {
        RawLine(".space " + std::to_string(bytes));
    }

    // =========================================================================
    // § Instructions
    // =========================================================================

    void AsmWriter::EmitInsn(const std::string& text) {
        RawLine(std::string(kIndent) + text);
    }

    void AsmWriter::EmitLi(const std::string& reg, int64_t imm) {
        RawLine(std::string(kIndent) + "li    " + reg + ", " + std::to_string(imm));
    }

    void AsmWriter::EmitLa(const std::string& reg, const std::string& label) {
        RawLine(std::string(kIndent) + "la    " + reg + ", " + label);
    }

    void AsmWriter::EmitMove(const std::string& dst, const std::string& src) {
        RawLine(std::string(kIndent) + "move  " + dst + ", " + src);
    }

    void AsmWriter::EmitSwSp(const std::string& reg, int offset) {
        RawLine(std::string(kIndent) + "sw    " + reg + ", " + std::to_string(offset) + "($sp)");
    }

    void AsmWriter::EmitLwSp(const std::string& reg, int offset) {
        RawLine(std::string(kIndent) + "lw    " + reg + ", " + std::to_string(offset) + "($sp)");
    }

    void AsmWriter::EmitAddiu(const std::string& dst, const std::string& src, int imm) {
        std::string line = std::string(kIndent) + "addiu " + dst + ", " + src + ", ";
        if (imm < 0) {
            line += "-" + std::to_string(-imm);
        } else {
            line += std::to_string(imm);
        }
        RawLine(std::move(line));
    }

    void AsmWriter::EmitSyscall() {
        RawLine(std::string(kIndent) + "syscall");
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
        for (const auto& line : buf_) {
            os_ << line << "\n";
        }
        buf_.clear();
        buffering_ = false;
    }

    // =========================================================================
    // § Peephole implementation
    // =========================================================================

    bool AsmWriter::IsInsnLine(const std::string& line) {
        // Instruction lines are indented by kIndent (4 spaces).
        // Label lines ("foo:") and blank lines are used as barriers and are
        // never candidates for pattern matching.
        return line.size() >= 4 && line[0] == ' ' && line[1] == ' ' && line[2] == ' ' &&
               line[3] == ' ';
    }

    /**
     * Parse "    sw    $REG, OFFSET($sp)" into @p out_reg and @p out_off.
     * The prefix "    sw    " is exactly 10 chars (kIndent + "sw    ").
     */
    bool AsmWriter::ParseSwSp(const std::string& line, std::string& out_reg, int& out_off) {
        const std::string kPfx = "    sw    ";
        if (line.compare(0, kPfx.size(), kPfx) != 0) {
            return false;
        }
        // rest: "$REG, OFFSET($sp)"
        const std::string kRest = line.substr(kPfx.size());
        const auto kComma = kRest.find(", ");
        if (kComma == std::string::npos) {
            return false;
        }
        out_reg = kRest.substr(0, kComma);
        // after_comma: "OFFSET($sp)"
        const std::string kAfterComma = kRest.substr(kComma + 2);
        const auto kParen = kAfterComma.find("($sp)");
        if (kParen == std::string::npos) {
            return false;
        }
        try {
            out_off = std::stoi(kAfterComma.substr(0, kParen));
        } catch (...) { return false; }
        return true;
    }

    /**
     * Parse "    lw    $REG, OFFSET($sp)" into @p out_reg and @p out_off.
     * Symmetric to ParseSwSp; only the opcode prefix differs.
     */
    bool AsmWriter::ParseLwSp(const std::string& line, std::string& out_reg, int& out_off) {
        const std::string kPfx = "    lw    ";
        if (line.compare(0, kPfx.size(), kPfx) != 0) {
            return false;
        }
        const std::string kRest = line.substr(kPfx.size());
        const auto kComma = kRest.find(", ");
        if (kComma == std::string::npos) {
            return false;
        }
        out_reg = kRest.substr(0, kComma);
        const std::string kAfterComma = kRest.substr(kComma + 2);
        const auto kParen = kAfterComma.find("($sp)");
        if (kParen == std::string::npos) {
            return false;
        }
        try {
            out_off = std::stoi(kAfterComma.substr(0, kParen));
        } catch (...) { return false; }
        return true;
    }

    /**
     * Parse "    move  $DST, $SRC" into @p out_dst and @p out_src.
     * The prefix "    move  " is 10 chars (kIndent + "move  ").
     */
    bool AsmWriter::ParseMove(const std::string& line, std::string& out_dst, std::string& out_src) {
        const std::string kPfx = "    move  ";
        if (line.compare(0, kPfx.size(), kPfx) != 0) {
            return false;
        }
        // rest: "$DST, $SRC"
        const std::string kRest = line.substr(kPfx.size());
        const auto kComma = kRest.find(", ");
        if (kComma == std::string::npos) {
            return false;
        }
        out_dst = kRest.substr(0, kComma);
        out_src = kRest.substr(kComma + 2);
        return true;
    }

    /**
     * Iterate peephole rules on buf_ until convergence (no further changes).
     *
     * Rules applied (in order, per iteration):
     *
     *   Rule P2 — sw/lw pair → sw/move:
     *     If two consecutive instruction lines (no label/blank between them) are
     *       [i  ]: sw $R,  X($sp)
     *       [i+1]: lw $R', X($sp)    (same stack offset X)
     *     then the lw is redundant — $R still holds the value just written.
     *     Replace [i+1] with "move $R', $R".
     *
     *   Rule P3 — self-move elimination:
     *     If any instruction line is "move $R, $R", it is a no-op.  Delete it.
     *     (This cleans up the P2 residue when $R == $R', i.e. the original
     *      sw/lw used the same register on both sides.)
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
                if (!IsInsnLine(buf_[i]) || !IsInsnLine(buf_[i + 1])) {
                    continue;
                }

                std::string sw_reg;
                int sw_off = 0;
                if (!ParseSwSp(buf_[i], sw_reg, sw_off)) {
                    continue;
                }

                std::string lw_reg;
                int lw_off = 0;
                if (!ParseLwSp(buf_[i + 1], lw_reg, lw_off)) {
                    continue;
                }

                if (sw_off != lw_off) {
                    continue;
                }

                // $sw_reg holds the just-written value; replace the load with a
                // register copy.  If sw_reg == lw_reg the result is "move $R, $R"
                // which Rule P3 will eliminate in the same or next iteration.
                buf_[i + 1] = std::string(kIndent) + "move  " + lw_reg + ", " + sw_reg;
                changed = true;
            }

            // ── Rule P3: move $R, $R → delete ───────────────────────────
            for (size_t i = 0; i < buf_.size(); /* incremented inside */) {
                std::string mv_dst, mv_src;
                if (IsInsnLine(buf_[i]) && ParseMove(buf_[i], mv_dst, mv_src) && mv_dst == mv_src) {
                    buf_.erase(buf_.begin() + static_cast<ptrdiff_t>(i));
                    changed = true;
                } else {
                    ++i;
                }
            }
        }
    }

} // namespace mips

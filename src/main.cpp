/**
 * Compiler entry: file I/O only. Reads testfile.txt, calls RunCompiler(), writes
 * error.txt (on errors) or lexer.txt / parser.txt / symbol.txt / llvm_ir.txt from
 * CompilerResult. All pipeline and front-end output collection live in Driver.
 */
#include "Driver.h"
#include "ir/Module.h"
#include "mips/MipsEmitter.h"
#include "pass/ConstFoldLVN.h"
#include "pass/DCE.h"
#include "pass/Mem2Reg.h"
#include <cerrno>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

int main() {
    try {
        std::ifstream in("testfile.txt");
        if (!in) {
            std::cerr << "Error: cannot open testfile.txt";
#ifdef _WIN32
            if (errno) {
                char buf[96];
                if (strerror_s(buf, errno) == 0) {
                    std::cerr << " (" << buf << ")";
                }
            }
#else
            if (errno) {
                std::cerr << " (" << strerror(errno) << ")";
            }
#endif
            std::cerr << ". Run from the directory that contains testfile.txt.\n";
            return 1;
        }

        std::ostringstream buf;
        buf << in.rdbuf();
        std::string source = buf.str();
        in.close();

        const bool kEmitLexerOutput = true;     /*lexical analysis: lexer.txt  */
        const bool kEmitParserOutput = true;    /* syntax analysis: parser.txt */
        const bool kEmitSymbolOutput = true;    /* semantic analysis: symbol.txt */
        const bool kEmitLLVMIROutput = true;    /* intermediate code(LLVM IR): llvm_ir.txt */
        const bool kEnableMem2Reg = true;       /* IR optimization(Mem2Reg Pass): mem2reg   */
        const bool kEnableConstFoldLVN = true;  /* IR optimization(ConstFold+LVN Pass)      */
        const bool kEnableDCE = true;           /* IR optimization(DCE Pass)                */
        const bool kEnableMulDivOpt = true;     /* MIPS optimization(Mul/Div strength reduction) */
        const bool kEnablePeephole = true;      /* MIPS optimization(Peephole: sw/lw elim, etc.) */
        const bool kEnableBlockMerge = true;    /* MIPS optimization(redundant-jump elimination) */
        const bool kEnableRegAlloc = true;      /* MIPS optimization (graph-coloring RA + vregs) */
        const bool kEmitMIPSOutput = true;      /* target code(MIPS): mips.txt */
        const bool kRenumberSSAForPrint = true; /* false = use original IR names (debug) */

        CompilerResult result =
            RunCompiler(source, kEmitLexerOutput, kEmitParserOutput, kEmitSymbolOutput);

        if (result.has_errors) {
            std::ofstream err("error.txt");
            for (const auto& p : result.errors) {
                err << p.first << " " << p.second << "\n";
            }
        } else {
            if (kEmitLexerOutput && !result.lexer_output.empty()) {
                std::ofstream lexer_out("lexer.txt");
                lexer_out << result.lexer_output;
            }
            if (kEmitParserOutput && !result.parser_output.empty()) {
                std::ofstream parser_out("parser.txt");
                parser_out << result.parser_output;
            }
            if (kEmitSymbolOutput && !result.symbol_output.empty()) {
                std::ofstream symbol_out("symbol.txt");
                symbol_out << result.symbol_output;
            }
            if (kEmitLLVMIROutput && result.module) {
                if (kEnableMem2Reg) {
                    pass::Mem2RegPass mem2reg;
                    for (auto& func : result.module->GetFunctions()) {
                        // Skip external declarations (no basic blocks).
                        if (!func->GetBlocks().empty()) {
                            mem2reg.Run(*func, result.module.get());
                        }
                    }
                }
                if (kEnableConstFoldLVN) {
                    // Iterate to a fixpoint because substitutions in one function/block
                    // can expose new fold/LVN opportunities in the same pass pipeline.
                    bool changed = true;
                    while (changed) {
                        changed = false;
                        pass::ConstFoldLVNPass const_fold_lvn(*result.module);
                        for (auto& func : result.module->GetFunctions()) {
                            // Skip external declarations (no basic blocks).
                            if (!func->GetBlocks().empty()) {
                                changed |= const_fold_lvn.Run(*func);
                            }
                        }
                    }
                }
                if (kEnableDCE) {
                    pass::DCEPass dce;
                    for (auto& func : result.module->GetFunctions()) {
                        if (!func->GetBlocks().empty()) {
                            dce.Run(*func);
                        }
                    }
                }
                std::ofstream llvm_out("llvm_ir.txt");
                result.module->Print(llvm_out, kRenumberSSAForPrint);
            }
            if (kEmitMIPSOutput && result.module) {
                std::ofstream mips_out("mips.txt");
                mips::MipsOptions mips_opts;
                mips_opts.enable_mul_div_opt = kEnableMulDivOpt;
                mips_opts.enable_peephole = kEnablePeephole;
                mips_opts.enable_block_merge = kEnableBlockMerge;
                mips_opts.enable_reg_alloc = kEnableRegAlloc;
                mips::MipsEmitter emitter(mips_out, *result.module, mips_opts);
                emitter.Emit();
            }
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Runtime error: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "Runtime error: unknown exception.\n";
        return 1;
    }
}

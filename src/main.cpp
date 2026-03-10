/**
 * Compiler entry: file I/O only. Reads testfile.txt, calls RunCompiler(), writes
 * error.txt (on errors) or lexer.txt / parser.txt / symbol.txt / llvm_ir.txt from
 * CompilerResult. All pipeline and front-end output collection live in Driver.
 */
#include "Driver.h"
#include "ir/Module.h"
#include "mips/MipsEmitter.h"
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
        const bool kEmitMIPSOutput = false;     /* target code(MIPS): mips.txt */
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
                std::ofstream llvm_out("llvm_ir.txt");
                result.module->Print(llvm_out, kRenumberSSAForPrint);
            }
            if (kEmitMIPSOutput && result.module) {
                std::ofstream mips_out("mips.txt");
                mips::MipsOptions mips_opts;
                // mips_opts.enable_reg_alloc = kEnableRegAlloc;  // TODO: wire up
                // mips_opts.enable_peephole  = kEnablePeephole;  // TODO: wire up
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

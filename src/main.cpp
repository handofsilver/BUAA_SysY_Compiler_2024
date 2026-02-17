/**
 * Compiler driver: reads testfile.txt, runs pipeline (Lexer -> Parser -> Semantic -> Codegen),
 * writes error.txt (on errors) or lexer.txt / parser.txt / symbol.txt / llvm_ir.txt per
 * requirements.
 */
#include "Driver.h"
#include "Symbol.h"
#include "ir/Module.h"
#include <algorithm>
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

        const bool kEmitLexerOutput = true;  /* requirement_1: lexer.txt */
        const bool kEmitParserOutput = true; /* requirement_2: parser.txt */
        CompilerResult result = RunCompiler(source, kEmitLexerOutput, kEmitParserOutput);

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

            const bool kEmitSymbolOutput = false;

            if (kEmitSymbolOutput && result.analyzer) {
                std::ofstream out("symbol.txt");
                std::vector<std::pair<int, Symbol>> ordered_symbols =
                    result.analyzer->GetOrderedSymbols();
                std::sort(ordered_symbols.begin(), ordered_symbols.end(),
                          [](const std::pair<int, Symbol>& a, const std::pair<int, Symbol>& b) {
                              return a.first < b.first;
                          });
                for (const auto& p : ordered_symbols) {
                    out << p.second.FormatForOutput() << "\n";
                }
            }

            if (result.module) {
                std::ofstream llvm_out("llvm_ir.txt");
                result.module->Print(llvm_out);
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

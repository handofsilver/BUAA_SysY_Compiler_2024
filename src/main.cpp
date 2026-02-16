/**
 * Compiler driver (requirement_2 parser + requirement_3 semantic analysis).
 * Reads testfile.txt, runs Lexer -> Parser -> SemanticAnalyzer.
 * Output: error.txt (any lexer/parser/semantic errors) or symbol.txt (no errors).
 * Requirement: symbol output shall be easy to turn off (e.g. for full compiler).
 */
#include "Lexer.h"
#include "Parser.h"
#include "SemanticAnalyzer.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

int main() {
    try {
        std::ifstream in("testfile.txt");
        if (!in) {
            std::cerr << "Error: cannot open testfile.txt";

            // Handle platform-specific error message formatting
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

        Lexer lexer(std::move(source));
        Parser parser(lexer);

        std::ostringstream parser_out;
        parser.SetParserOutput(&parser_out);
        parser.SetEmitParserOutput(true);

        auto comp_unit = parser.ParseCompUnit();

        std::vector<std::pair<int, std::string>> all_errors;
        for (const auto& p : lexer.GetErrorLog()) {
            all_errors.push_back(p);
        }
        for (const auto& p : parser.GetErrorLog()) {
            all_errors.push_back(p);
        }

        std::unique_ptr<SemanticAnalyzer> analyzer;
        if (comp_unit) {
            analyzer = std::make_unique<SemanticAnalyzer>();
            analyzer->Analyze(*comp_unit);
            for (const auto& p : analyzer->GetErrorLog()) {
                all_errors.push_back(p);
            }
        }

        std::sort(all_errors.begin(), all_errors.end(),
                  [](const std::pair<int, std::string>& a, const std::pair<int, std::string>& b) {
                      return a.first < b.first;
                  });

        const bool kEmitSymbolOutput =
            true; // set false to disable symbol.txt (e.g. for full compiler)

        if (!all_errors.empty()) {
            std::ofstream err("error.txt");
            for (const auto& p : all_errors) {
                err << p.first << " " << p.second << "\n";
            }
        } else if (analyzer && kEmitSymbolOutput) {
            std::ofstream out("symbol.txt");

            std::vector<std::pair<int, Symbol>> ordered_symbols = analyzer->GetOrderedSymbols();
            std::sort(ordered_symbols.begin(), ordered_symbols.end(),
                      [](const std::pair<int, Symbol>& a, const std::pair<int, Symbol>& b) {
                          return a.first < b.first;
                      });

            for (const auto& p : ordered_symbols) {
                out << p.second.FormatForOutput() << "\n";
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

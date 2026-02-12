/**
 * Compiler driver (requirement_2 parser + requirement_3 semantic analysis).
 * Reads testfile.txt, runs Lexer -> Parser -> SemanticAnalyzer.
 * Output: error.txt (any lexer/parser/semantic errors) or symbol.txt (no errors).
 */
#include "Lexer.h"
#include "Parser.h"
#include "SemanticAnalyzer.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

int main() {
    std::ifstream in("testfile.txt");
    if (!in) {
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
    if (all_errors.empty() && comp_unit) {
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

    if (!all_errors.empty()) {
        std::ofstream err("error.txt");
        for (const auto& p : all_errors) {
            err << p.first << " " << p.second << "\n";
        }
    } else if (analyzer) {
        std::ofstream out("symbol.txt");
        for (const auto& p : analyzer->GetOrderedSymbols()) {
            out << p.second.FormatForOutput() << "\n";
        }
    }
    // Optional: when no semantic phase ran (syntax errors), no symbol.txt is written.

    return 0;
}

/**
 * Compiler driver for requirement_2_parser.
 * Reads testfile.txt, runs Lexer + Parser, outputs parser.txt (correct) or error.txt (errors).
 */
#include "Lexer.h"
#include "Parser.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

int main() {
    // 1. Read source from testfile.txt
    std::ifstream in("testfile.txt");
    if (!in) {
        return 1;
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    std::string source = buf.str();
    in.close();

    // 2. Run parser (parser drives lexer via Advance())
    Lexer lexer(std::move(source));
    Parser parser(lexer);

    std::ostringstream parser_out;
    parser.SetParserOutput(&parser_out);
    parser.SetEmitParserOutput(true);

    parser.ParseCompUnit();

    // 3. Merge lexer and parser errors, sort by line number
    std::vector<std::pair<int, std::string>> all_errors;
    for (const auto& p : lexer.GetErrorLog()) {
        all_errors.push_back(p);
    }
    for (const auto& p : parser.GetErrorLog()) {
        all_errors.push_back(p);
    }
    std::sort(all_errors.begin(), all_errors.end(),
              [](const std::pair<int, std::string>& a, const std::pair<int, std::string>& b) {
                  return a.first < b.first;
              });

    // 4. Output: error.txt if any errors, else parser.txt
    if (!all_errors.empty()) {
        std::ofstream err("error.txt");
        for (const auto& p : all_errors) {
            err << p.first << " " << p.second << "\n";
        }
    } else {
        std::ofstream out("parser.txt");
        out << parser_out.str();
    }

    return 0;
}

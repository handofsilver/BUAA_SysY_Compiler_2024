/**
 * @file Driver.cpp
 * @brief Implementation of RunCompiler pipeline.
 * All front-end outputs (lexer, parser, symbol) are collected here; main only does file I/O.
 */
#include "Driver.h"
#include "IRGenVisitor.h"
#include "Lexer.h"
#include "Parser.h"
#include "Symbol.h"
#include <algorithm>
#include <memory>
#include <sstream>

CompilerResult RunCompiler(const std::string& source, bool emit_lexer_output,
                           bool emit_parser_output, bool emit_symbol_output) {
    CompilerResult result;
    std::string src = source;
    Lexer lexer(std::move(src));

    std::ostringstream lexer_out;
    if (emit_lexer_output) {
        lexer.SetLexerOutput(&lexer_out);
        lexer.SetEmitLexerOutput(true);
    }

    Parser parser(lexer);

    std::ostringstream parser_out;
    if (emit_parser_output) {
        parser.SetParserOutput(&parser_out);
        parser.SetEmitParserOutput(true);
    }

    result.comp_unit = parser.ParseCompUnit();

    if (emit_lexer_output) {
        result.lexer_output = lexer_out.str();
    }

    result.errors.clear();
    for (const auto& p : lexer.GetErrorLog()) {
        result.errors.push_back(p);
    }
    for (const auto& p : parser.GetErrorLog()) {
        result.errors.push_back(p);
    }

    if (result.comp_unit) {
        auto analyzer = std::make_unique<SemanticAnalyzer>();
        analyzer->Analyze(*result.comp_unit);
        for (const auto& p : analyzer->GetErrorLog()) {
            result.errors.push_back(p);
        }
        result.analyzer = std::move(analyzer);
    }

    std::sort(result.errors.begin(), result.errors.end(),
              [](const std::pair<int, std::string>& a, const std::pair<int, std::string>& b) {
                  return a.first < b.first;
              });

    if (emit_parser_output) {
        result.parser_output = parser_out.str();
    }
    result.has_errors = !result.errors.empty();

    if (result.has_errors) {
        return result;
    }

    if (emit_symbol_output && result.analyzer) {
        std::vector<std::pair<int, Symbol>> ordered = result.analyzer->GetOrderedSymbols();
        std::sort(ordered.begin(), ordered.end(),
                  [](const std::pair<int, Symbol>& a, const std::pair<int, Symbol>& b) {
                      return a.first < b.first;
                  });
        std::ostringstream sym_out;
        for (const auto& p : ordered) {
            sym_out << p.second.FormatForOutput() << "\n";
        }
        result.symbol_output = sym_out.str();
    }

    if (result.comp_unit) {
        IRGenVisitor ir_gen;
        result.module = ir_gen.Translate(*result.comp_unit);
    }

    return result;
}

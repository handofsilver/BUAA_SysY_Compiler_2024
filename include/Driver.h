/**
 * @file Driver.h
 * @brief Compiler pipeline: Lexer -> Parser -> SemanticAnalyzer -> IRGenVisitor.
 * Main only does file I/O and delegates to RunCompiler().
 */
#pragma once

#include "AST.h"
#include "SemanticAnalyzer.h"
#include <memory>
#include <string>
#include <vector>

namespace ir {
    class Module;
}

/**
 * Result of running the compiler pipeline on a source string.
 * When has_errors is true, only errors are guaranteed; lexer_output/parser_output may still be
 * filled if they were requested (for debugging). For 正确源程序, write lexer.txt/parser.txt from
 * these when the corresponding emit flag was true.
 */
struct CompilerResult {
    bool has_errors = false;
    std::vector<std::pair<int, std::string>> errors;
    std::unique_ptr<CompUnit> comp_unit;
    std::unique_ptr<SemanticAnalyzer> analyzer;
    std::string lexer_output;  /** Filled when emit_lexer_output was true (requirement_1). */
    std::string parser_output; /** Filled when emit_parser_output was true (requirement_2). */
    std::unique_ptr<ir::Module> module;
};

/** Run full pipeline: lex -> parse -> semantic -> codegen.
 * \p emit_lexer_output: collect token lines for lexer.txt (requirement_1).
 * \p emit_parser_output: collect token + syntax lines for parser.txt (requirement_2).
 * Caller writes error.txt / lexer.txt / parser.txt / symbol.txt / llvm_ir.txt from result. */
CompilerResult RunCompiler(const std::string& source, bool emit_lexer_output = true,
                           bool emit_parser_output = true);

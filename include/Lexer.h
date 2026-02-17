#pragma once

#include "Token.h"
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

/**
 * Lexer: reads source, advances via Next(), sets current token.
 * Errors are recorded as (line number, error code) in GetErrorLog().
 */
class Lexer {
public:
    explicit Lexer(const char* file_path);
    explicit Lexer(std::string&& source);

    ~Lexer() = default;

    const std::optional<Token>& GetCurrentToken() const {
        return cur_token_;
    }

    bool NotEnd() const {
        return cur_pos_ < source_.size();
    }

    /** Advance to next token. \p from_lookahead true => do not write to lexer_out (used by
     * PeekNext). */
    void Next(bool from_lookahead = false);

    /** Error log: (line number, error code). Merge with Parser::GetErrorLog() for error.txt. */
    const std::vector<std::pair<int, std::string>>& GetErrorLog() const {
        return error_log_;
    }

    /** When enabled, write "类别码 字符/字符串形式" per token to the given stream (e.g. lexer.txt).
     */
    void SetLexerOutput(std::ostream* out) {
        lexer_out_ = out;
    }
    void SetEmitLexerOutput(bool enable) {
        emit_lexer_output_ = enable;
    }

    /**
     * One-token lookahead: returns the next token without consuming it.
     * Use to distinguish productions that share a prefix (e.g. Ident for LVal vs
     * Ident '(' for function call). Implemented by save state -> Next() -> capture -> restore.
     */
    std::optional<Token> PeekNext();

    /**
     * Two-token lookahead: returns the token two positions ahead without consuming.
     * E.g. CompUnit: "int ident (" is FuncDef; "int ident ;" is Decl.
     */
    std::optional<Token> PeekNext2();

private:
    std::string source_;
    size_t cur_pos_;
    size_t line_num_;
    std::optional<Token> cur_token_;
    std::vector<std::pair<int, std::string>> error_log_;
    std::ostream* lexer_out_{nullptr};
    bool emit_lexer_output_{false};

    void SkipComment();
    void GetStringConst();
    void GetCharConst();
    void GetWord();
    void GetIntConst();
    void GetDelimitor();
};

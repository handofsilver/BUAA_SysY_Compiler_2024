#pragma once

#include "Token.h"
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Lexer: reads source, advances via Next(), sets current token. Errors: (line, code) in vector.

class Lexer {
public:
    explicit Lexer(const char* file_path);
    explicit Lexer(std::string&& source); // move: for Lexer(std::move(source)); disambiguates from file path

    ~Lexer() = default;

    const std::optional<Token>& GetCurrentToken() const {
        return cur_token_;
    }
    bool NotEnd() const {
        return cur_pos_ < source_.size();
    }
    void Next();

    /** Error log: (line number, error code). Output to error.txt when non-empty. */
    const std::vector<std::pair<int, std::string>>& GetErrorLog() const {
        return error_log_;
    }

private:
    std::string source_;
    size_t cur_pos_;
    size_t line_num_;
    std::optional<Token> cur_token_;
    std::vector<std::pair<int, std::string>> error_log_;

    void SkipComment();
    void GetStringConst();
    void GetCharConst();
    void GetWord();
    void GetIntConst();
    void GetOperator();
};

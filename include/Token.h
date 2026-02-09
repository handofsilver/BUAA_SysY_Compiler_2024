#pragma once

#include "TokenType.h"
#include <string>

// Token: type + line number + value (string form)
struct Token {
    TokenType type;
    int line_num;
    std::string value;

    Token(TokenType t, int line, std::string value) : type(t), line_num(line), value(std::move(value)) {
    }
};

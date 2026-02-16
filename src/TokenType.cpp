#include "TokenType.h"
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

std::string ToString(TokenType type) {
    switch (type) {
        // Identifier
        case TokenType::IDENFR: return "IDENFR";

        // Constant
        case TokenType::INTCON: return "INTCON";
        case TokenType::STRCON: return "STRCON";
        case TokenType::CHRCON: return "CHRCON";

        // Reserved Word
        case TokenType::MAINTK: return "MAINTK";
        case TokenType::CONSTTK: return "CONSTTK";
        case TokenType::INTTK: return "INTTK";
        case TokenType::CHARTK: return "CHARTK";
        case TokenType::BREAKTK: return "BREAKTK";
        case TokenType::CONTINUETK: return "CONTINUETK";
        case TokenType::IFTK: return "IFTK";
        case TokenType::ELSETK: return "ELSETK";
        case TokenType::VOIDTK: return "VOIDTK";
        case TokenType::FORTK: return "FORTK";
        case TokenType::GETINTTK: return "GETINTTK";
        case TokenType::GETCHARTK: return "GETCHARTK";
        case TokenType::PRINTFTK: return "PRINTFTK";
        case TokenType::RETURNTK: return "RETURNTK";

        // Operator and Punctuator
        case TokenType::NOT: return "NOT";
        case TokenType::AND: return "AND";
        case TokenType::OR: return "OR";
        case TokenType::MULT: return "MULT";
        case TokenType::DIV: return "DIV";
        case TokenType::MOD: return "MOD";
        case TokenType::LSS: return "LSS";
        case TokenType::LEQ: return "LEQ";
        case TokenType::GRE: return "GRE";
        case TokenType::GEQ: return "GEQ";
        case TokenType::EQL: return "EQL";
        case TokenType::NEQ: return "NEQ";
        case TokenType::PLUS: return "PLUS";
        case TokenType::MINU: return "MINU";
        case TokenType::ASSIGN: return "ASSIGN";
        case TokenType::SEMICN: return "SEMICN";
        case TokenType::COMMA: return "COMMA";
        case TokenType::LPARENT: return "LPARENT";
        case TokenType::RPARENT: return "RPARENT";
        case TokenType::LBRACK: return "LBRACK";
        case TokenType::RBRACK: return "RBRACK";
        case TokenType::LBRACE: return "LBRACE";
        case TokenType::RBRACE: return "RBRACE";
        default: return "UNKNOWN";
    }
}

std::optional<TokenType> GetDelimitorType(std::string_view delimitor) {
    static const std::unordered_map<std::string, TokenType> kDelimitorMap = {
        {"!",  TokenType::NOT    },
        {"&&", TokenType::AND    },
        {"||", TokenType::OR     },
        {"*",  TokenType::MULT   },
        {"/",  TokenType::DIV    },
        {"%",  TokenType::MOD    },
        {"<",  TokenType::LSS    },
        {"<=", TokenType::LEQ    },
        {">",  TokenType::GRE    },
        {">=", TokenType::GEQ    },
        {"==", TokenType::EQL    },
        {"!=", TokenType::NEQ    },
        {"+",  TokenType::PLUS   },
        {"-",  TokenType::MINU   },
        {"=",  TokenType::ASSIGN },
        {";",  TokenType::SEMICN },
        {",",  TokenType::COMMA  },
        {"(",  TokenType::LPARENT},
        {")",  TokenType::RPARENT},
        {"[",  TokenType::LBRACK },
        {"]",  TokenType::RBRACK },
        {"{",  TokenType::LBRACE },
        {"}",  TokenType::RBRACE },
    };
    auto it = kDelimitorMap.find(std::string(delimitor));
    if (it != kDelimitorMap.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<TokenType> GetReservedWordType(std::string_view word) {
    static const std::unordered_map<std::string, TokenType> kReservedWordMap = {
        {"main",     TokenType::MAINTK    },
        {"const",    TokenType::CONSTTK   },
        {"int",      TokenType::INTTK     },
        {"char",     TokenType::CHARTK    },
        {"break",    TokenType::BREAKTK   },
        {"continue", TokenType::CONTINUETK},
        {"if",       TokenType::IFTK      },
        {"else",     TokenType::ELSETK    },
        {"void",     TokenType::VOIDTK    },
        {"for",      TokenType::FORTK     },
        {"getint",   TokenType::GETINTTK  },
        {"getchar",  TokenType::GETCHARTK },
        {"printf",   TokenType::PRINTFTK  },
        {"return",   TokenType::RETURNTK  },
    };
    auto it = kReservedWordMap.find(std::string(word));
    if (it != kReservedWordMap.end()) {
        return it->second;
    }
    return std::nullopt;
}

#include "TokenType.h"
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

std::string toString(TokenType type) {
    // 用 switch (type) 为每种 TokenType 返回对应的类别码字符串
    // 参考 requirement_1 表格，如 TokenType::IDENFR -> "IDENFR", INTCON -> "INTCON" 等。
    switch (type) {
        // 标识符 Identifier
        case TokenType::IDENFR:
            return "IDENFR";

        // 常量 Constant
        case TokenType::INTCON:
            return "INTCON";
        case TokenType::STRCON:
            return "STRCON";
        case TokenType::CHRCON:
            return "CHRCON";

        // 保留字 Reserved Word
        case TokenType::MAINTK:
            return "MAINTK";
        case TokenType::CONSTTK:
            return "CONSTTK";
        case TokenType::INTTK:
            return "INTTK";
        case TokenType::CHARTK:
            return "CHARTK";
        case TokenType::BREAKTK:
            return "BREAKTK";
        case TokenType::CONTINUETK:
            return "CONTINUETK";
        case TokenType::IFTK:
            return "IFTK";
        case TokenType::ELSETK:
            return "ELSETK";
        case TokenType::VOIDTK:
            return "VOIDTK";
        case TokenType::FORTK:
            return "FORTK";
        case TokenType::GETINTTK:
            return "GETINTTK";
        case TokenType::GETCHARTK:
            return "GETCHARTK";
        case TokenType::PRINTFTK:
            return "PRINTFTK";
        case TokenType::RETURNTK:
            return "RETURNTK";

        // 运算符与界符 Operator and Punctuator
        case TokenType::NOT:
            return "NOT";
        case TokenType::AND:
            return "AND";
        case TokenType::OR:
            return "OR";
        case TokenType::MULT:
            return "MULT";
        case TokenType::DIV:
            return "DIV";
        case TokenType::MOD:
            return "MOD";
        case TokenType::LSS:
            return "LSS";
        case TokenType::LEQ:
            return "LEQ";
        case TokenType::GRE:
            return "GRE";
        case TokenType::GEQ:
            return "GEQ";
        case TokenType::EQL:
            return "EQL";
        case TokenType::NEQ:
            return "NEQ";
        case TokenType::PLUS:
            return "PLUS";
        case TokenType::MINU:
            return "MINU";
        case TokenType::ASSIGN:
            return "ASSIGN";
        case TokenType::SEMICN:
            return "SEMICN";
        case TokenType::COMMA:
            return "COMMA";
        case TokenType::LPARENT:
            return "LPARENT";
        case TokenType::RPARENT:
            return "RPARENT";
        case TokenType::LBRACK:
            return "LBRACK";
        case TokenType::RBRACK:
            return "RBRACK";
        case TokenType::LBRACE:
            return "LBRACE";
        case TokenType::RBRACE:
            return "RBRACE";
        default:
            return "UNKNOWN";
    }
}

std::optional<TokenType> getOperatorType(std::string_view op) {
    static const std::unordered_map<std::string, TokenType> operatorMap = {
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
    auto it = operatorMap.find(std::string(op));
    if (it != operatorMap.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<TokenType> getReservedWordType(std::string_view word) {
    static const std::unordered_map<std::string, TokenType> reservedWordMap = {
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
    auto it = reservedWordMap.find(std::string(word));
    if (it != reservedWordMap.end()) {
        return it->second;
    }
    return std::nullopt;
}
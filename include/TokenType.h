#pragma once

#include <optional>
#include <string>
#include <string_view>

// TokenType: the type of the token
enum class TokenType {
    // Identifier
    IDENFR, // <Ident>

    // Constant
    INTCON, // <IntConst>
    STRCON, // <StringConst>
    CHRCON, // <CharConst>

    // Reserved Word
    MAINTK,     // main
    CONSTTK,    // const
    INTTK,      // int
    CHARTK,     // char
    BREAKTK,    // break
    CONTINUETK, // continue
    IFTK,       // if
    ELSETK,     // else
    VOIDTK,     // void
    FORTK,      // for
    GETINTTK,   // getint
    GETCHARTK,  // getchar
    PRINTFTK,   // printf
    RETURNTK,   // return

    // Operator and Punctuator
    NOT,     // !
    AND,     // &&
    OR,      // ||
    MULT,    // *
    DIV,     // /
    MOD,     // %
    LSS,     // <
    LEQ,     // <=
    GRE,     // >
    GEQ,     // >=
    EQL,     // ==
    NEQ,     // !=
    PLUS,    // +
    MINU,    // -
    ASSIGN,  // =
    SEMICN,  // ;
    COMMA,   // ,
    LPARENT, // (
    RPARENT, // )
    LBRACK,  // [
    RBRACK,  // ]
    LBRACE,  // {
    RBRACE,  // }
};

// convert the token type to a string for output, e.g. "IDENFR", "INTCON"
std::string ToString(TokenType type);

std::optional<TokenType> GetOperatorType(std::string_view op);

std::optional<TokenType> GetReservedWordType(std::string_view word);

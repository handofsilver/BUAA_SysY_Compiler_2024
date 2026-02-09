#pragma once

#include <optional>
#include <string>
#include <string_view>

// =============================================================================
// 词法单元类型（与 requirement_1_lexer.md 表格一致）
// C++ 使用 enum class 避免与整型隐式转换，且枚举值在 TokenType 命名空间下。
// =============================================================================

enum class TokenType {
    // 标识符 Identifier
    IDENFR, // <Ident>

    // 常量 Constant
    INTCON, // <IntConst>
    STRCON, // <StringConst>
    CHRCON, // <CharConst>

    // 保留字 Reserved Word
    MAINTK,
    CONSTTK,
    INTTK,
    CHARTK,
    BREAKTK,
    CONTINUETK,
    IFTK,
    ELSETK,
    VOIDTK,
    FORTK,
    GETINTTK,
    GETCHARTK,
    PRINTFTK,
    RETURNTK,

    // 运算符与界符 Operator and Punctuator
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

// 将类别码转为输出用的字符串（如 "IDENFR", "INTCON"），用于 lexer.txt。
// 对应 Java 里输出时用枚举名；这里集中在一处便于维护。
std::string toString(TokenType type);

std::optional<TokenType> getOperatorType(std::string_view op);

std::optional<TokenType> getReservedWordType(std::string_view word);

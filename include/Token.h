#pragma once

#include "TokenType.h"
#include <string>

// =============================================================================
// 词法单元：类型 + 行号 + 字符串形式的值（与 requirement_1 要求一致）
// =============================================================================
//
// 【教学点】为什么用 std::string 而不是 Java 的“引用”？
// - Java 里 String 是对象，变量存的是引用；赋值/传参是复制引用，不复制字符。
// - C++ 里 std::string 是值类型：赋值、按值传参会复制整段字符（拷贝语义）。
//   这样每个 Token 自己持有一份 value，生命周期清晰，不依赖外部字符串是否仍存在。
// - 若不想复制大字符串，可用 std::string_view（只读视图）或 const std::string& 传参；
//   但作为“存储”时，Token 需要拥有这份数据，用 std::string 更合适。
// =============================================================================

struct Token {
    TokenType type;
    int lineNum;       // 行号，从 1 开始（输出与错误提示用）
    std::string value; // 单词的字符/字符串形式，原样保留便于输出

    Token(TokenType t, int line, std::string val) : type(t), lineNum(line), value(std::move(val)) {}
};

#pragma once

#include "Token.h"
#include <optional>
#include <string>
#include <utility>
#include <vector>

// =============================================================================
// 词法分析器骨架。对应 Java Lexer，源文件读入 source，通过 next() 推进并设置 curToken。
// 错误记录：行号 + 错误类别码（如 "a"），用 vector<pair<int, string>> 而非 ArrayList<String>。
// =============================================================================

class Lexer {
public:
    // 从文件路径读取整份源码到 source_（如 testfile.txt）。
    explicit Lexer(const std::string& filePath);
    // 或直接传入已读入的源码字符串（与 Java Lexer(String source) 一致）。
    explicit Lexer(std::string source);

    std::optional<Token> getCurrentToken() const {
        return curToken_;
    }
    bool notEnd() const {
        return curPos_ < static_cast<int>(source_.size());
    }
    void next();

    // 错误列表：(行号, 错误类别码)。按行号从小到大输出到 error.txt。
    const std::vector<std::pair<int, std::string>>& getErrorLog() const {
        return errorLog_;
    }

private:
    std::string source_;
    int curPos_;
    int lineNum_;
    std::optional<Token> curToken_;
    std::vector<std::pair<int, std::string>> errorLog_;

    void skipComment();
    void getStringConst();
    void getCharConst();
    void getWord();
    void getIntConst();
    void getOperator();
};

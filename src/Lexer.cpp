#include "Lexer.h"
#include "TokenType.h"
#include <fstream>
#include <sstream>

Lexer::Lexer(std::string source) : source_(std::move(source)), curPos_(0), lineNum_(0), curToken_(std::nullopt) {}

Lexer::Lexer(const std::string& filePath) : curPos_(0), lineNum_(0), curToken_(std::nullopt) {
    std::ifstream f(filePath);
    std::ostringstream oss;
    oss << f.rdbuf();
    source_ = oss.str();
}

void Lexer::next() {
    curToken_ = std::nullopt;
    // TODO：对应 Java next()：while (curPos < source.length()) 根据当前字符分支：
    //   '"' -> getStringConst(); break;
    //   '\'' -> getCharConst(); break;
    //   字母或'_' -> getWord(); break;
    //   数字 -> getIntConst(); break;
    //   空白 -> 若 '\n' 则 lineNum++; curPos++; 继续循环
    //   '/' 且下一个是 '/' 或 '*' -> skipComment(); 继续循环
    //   否则 -> getOperator(); break;

    while (curPos_ < source_.length()) {
        char currentChar = source_[curPos_];

        // string const
        if (currentChar == '\"') {
            getStringConst();
            break;
        }
        // char const
        else if (currentChar == '\'') {
            getCharConst();
            break;
        }
        // int const
        else if (std::isdigit(currentChar)) {
            getIntConst();
            break;
        }
        // word(identifiers or reserved words)
        else if (std::isalpha(currentChar) || currentChar == '_') {
            getWord();
            break;
        }
        // others(whitespaces, comments, operators)
        else {
            // whitespaces
            if (std::isspace(currentChar)) {
                if (currentChar == '\n') {
                    lineNum_++;
                }
                curPos_++;
            }
            // comments
            else if (currentChar == '/' && curPos_ + 1 < source_.length()) {
                char nextChar = source_[curPos_ + 1];
                if (nextChar == '/' || nextChar == '*') {
                    skipComment();
                } else {
                    getOperator();
                }
            }
        }
    }
}

void Lexer::skipComment() {
    // TODO：对应 Java handleComment()：curPos++ 跳过第一个 '/'；
    // 若下一字符是 '/'：单行注释，循环直到 '\n'，再 lineNum++, curPos++；
    // 若下一字符是 '*'：多行注释，循环找 "*/"，途中遇到 '\n' 则 lineNum++。

    curPos_++;

    if (curPos_ < source_.size() && source_[curPos_] == '/') {
        // single line comment
        while (curPos_ < source_.size() && source_[curPos_] != '\n') {
            curPos_++;
        }
        if (curPos_ < source_.size() && source_[curPos_] == '\n') {
            lineNum_++;
            curPos_++;
        }
        // end of single line comment
    } else if (curPos_ < source_.size() && source_[curPos_] == '*') {
        // multi line comment
        curPos_++;
        while (curPos_ < source_.size()) {
            while (curPos_ < source_.size() && source_[curPos_] != '*') { // stage 0
                if (source_[curPos_] == '\n') {
                    lineNum_++;
                }
                curPos_++;
            }
            while (curPos_ < source_.size() && source_[curPos_] == '*') {
                curPos_++;
            }
            if (curPos_ < source_.size() && source_[curPos_] == '/') {
                curPos_++;
                break;
            }
        }
        // end of multi line comment
    }
}

void Lexer::getStringConst() {
    // TODO：对应 Java getStringConst()：从当前 '"' 开始，用 string 或 stringstream
    // 收集字符直到遇到未转义的 '"'；处理 \\ 转义；最后 curToken_ = Token(STRCON, lineNum_+1, 收集的串)。

    std::string stringConst = "";

    do {
        stringConst += source_[curPos_++];
        if (source_[curPos_] == '\\') {
            stringConst += source_[curPos_++];
            stringConst += source_[curPos_++];
        }
    } while (curPos_ < source_.size() && source_[curPos_] != '\"');

    curToken_ = Token(TokenType::STRCON, lineNum_ + 1, stringConst);
}

void Lexer::getCharConst() {
    // TODO：对应 Java getCharConst()：从当前 '\'' 开始收集直到配对 '\''，
    // 处理转义；curToken_ = Token(CHRCON, lineNum_+1, 收集的串)。

    std::string charConst = "";

    do {
        charConst += source_[curPos_++];
        if (source_[curPos_] == '\\') {
            charConst += source_[curPos_++];
            charConst += source_[curPos_++];
        }
    } while (curPos_ < source_.size() && source_[curPos_] != '\'');

    if (curPos_ < source_.size() && source_[curPos_] == '\'') {
        charConst += source_[curPos_++];
    }

    curToken_ = Token(TokenType::CHRCON, lineNum_ + 1, charConst);
}

void Lexer::getWord() {
    // TODO：对应 Java getWord()：循环收集字母/数字/'_'；然后判断是否为保留字
    // （可维护 map<string, TokenType> 或 if-else），是则 curToken_ = Token(保留字类型, lineNum_+1, 串)，
    // 否则 curToken_ = Token(IDENFR, lineNum_+1, 串)。
    std::string word = "";
    while (curPos_ < source_.size() && (std::isalpha(source_[curPos_]) || std::isdigit(source_[curPos_]) || source_[curPos_] == '_')) {
        word += source_[curPos_];
        curPos_++;
    }

    if (getReservedWordType(word)) {
        curToken_ = Token(getReservedWordType(word).value(), lineNum_ + 1, word);
    } else {
        curToken_ = Token(TokenType::IDENFR, lineNum_ + 1, word);
    }
}

void Lexer::getIntConst() {
    // TODO：对应 Java getIntConst()：循环收集数字，curToken_ = Token(INTCON, lineNum_+1, 串)。
    std::string intConst = "";

    while (curPos_ < source_.size() && (std::isdigit(source_[curPos_]))) {
        intConst += source_[curPos_];
        curPos_++;
    }

    curToken_ = Token(TokenType::INTCON, lineNum_ + 1, intConst);
}

void Lexer::getOperator() {
    // TODO：对应 Java getOperator()：先看 curPos 起的两字符是否在运算符表（如 "&&","||","<=",">=","==","!="），
    // 是则 curPos+=2 并设置对应 Token；否则看一字符运算符；若都不是则 errorLog_.push_back({lineNum_+1, "a"})，curPos++。
    if (curPos_ + 1 < source_.size() && getOperatorType(source_.substr(curPos_, 2))) {
        curToken_ = Token(getOperatorType(source_.substr(curPos_, 2)).value(), lineNum_ + 1, source_.substr(curPos_, 2));
        curPos_ += 2;
    } else if (curPos_ + 1 < source_.size() && getOperatorType(source_.substr(curPos_, 1))) {
        curToken_ = Token(getOperatorType(source_.substr(curPos_, 1)).value(), lineNum_ + 1, source_.substr(curPos_, 1));
        curPos_ += 1;
    } else {
        errorLog_.push_back({lineNum_ + 1, "a"});
    }
}
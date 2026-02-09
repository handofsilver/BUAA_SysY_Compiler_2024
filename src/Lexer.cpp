#include "Lexer.h"
#include "TokenType.h"
#include <cctype>
#include <fstream>
#include <sstream>
#include <string_view>

Lexer::Lexer(std::string source) : source_(std::move(source)), curPos_(0), lineNum_(0), curToken_(std::nullopt) {}

Lexer::Lexer(const std::string& filePath) : curPos_(0), lineNum_(0), curToken_(std::nullopt) {
    std::ifstream f(filePath);
    std::ostringstream oss;
    oss << f.rdbuf();
    source_ = oss.str();
}

void Lexer::next() {
    curToken_ = std::nullopt;
    //   while (curPos < source.length()) 根据当前字符分支：
    //   '"' -> getStringConst(); break;
    //   '\'' -> getCharConst(); break;
    //   letter or '_' -> getWord(); break;
    //   number(digits) -> getIntConst(); break;
    //   whitespace -> if '\n' then lineNum++; curPos++; continue loop
    //   '/' and the next one is '/' or '*' -> skipComment(); continue loop
    //   otherwise -> getOperator(); break;

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
            if (std::isspace(currentChar)) {
                if (currentChar == '\n') {
                    lineNum_++;
                }
                curPos_++;
            } else if (currentChar == '/' && curPos_ + 1 < static_cast<int>(source_.size())) {
                char nextChar = source_[curPos_ + 1];
                if (nextChar == '/' || nextChar == '*') {
                    skipComment();
                } else {
                    // DIV ('/') operator
                    getOperator();
                    break;
                }
            } else {
                getOperator();
                break;
            }
        }
    }
}

void Lexer::skipComment() {
    // curPos++ to skip the first '/'
    // if the next character is '/'：single line comment, loop until '\n', then lineNum++, curPos++
    // if the next character is '*'：multi line comment, loop until "*/", meet '\n' then lineNum++

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
    // from the current '"' to the unescaped '"'
    // handle \\ escape
    // finally curToken_ = Token(STRCON, lineNum_+1, collected string)

    std::string stringConst;
    stringConst.reserve(64);

    do {
        stringConst += source_[curPos_++];
        if (source_[curPos_] == '\\') {
            stringConst += source_[curPos_++];
            stringConst += source_[curPos_++];
        }
    } while (curPos_ < static_cast<int>(source_.size()) && source_[curPos_] != '\"');

    curToken_ = Token(TokenType::STRCON, lineNum_ + 1, std::move(stringConst));
}

void Lexer::getCharConst() {
    // from the current '\'' to the unescaped '\''
    // handle \\ escape
    // finally curToken_ = Token(CHRCON, lineNum_+1, collected string)

    std::string charConst;
    charConst.reserve(64);

    do {
        charConst += source_[curPos_++];
        if (curPos_ < static_cast<int>(source_.size()) && source_[curPos_] == '\\') {
            charConst += source_[curPos_++];
            charConst += source_[curPos_++];
        }
    } while (curPos_ < static_cast<int>(source_.size()) && source_[curPos_] != '\'');

    if (curPos_ < static_cast<int>(source_.size()) && source_[curPos_] == '\'') {
        charConst += source_[curPos_++];
    }

    curToken_ = Token(TokenType::CHRCON, lineNum_ + 1, std::move(charConst));
}

void Lexer::getWord() {
    // TODO：对应 Java getWord()：循环收集字母/数字/'_'；然后判断是否为保留字
    // （可维护 map<string, TokenType> 或 if-else），是则 curToken_ = Token(保留字类型, lineNum_+1, 串)，
    // 否则 curToken_ = Token(IDENFR, lineNum_+1, 串)。
    std::string word;
    word.reserve(64);
    while (curPos_ < static_cast<int>(source_.size()) &&
           (std::isalpha(static_cast<unsigned char>(source_[curPos_])) ||
            std::isdigit(static_cast<unsigned char>(source_[curPos_])) || source_[curPos_] == '_')) {
        word += static_cast<char>(source_[curPos_]);
        curPos_++;
    }

    auto reserved = getReservedWordType(word);
    if (reserved) {
        curToken_ = Token(reserved.value(), lineNum_ + 1, std::move(word));
    } else {
        curToken_ = Token(TokenType::IDENFR, lineNum_ + 1, std::move(word));
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
    const auto length = static_cast<int>(source_.size());
    if (curPos_ + 2 <= length) {
        std::string_view two(source_.data() + curPos_, 2);
        auto opt = getOperatorType(two);
        if (opt) {
            curToken_ = Token(opt.value(), lineNum_ + 1, std::string(two));
            curPos_ += 2;
            return;
        }
    }
    if (curPos_ < length) {
        std::string_view one(source_.data() + curPos_, 1);
        auto opt = getOperatorType(one);
        if (opt) {
            curToken_ = Token(opt.value(), lineNum_ + 1, std::string(one));
            curPos_ += 1;
            return;
        }
    }
    errorLog_.push_back({lineNum_ + 1, "a"});
    curPos_++;
}
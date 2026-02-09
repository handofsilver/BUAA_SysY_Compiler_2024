#include "Lexer.h"
#include "TokenType.h"
#include <cctype>
#include <fstream>
#include <sstream>
#include <string_view>

Lexer::Lexer(std::string&& source) : source_(std::move(source)), cur_pos_(0), line_num_(0), cur_token_(std::nullopt) {}

Lexer::Lexer(const char* file_path) : cur_pos_(0), line_num_(0), cur_token_(std::nullopt) {
    std::ifstream f(file_path);
    std::ostringstream oss;
    oss << f.rdbuf();
    source_ = oss.str();
}

void Lexer::Next() {
    cur_token_ = std::nullopt;

    while (cur_pos_ < source_.size()) {
        char cur_ch = source_[cur_pos_];

        if (cur_ch == '\"') {
            GetStringConst();
            break;
        }
        if (cur_ch == '\'') {
            GetCharConst();
            break;
        }
        if (std::isdigit(static_cast<unsigned char>(cur_ch))) {
            GetIntConst();
            break;
        }
        if (std::isalpha(static_cast<unsigned char>(cur_ch)) || cur_ch == '_') {
            GetWord();
            break;
        }

        if (std::isspace(cur_ch)) {
            if (cur_ch == '\n') {
                line_num_++;
            }
            cur_pos_++;
        } else if (cur_ch == '/' && cur_pos_ + 1 < source_.size()) {
            char next_ch = source_[cur_pos_ + 1];
            if (next_ch == '/' || next_ch == '*') {
                SkipComment();
            } else {
                GetOperator();
                break;
            }
        } else {
            GetOperator();
            break;
        }
    }
}

void Lexer::SkipComment() {
    cur_pos_++;

    if (cur_pos_ < source_.size() && source_[cur_pos_] == '/') {
        while (cur_pos_ < source_.size() && source_[cur_pos_] != '\n') {
            cur_pos_++;
        }
        if (cur_pos_ < source_.size() && source_[cur_pos_] == '\n') {
            line_num_++;
            cur_pos_++;
        }
    } else if (cur_pos_ < source_.size() && source_[cur_pos_] == '*') {
        cur_pos_++;
        while (cur_pos_ < source_.size()) {
            while (cur_pos_ < source_.size() && source_[cur_pos_] != '*') {
                if (source_[cur_pos_] == '\n') {
                    line_num_++;
                }
                cur_pos_++;
            }
            while (cur_pos_ < source_.size() && source_[cur_pos_] == '*') {
                cur_pos_++;
            }
            if (cur_pos_ < source_.size() && source_[cur_pos_] == '/') {
                cur_pos_++;
                break;
            }
        }
    }
}

void Lexer::GetStringConst() {
    std::string str_const;
    str_const.reserve(64);
    do {
        str_const += source_[cur_pos_++];
        if (cur_pos_ < source_.size() && source_[cur_pos_] == '\\') {
            str_const += source_[cur_pos_++];
            if (cur_pos_ < source_.size()) {
                str_const += source_[cur_pos_++];
            }
        }
    } while (cur_pos_ < source_.size() && source_[cur_pos_] != '\"');
    if (cur_pos_ < source_.size() && source_[cur_pos_] == '\"') {
        str_const += source_[cur_pos_++];
    }
    cur_token_ = Token(TokenType::STRCON, static_cast<int>(line_num_ + 1), std::move(str_const));
}

void Lexer::GetCharConst() {
    std::string char_const;
    char_const.reserve(64);
    do {
        char_const += source_[cur_pos_++];
        if (cur_pos_ < source_.size() && source_[cur_pos_] == '\\') {
            char_const += source_[cur_pos_++];
            if (cur_pos_ < source_.size()) {
                char_const += source_[cur_pos_++];
            }
        }
    } while (cur_pos_ < source_.size() && source_[cur_pos_] != '\'');
    if (cur_pos_ < source_.size() && source_[cur_pos_] == '\'') {
        char_const += source_[cur_pos_++];
    }
    cur_token_ = Token(TokenType::CHRCON, static_cast<int>(line_num_ + 1), std::move(char_const));
}

void Lexer::GetWord() {
    std::string word;
    word.reserve(64);
    while (cur_pos_ < source_.size() &&
           (std::isalpha(static_cast<unsigned char>(source_[cur_pos_])) ||
            std::isdigit(static_cast<unsigned char>(source_[cur_pos_])) || source_[cur_pos_] == '_')) {
        word += source_[cur_pos_];
        cur_pos_++;
    }
    auto reserved = GetReservedWordType(word);
    if (reserved) {
        cur_token_ = Token(reserved.value(), static_cast<int>(line_num_ + 1), std::move(word));
    } else {
        cur_token_ = Token(TokenType::IDENFR, static_cast<int>(line_num_ + 1), std::move(word));
    }
}

void Lexer::GetIntConst() {
    std::string int_const;
    int_const.reserve(32);
    while (cur_pos_ < source_.size() && std::isdigit(static_cast<unsigned char>(source_[cur_pos_]))) {
        int_const += source_[cur_pos_];
        cur_pos_++;
    }
    cur_token_ = Token(TokenType::INTCON, static_cast<int>(line_num_ + 1), std::move(int_const));
}

void Lexer::GetOperator() {
    if (cur_pos_ + 2 <= source_.size()) {
        std::string_view two(source_.data() + cur_pos_, 2);
        auto opt = GetOperatorType(two);
        if (opt) {
            cur_token_ = Token(opt.value(), static_cast<int>(line_num_ + 1), std::string(two));
            cur_pos_ += 2;
            return;
        }
    }
    if (cur_pos_ < source_.size()) {
        std::string_view one(source_.data() + cur_pos_, 1);
        auto opt = GetOperatorType(one);
        if (opt) {
            cur_token_ = Token(opt.value(), static_cast<int>(line_num_ + 1), std::string(one));
            cur_pos_ += 1;
            return;
        }
    }
    error_log_.push_back({static_cast<int>(line_num_ + 1), "a"});
    cur_pos_++;
}

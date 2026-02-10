#include "Lexer.h"
#include "TokenType.h"
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

int main() {
    std::ifstream in("testfile.txt");
    if (!in) {
        return 1;
    }
    std::string source((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    Lexer lex(std::move(source));
    std::vector<Token> tokens;

    while (lex.NotEnd()) {
        lex.Next();
        const auto& cur = lex.GetCurrentToken();
        if (cur.has_value()) {
            tokens.push_back(cur.value());
        }
    }

    const auto& error_log = lex.GetErrorLog();
    if (!error_log.empty()) {
        std::ofstream error_out("error.txt");
        for (const auto& p : error_log) {
            error_out << p.first << " " << p.second << "\n";
        }
    } else {
        std::ofstream lex_out("lexer.txt");
        for (const auto& tok : tokens) {
            lex_out << ToString(tok.type) << " " << tok.value << "\n";
        }
    }

    return 0;
}

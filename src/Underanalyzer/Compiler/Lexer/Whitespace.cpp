
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Lexer/Whitespace.h"

#include <cctype>

namespace Underanalyzer::Compiler::Lexer {

int Whitespace::Skip(std::string_view text, int startPosition) {
    int pos = startPosition;
    while (true) {
        while (pos < static_cast<int>(text.size()) && std::isspace(static_cast<unsigned char>(text[pos]))) {
            pos++;
        }
        if (pos + 1 < static_cast<int>(text.size()) && text[pos] == '/' &&
            (text[pos + 1] == '/' || text[pos + 1] == '*')) {
            switch (text[pos + 1]) {
                case '/':
                    pos += 2;
                    while (pos < static_cast<int>(text.size())) {
                        if (text[pos++] == '\n')
                            break;
                    }
                    break;
                case '*':
                    pos += 2;
                    while (pos < static_cast<int>(text.size())) {
                        if (text[pos++] == '*') {
                            if (pos < static_cast<int>(text.size()) && text[pos] == '/') {
                                pos++;
                                break;
                            }
                        }
                    }
                    break;
            }
        } else {
            break;
        }
    }
    return pos;
}

} // namespace Underanalyzer::Compiler::Lexer

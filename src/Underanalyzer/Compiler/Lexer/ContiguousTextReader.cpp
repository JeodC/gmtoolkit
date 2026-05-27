
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Lexer/ContiguousTextReader.h"

#include <cctype>

namespace Underanalyzer::Compiler::Lexer {

int ContiguousTextReader::ReadUntilWhitespace(std::string_view text, int startPosition, std::string_view& output) {
    int pos = startPosition;
    while (pos < static_cast<int>(text.size())) {
        if (!std::isspace(static_cast<unsigned char>(text[pos]))) {
            pos++;
        } else {
            break;
        }
    }
    output = text.substr(startPosition, pos - startPosition);
    return pos;
}

int ContiguousTextReader::ReadUntilChar(std::string_view text, int startPosition, char c, std::string_view& output) {
    int pos = startPosition;
    while (pos < static_cast<int>(text.size())) {
        if (text[pos] != c) {
            pos++;
        } else {
            break;
        }
    }
    output = text.substr(startPosition, pos - startPosition);
    return pos;
}

int ContiguousTextReader::ReadWhileIdentifier(std::string_view text, int startPosition, std::string_view& output) {
    int pos = startPosition;
    while (pos < static_cast<int>(text.size())) {
        char c = text[pos];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
            pos++;
        } else {
            break;
        }
    }
    output = text.substr(startPosition, pos - startPosition);
    return pos;
}

// Compiler-internal names like @@self@@ embed '@' inside an otherwise normal identifier;
// require at least one non-'@' character so a bare "@@@" isn't accepted as a name.
int ContiguousTextReader::ReadWhileInternalIdentifier(std::string_view text, int startPosition,
                                                      std::string_view& output, bool& anyNormalChars) {
    anyNormalChars = false;
    int pos = startPosition;
    while (pos < static_cast<int>(text.size())) {
        char c = text[pos];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '@') {
            pos++;
            if (c != '@')
                anyNormalChars = true;
        } else {
            break;
        }
    }
    output = text.substr(startPosition, pos - startPosition);
    return pos;
}

// At most one decimal point per literal; further dots terminate the number so
// e.g. "1.2.3" parses as "1.2" then dot accessor then "3".
int ContiguousTextReader::ReadWhileNumber(std::string_view text, int startPosition, std::string_view& output) {
    int pos = startPosition;
    bool usedDot = false;
    while (pos < static_cast<int>(text.size())) {
        char c = text[pos];
        if (std::isdigit(static_cast<unsigned char>(c)) || (!usedDot && c == '.')) {
            if (c == '.')
                usedDot = true;
            pos++;
        } else {
            break;
        }
    }
    output = text.substr(startPosition, pos - startPosition);
    return pos;
}

int ContiguousTextReader::ReadWhileHex(std::string_view text, int startPosition, std::string_view& output) {
    int pos = startPosition;
    while (pos < static_cast<int>(text.size())) {
        char c = text[pos];
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')) {
            pos++;
        } else {
            break;
        }
    }
    output = text.substr(startPosition, pos - startPosition);
    return pos;
}

} // namespace Underanalyzer::Compiler::Lexer

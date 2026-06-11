
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Lexer/Strings.h"

#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/Lexer/ContiguousTextReader.h"
#include "Underanalyzer/Compiler/Lexer/LexContext.h"

#include <stdexcept>
#include <string>

namespace Underanalyzer::Compiler::Lexer {

static int HexCharToInt(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return 10 + (c - 'A');
    if (c >= 'a' && c <= 'f')
        return 10 + (c - 'a');
    return -1;
}

// Encode a Unicode code point into UTF-8 bytes appended to sb. Mirrors the encoding
// the upstream relies on .NET's char/string types to do implicitly.
static void AppendUtf32(std::string& sb, int codepoint) {
    // Surrogates rejected to match .NET char.ConvertFromUtf32, which throws
    // for U+D800-U+DFFF (upstream surfaces that as a compile error).
    if (codepoint < 0 || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
        throw std::out_of_range("code point out of range");
    }
    if (codepoint < 0x80) {
        sb.push_back(static_cast<char>(codepoint));
    } else if (codepoint < 0x800) {
        sb.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        sb.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint < 0x10000) {
        sb.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        sb.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        sb.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        sb.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        sb.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        sb.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        sb.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

// Verbatim strings take everything between the delimiters literally; no escape handling.
// The '@' prefix form picks up the real opener (' or ") from the next character.
int Strings::ParseVerbatim(LexContext& context, int startPosition, char startChar) {
    int pos = startPosition;
    if (startChar == '@') {
        startChar = context.Text()[pos + 1];
        pos++;
    }

    std::string_view str;
    pos = ContiguousTextReader::ReadUntilChar(context.Text(), pos + 1, startChar, str);

    if (pos >= static_cast<int>(context.Text().size())) {
        context.CompileContextRef().PushError("String not closed", context, startPosition);
        return pos;
    }

    context.Tokens().push_back(context.Arena().New<TokenString>(
        context, startPosition, std::string(context.Text().substr(startPosition, (pos + 1) - startPosition)),
        std::string(str)));
    return pos + 1;
}

int Strings::ParseRegular(LexContext& context, int startPosition) {
    std::string_view text = context.Text();
    std::string sb;
    sb.reserve(64);
    int pos = startPosition + 1;
    bool newlineErroredAlready = false;
    while (pos < static_cast<int>(text.size())) {
        char c = text[pos];
        if (c == '"')
            break;

        if (c == '\n') {
            // Keep scanning past the newline so we still find the closing quote,
            // but only report the error once per string.
            if (!newlineErroredAlready) {
                newlineErroredAlready = true;
                context.CompileContextRef().PushError(
                    "Direct newline found in string (should use \"\\n\" instead, or a raw string literal)", context,
                    startPosition);
            }
            pos++;
            continue;
        }

        if (c == '\\' && pos + 1 < static_cast<int>(text.size())) {
            int escapeStartPos = pos;
            char escapedChar = text[pos + 1];
            pos += 2;
            switch (escapedChar) {
                case 'a':
                    sb.push_back('\a');
                    break;
                case 'b':
                    sb.push_back('\b');
                    break;
                case 'f':
                    sb.push_back('\f');
                    break;
                case 'n':
                    sb.push_back('\n');
                    break;
                case 'r':
                    sb.push_back('\r');
                    break;
                case 't':
                    sb.push_back('\t');
                    break;
                case 'v':
                    sb.push_back('\v');
                    break;
                case 'u': {
                    // Up to 6 hex digits, greedy; stops at the first non-hex character.
                    int result = 0;
                    int charsRead = 0;
                    while (pos < static_cast<int>(text.size()) && charsRead < 6) {
                        int curr = HexCharToInt(text[pos]);
                        if (curr == -1)
                            break;
                        result = (result << 4) + curr;
                        pos++;
                        charsRead++;
                    }
                    if (charsRead != 0) {
                        try {
                            AppendUtf32(sb, result);
                        } catch (const std::out_of_range&) {
                            context.CompileContextRef().PushError("\\u character code not valid in string", context,
                                                                  escapeStartPos);
                        }
                    }
                    break;
                }
                case 'x': {
                    int result = 0;
                    int charsRead = 0;
                    while (pos < static_cast<int>(text.size()) && charsRead < 2) {
                        int curr = HexCharToInt(text[pos]);
                        if (curr == -1)
                            break;
                        result = (result << 4) + curr;
                        pos++;
                        charsRead++;
                    }
                    if (charsRead == 2) {
                        // C# appends (char)result, a UTF-16 code unit that gets
                        // UTF-8-encoded on output; a raw byte push would produce
                        // invalid UTF-8 for \x80-\xFF.
                        AppendUtf32(sb, result);
                    } else {
                        context.CompileContextRef().PushError("\\x character code needs exactly 2 hex digits", context,
                                                              escapeStartPos);
                    }
                    break;
                }
                default:
                    if (escapedChar >= '0' && escapedChar <= '7') {
                        // Step back so the leading digit is consumed as part of the octal run.
                        int result = 0;
                        int charsRead = 0;
                        pos--;
                        while (pos < static_cast<int>(text.size()) && charsRead < 3) {
                            char octalChar = text[pos];
                            if (octalChar < '0' || octalChar > '7')
                                break;
                            result = (result * 8) + (octalChar - '0');
                            pos++;
                            charsRead++;
                        }
                        if (charsRead == 3) {
                            // Up to \777 = 511; C# appends the UTF-16 code unit.
                            AppendUtf32(sb, result);
                        } else {
                            context.CompileContextRef().PushError(
                                std::string("\\") + escapedChar +
                                    "?? octal value in string is missing valid octal characters",
                                context, escapeStartPos);
                        }
                    } else {
                        sb.push_back(escapedChar);
                    }
                    break;
            }
            continue;
        }

        sb.push_back(c);
        pos++;
    }

    if (pos >= static_cast<int>(text.size())) {
        context.CompileContextRef().PushError("String not closed", context, startPosition);
        return pos;
    }

    context.Tokens().push_back(context.Arena().New<TokenString>(
        context, startPosition, std::string(text.substr(startPosition, (pos + 1) - startPosition)), std::move(sb)));
    return pos + 1;
}

} // namespace Underanalyzer::Compiler::Lexer

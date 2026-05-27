
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <string_view>

namespace Underanalyzer::Compiler::Lexer {

class ContiguousTextReader {
  public:
    static int ReadUntilWhitespace(std::string_view text, int startPosition, std::string_view& output);
    static int ReadUntilChar(std::string_view text, int startPosition, char c, std::string_view& output);
    static int ReadWhileIdentifier(std::string_view text, int startPosition, std::string_view& output);
    static int ReadWhileInternalIdentifier(std::string_view text, int startPosition, std::string_view& output,
                                           bool& anyNormalChars);
    static int ReadWhileNumber(std::string_view text, int startPosition, std::string_view& output);
    static int ReadWhileHex(std::string_view text, int startPosition, std::string_view& output);
};

} // namespace Underanalyzer::Compiler::Lexer

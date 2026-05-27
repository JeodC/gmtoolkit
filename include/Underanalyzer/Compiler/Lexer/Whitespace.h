
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <string_view>

namespace Underanalyzer::Compiler::Lexer {

class Whitespace {
  public:
    static int Skip(std::string_view text, int startPosition);
};

} // namespace Underanalyzer::Compiler::Lexer

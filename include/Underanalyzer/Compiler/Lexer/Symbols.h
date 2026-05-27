
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

namespace Underanalyzer::Compiler::Lexer {

class LexContext;

class Symbols {
  public:
    static int Parse(LexContext& context, int startPosition, char currChar, char nextChar, bool& success);
};

} // namespace Underanalyzer::Compiler::Lexer


// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

namespace Underanalyzer::Compiler::Lexer {

class LexContext;

class Strings {
  public:
    static int ParseVerbatim(LexContext& context, int startPosition, char startChar);
    static int ParseRegular(LexContext& context, int startPosition);
};

} // namespace Underanalyzer::Compiler::Lexer

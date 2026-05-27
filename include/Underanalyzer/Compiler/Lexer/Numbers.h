
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

namespace Underanalyzer::Compiler::Lexer {

class LexContext;

class Numbers {
  public:
    static int ParseDecimal(LexContext& context, int startPosition);
    static int ParseHex(LexContext& context, int startPosition, bool dollarSignSyntax);
};

} // namespace Underanalyzer::Compiler::Lexer


// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

namespace Underanalyzer::Compiler::Lexer {

class LexContext;

class Identifiers {
  public:
    static int Parse(LexContext& context, int startPosition);
    static int ParseInternal(LexContext& context, int startPosition);
};

} // namespace Underanalyzer::Compiler::Lexer

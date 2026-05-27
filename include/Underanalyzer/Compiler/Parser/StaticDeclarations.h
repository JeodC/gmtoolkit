
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

namespace Underanalyzer::Compiler::Parser {

class ParseContext;

class StaticDeclarations {
  public:
    static void Parse(ParseContext& context);
};

} // namespace Underanalyzer::Compiler::Parser

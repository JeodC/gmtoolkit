
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

namespace Underanalyzer::Compiler::Nodes {
class IASTNode;
}

namespace Underanalyzer::Compiler::Parser {

class ParseContext;

class Statements {
  public:
    static Nodes::IASTNode* ParseStatement(ParseContext& context);
};

} // namespace Underanalyzer::Compiler::Parser

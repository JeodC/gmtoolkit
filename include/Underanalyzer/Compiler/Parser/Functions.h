
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <vector>

namespace Underanalyzer::Compiler::Nodes {
class IASTNode;
}

namespace Underanalyzer::Compiler::Parser {

class ParseContext;

class Functions {
  public:
    static std::vector<Nodes::IASTNode*> ParseCallArguments(ParseContext& context, int maxArgumentCount);
};

} // namespace Underanalyzer::Compiler::Parser

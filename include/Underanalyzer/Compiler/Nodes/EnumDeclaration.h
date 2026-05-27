
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Underanalyzer::Compiler::Parser {
class ParseContext;
}

namespace Underanalyzer::Compiler::Nodes {

class IASTNode;

class EnumDeclaration {
  public:
    std::string Name;
    std::vector<std::string> ValueNames;
    std::unordered_map<std::string, IASTNode*> Values;
    std::unordered_map<std::string, int64_t> IntegerValues;

    EnumDeclaration(std::string name, std::vector<std::string> valueNames,
                    std::unordered_map<std::string, IASTNode*> values);

    static void Parse(Parser::ParseContext& context);
    static void ResolveValues(Parser::ParseContext& context);
};

} // namespace Underanalyzer::Compiler::Nodes

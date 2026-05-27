
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/EmptyNode.h"

#include "Underanalyzer/Compiler/Parser/ParseContext.h"

namespace Underanalyzer::Compiler::Nodes {

EmptyNode* EmptyNode::Create() {
    static EmptyNode Shared{ nullptr };
    return &Shared;
}

EmptyNode* EmptyNode::Create(Parser::ParseContext& Context, Lexer::IToken* NearbyToken) {
    return Context.Make<EmptyNode>(NearbyToken);
}

} // namespace Underanalyzer::Compiler::Nodes

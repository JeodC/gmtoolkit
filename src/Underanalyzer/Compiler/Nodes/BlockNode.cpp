
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/BlockNode.h"

#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/FunctionScope.h"
#include "Underanalyzer/Compiler/Lexer/Token.h"
#include "Underanalyzer/Compiler/Nodes/AssignNode.h"
#include "Underanalyzer/Compiler/Nodes/SimpleVariableNode.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"
#include "Underanalyzer/Compiler/Parser/Statements.h"

namespace Underanalyzer::Compiler::Nodes {

using namespace Lexer;
using Parser::ParseContext;
using Parser::Statements;

BlockNode* BlockNode::ParseRoot(ParseContext& Context) {
    std::vector<IASTNode*> Children;
    Children.reserve(32);
    Context.SkipSemicolons();
    while (!Context.EndOfCode()) {
        if (IASTNode* Node = Statements::ParseStatement(Context)) {
            Children.push_back(Node);
        } else {
            break;
        }
        Context.SkipSemicolons();
    }
    IToken* NearbyTokenLocal = Children.empty() ? nullptr : Children[0]->NearbyToken();
    return Context.Make<BlockNode>(std::move(Children), NearbyTokenLocal);
}

BlockNode* BlockNode::ParseRegular(ParseContext& Context) {
    IToken* TokenOpen = Context.EnsureToken(SeparatorKind::BlockOpen, KeywordKind::Begin);

    std::vector<IASTNode*> Children;
    Children.reserve(32);
    Context.SkipSemicolons();
    while (!Context.EndOfCode() && !Context.IsCurrentToken(SeparatorKind::BlockClose, KeywordKind::End)) {
        if (IASTNode* Node = Statements::ParseStatement(Context)) {
            Children.push_back(Node);
        } else {
            break;
        }
        Context.SkipSemicolons();
    }

    Context.EnsureToken(SeparatorKind::BlockClose, KeywordKind::End);
    return Context.Make<BlockNode>(std::move(Children), TokenOpen);
}

BlockNode* BlockNode::CreateEmpty(ParseContext& Context, IToken* NearbyTokenIn, int Capacity) {
    std::vector<IASTNode*> Children;
    Children.reserve(Capacity);
    return Context.Make<BlockNode>(std::move(Children), NearbyTokenIn);
}

IASTNode* BlockNode::PostProcess(ParseContext& Context) {
    for (auto& Child : _Children)
        Child = Child->PostProcess(Context);
    return this;
}

void BlockNode::PostProcessChildrenOnly(ParseContext& Context) {
    for (auto& Child : _Children)
        Child = Child->PostProcess(Context);
}

IASTNode* BlockNode::Duplicate(ParseContext& Context) {
    std::vector<IASTNode*> NewChildren(_Children);
    for (auto& Child : NewChildren)
        Child = Child->Duplicate(Context);
    return Context.Make<BlockNode>(std::move(NewChildren), _NearbyToken);
}

void BlockNode::GenerateCode(Bytecode::BytecodeContext& Context) {
    for (IASTNode* Statement : _Children)
        Statement->GenerateCode(Context);
}

// Static initializers expose the destination name to nested function decls so a
// constructor declared at "static foo = function()..." sees itself as "foo".
void BlockNode::GenerateStaticCode(Bytecode::BytecodeContext& Context) {
    Context.CurrentScope().SetGeneratingStaticBlock(true);
    for (IASTNode* Statement : _Children) {
        if (auto* Assign = As<AssignNode>(Statement)) {
            if (auto* Dest = As<SimpleVariableNode>(Assign->Destination)) {
                Context.CurrentScope().SetStaticVariableName(Dest->VariableName());
                Assign->GenerateCode(Context);
                Context.CurrentScope().SetStaticVariableName(std::nullopt);
                continue;
            }
        }
        Statement->GenerateCode(Context);
    }
    Context.CurrentScope().SetGeneratingStaticBlock(false);
}

} // namespace Underanalyzer::Compiler::Nodes

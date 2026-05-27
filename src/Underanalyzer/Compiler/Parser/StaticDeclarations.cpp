
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Parser/StaticDeclarations.h"

#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/FunctionScope.h"
#include "Underanalyzer/Compiler/Lexer/Token.h"
#include "Underanalyzer/Compiler/Nodes/AssignNode.h"
#include "Underanalyzer/Compiler/Nodes/BlockNode.h"
#include "Underanalyzer/Compiler/Nodes/IASTNode.h"
#include "Underanalyzer/Compiler/Nodes/SimpleVariableNode.h"
#include "Underanalyzer/Compiler/Parser/Expressions.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"

namespace Underanalyzer::Compiler::Parser {

using namespace Lexer;
using namespace Nodes;

void StaticDeclarations::Parse(ParseContext& context) {
    TokenKeyword* tokenKeyword = context.EnsureToken(KeywordKind::Static);
    if (tokenKeyword == nullptr)
        return;

    if (!context.CurrentScope().IsFunction()) {
        context.CompileContextRef().PushError("Cannot declare static variables outside of a function", tokenKeyword);
    } else {
        // All static initializers in a scope get appended to one shared block so they
        // run together exactly once at first function call, in declaration order.
        if (context.CurrentScope().StaticInitializerBlock() == nullptr) {
            context.CurrentScope().SetStaticInitializerBlock(BlockNode::CreateEmpty(context, tokenKeyword, 8));
        }
    }

    while (!context.EndOfCode()) {
        auto* tokenVariable = As<TokenVariable>(context.Tokens()[context.Position()]);
        if (tokenVariable == nullptr)
            break;
        context.SetPosition(context.Position() + 1);

        if (tokenVariable->BuiltinVariable != nullptr) {
            context.CompileContextRef().PushError("Declaring local variable over builtin '" + tokenVariable->Text + "'",
                                                  tokenVariable);
        }

        context.CurrentScope().DeclareStatic(tokenVariable->Text);

        if (context.IsCurrentToken(OperatorKind::Assign) || context.IsCurrentToken(OperatorKind::Assign2)) {
            context.SetPosition(context.Position() + 1);
            IASTNode* value = Expressions::ParseExpression(context);
            if (value != nullptr) {
                if (BlockNode* initBlock = context.CurrentScope().StaticInitializerBlock()) {
                    auto* newVariable = context.Make<SimpleVariableNode>(tokenVariable->Text, nullptr);
                    newVariable->SetExplicitInstanceType(IGMInstruction::InstanceType::Static);
                    initBlock->Children().push_back(
                        context.Make<AssignNode>(AssignNode::AssignKind::Normal, newVariable, value));
                }
            } else {
                break;
            }
        } else {
            context.CompileContextRef().PushError("Static variable declaration must assign an initial value",
                                                  tokenVariable);
        }

        if (!context.IsCurrentToken(SeparatorKind::Comma))
            break;
        context.SetPosition(context.Position() + 1);
    }
}

} // namespace Underanalyzer::Compiler::Parser


// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Parser/Functions.h"

#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/Lexer/Token.h"
#include "Underanalyzer/Compiler/Nodes/IASTNode.h"
#include "Underanalyzer/Compiler/Nodes/SimpleVariableNode.h"
#include "Underanalyzer/Compiler/Parser/Expressions.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"

namespace Underanalyzer::Compiler::Parser {

using namespace Lexer;
using namespace Nodes;

std::vector<IASTNode*> Functions::ParseCallArguments(ParseContext& context, int maxArgumentCount) {
    std::vector<IASTNode*> arguments;
    arguments.reserve(16);

    IToken* open = context.EnsureToken(SeparatorKind::GroupOpen);
    while (!context.EndOfCode() && !context.IsCurrentToken(SeparatorKind::GroupClose)) {
        // A bare comma marks a skipped/elided argument; fill it with undefined.
        if (context.IsCurrentToken(SeparatorKind::Comma)) {
            arguments.push_back(SimpleVariableNode::CreateUndefined(context));
            context.SetPosition(context.Position() + 1);
            continue;
        }

        IASTNode* argument = Expressions::ParseExpression(context);
        if (argument == nullptr)
            break;
        arguments.push_back(argument);

        if (context.EndOfCode())
            break;

        if (context.IsCurrentToken(SeparatorKind::Comma)) {
            context.SetPosition(context.Position() + 1);
            continue;
        }

        if (!context.IsCurrentToken(SeparatorKind::GroupClose)) {
            IToken* currentToken = context.Tokens()[context.Position()];
            context.CompileContextRef().PushError(
                "Expected '" + TokenSeparator::KindToString(SeparatorKind::Comma) + "' or '" +
                    TokenSeparator::KindToString(SeparatorKind::GroupClose) + "', got token",
                currentToken);
            break;
        }
    }
    context.EnsureToken(SeparatorKind::GroupClose);

    if (static_cast<int>(arguments.size()) > maxArgumentCount) {
        context.CompileContextRef().PushError("Calling function with too many arguments", open);
    }
    return arguments;
}

} // namespace Underanalyzer::Compiler::Parser

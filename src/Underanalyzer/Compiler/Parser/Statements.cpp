
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Parser/Statements.h"

#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/IBuiltins.h"
#include "Underanalyzer/Compiler/Lexer/Token.h"
#include "Underanalyzer/Compiler/Nodes/AssignNode.h"
#include "Underanalyzer/Compiler/Nodes/BlockNode.h"
#include "Underanalyzer/Compiler/Nodes/BreakNode.h"
#include "Underanalyzer/Compiler/Nodes/ContinueNode.h"
#include "Underanalyzer/Compiler/Nodes/DoUntilLoopNode.h"
#include "Underanalyzer/Compiler/Nodes/EmptyNode.h"
#include "Underanalyzer/Compiler/Nodes/EnumDeclaration.h"
#include "Underanalyzer/Compiler/Nodes/ExitNode.h"
#include "Underanalyzer/Compiler/Nodes/ForLoopNode.h"
#include "Underanalyzer/Compiler/Nodes/IASTNode.h"
#include "Underanalyzer/Compiler/Nodes/IfNode.h"
#include "Underanalyzer/Compiler/Nodes/LocalVarDeclNode.h"
#include "Underanalyzer/Compiler/Nodes/RepeatLoopNode.h"
#include "Underanalyzer/Compiler/Nodes/ReturnNode.h"
#include "Underanalyzer/Compiler/Nodes/SimpleVariableNode.h"
#include "Underanalyzer/Compiler/Nodes/SwitchNode.h"
#include "Underanalyzer/Compiler/Nodes/ThrowNode.h"
#include "Underanalyzer/Compiler/Nodes/TryCatchNode.h"
#include "Underanalyzer/Compiler/Nodes/WhileLoopNode.h"
#include "Underanalyzer/Compiler/Nodes/WithLoopNode.h"
#include "Underanalyzer/Compiler/Parser/Expressions.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"
#include "Underanalyzer/Compiler/Parser/StaticDeclarations.h"
#include "Underanalyzer/IGameContext.h"

#include <stdexcept>

namespace Underanalyzer::Compiler::Parser {

using namespace Lexer;
using namespace Nodes;

static IASTNode* ParseAssignmentOrExpressionStatement(ParseContext& context) {
    IASTNode* expr = Expressions::ParseChainExpression(context);
    if (expr == nullptr)
        return nullptr;

    auto* assignable = dynamic_cast<IAssignableASTNode*>(expr);
    if (assignable != nullptr && !context.EndOfCode()) {
        auto* tokenOperator = As<TokenOperator>(context.Tokens()[context.Position()]);
        if (tokenOperator != nullptr) {
            switch (tokenOperator->Kind) {
                case OperatorKind::Assign:
                case OperatorKind::Assign2:
                case OperatorKind::CompoundPlus:
                case OperatorKind::CompoundMinus:
                case OperatorKind::CompoundTimes:
                case OperatorKind::CompoundDivide:
                case OperatorKind::CompoundMod:
                case OperatorKind::CompoundBitwiseAnd:
                case OperatorKind::CompoundBitwiseOr:
                case OperatorKind::CompoundBitwiseXor:
                case OperatorKind::CompoundNullishCoalesce:
                    break;
                default:
                    tokenOperator = nullptr;
                    break;
            }
        }
        if (tokenOperator != nullptr) {
            context.SetPosition(context.Position() + 1);

            // Pre-GMLv2 enforces read-only builtins at parse time; later versions
            // moved the check into the runtime so the parser stays out of the way.
            if (!context.CompileContextRef().GameContext().UsingGMLv2()) {
                if (auto* variable = dynamic_cast<IVariableASTNode*>(assignable)) {
                    auto* bv = variable->BuiltinVariable();
                    if (bv != nullptr && !bv->CanSet()) {
                        context.CompileContextRef().PushError("Attempting to assign read-only variable '" +
                                                                  variable->VariableName() + "'",
                                                              variable->NearbyToken());
                    }
                }
            }

            if (tokenOperator->Kind == OperatorKind::CompoundNullishCoalesce &&
                !context.CompileContextRef().GameContext().UsingNullishOperator()) {
                context.CompileContextRef().PushError(
                    "Nullish coalesce operator (?\?=) is not supported in this GameMaker version (must be 2.3.7+)",
                    tokenOperator);
            }

            IASTNode* rhs = Expressions::ParseExpression(context);
            if (rhs == nullptr)
                return nullptr;

            AssignNode::AssignKind kind;
            switch (tokenOperator->Kind) {
                case OperatorKind::Assign:
                case OperatorKind::Assign2:
                    kind = AssignNode::AssignKind::Normal;
                    break;
                case OperatorKind::CompoundPlus:
                    kind = AssignNode::AssignKind::CompoundPlus;
                    break;
                case OperatorKind::CompoundMinus:
                    kind = AssignNode::AssignKind::CompoundMinus;
                    break;
                case OperatorKind::CompoundTimes:
                    kind = AssignNode::AssignKind::CompoundTimes;
                    break;
                case OperatorKind::CompoundDivide:
                    kind = AssignNode::AssignKind::CompoundDivide;
                    break;
                case OperatorKind::CompoundMod:
                    kind = AssignNode::AssignKind::CompoundMod;
                    break;
                case OperatorKind::CompoundBitwiseAnd:
                    kind = AssignNode::AssignKind::CompoundBitwiseAnd;
                    break;
                case OperatorKind::CompoundBitwiseOr:
                    kind = AssignNode::AssignKind::CompoundBitwiseOr;
                    break;
                case OperatorKind::CompoundBitwiseXor:
                    kind = AssignNode::AssignKind::CompoundBitwiseXor;
                    break;
                case OperatorKind::CompoundNullishCoalesce:
                    kind = AssignNode::AssignKind::CompoundNullishCoalesce;
                    break;
                default:
                    throw std::runtime_error("Unknown operator kind in assignment");
            }
            return context.Make<AssignNode>(kind, assignable, rhs);
        }
    }

    if (auto* statement = dynamic_cast<IMaybeStatementASTNode*>(expr)) {
        statement->SetIsStatement(true);
        return statement;
    }

    context.CompileContextRef().PushError("Expression floating outside of any statement", expr->NearbyToken());
    return nullptr;
}

IASTNode* Statements::ParseStatement(ParseContext& context) {
    if (context.EndOfCode()) {
        context.CompileContextRef().PushError("Unexpected end of code");
        return nullptr;
    }

    IToken* token = context.Tokens()[context.Position()];

    if (auto* sep = As<TokenSeparator>(token); sep != nullptr && sep->Kind == SeparatorKind::BlockOpen) {
        return BlockNode::ParseRegular(context);
    }
    if (auto* kw = As<TokenKeyword>(token)) {
        switch (kw->Kind) {
            case KeywordKind::Begin:
                return BlockNode::ParseRegular(context);
            case KeywordKind::If:
                return IfNode::Parse(context);
            case KeywordKind::Switch:
                return SwitchNode::Parse(context);
            case KeywordKind::Try:
                if (context.CompileContextRef().GameContext().UsingGMLv2()) {
                    return TryCatchNode::Parse(context);
                }
                context.SetPosition(context.Position() + 1);
                context.CompileContextRef().PushError("Cannot use try/catch/finally before GMLv2 (GameMaker 2.3+)",
                                                      token);
                return nullptr;
            case KeywordKind::While:
                return WhileLoopNode::Parse(context);
            case KeywordKind::For:
                return ForLoopNode::Parse(context);
            case KeywordKind::Repeat:
                return RepeatLoopNode::Parse(context);
            case KeywordKind::Do:
                return DoUntilLoopNode::Parse(context);
            case KeywordKind::With:
                return WithLoopNode::Parse(context);
            case KeywordKind::Var:
                return LocalVarDeclNode::Parse(context);
            case KeywordKind::Enum:
                EnumDeclaration::Parse(context);
                return EmptyNode::Create();
            case KeywordKind::Static:
                if (context.CompileContextRef().GameContext().UsingGMLv2()) {
                    StaticDeclarations::Parse(context);
                    return EmptyNode::Create();
                }
                context.SetPosition(context.Position() + 1);
                context.CompileContextRef().PushError("Cannot use static variables before GMLv2 (GameMaker 2.3+)",
                                                      token);
                return nullptr;
            case KeywordKind::Exit:
                context.SetPosition(context.Position() + 1);
                context.SetExitReturnBreakContinueCount(context.ExitReturnBreakContinueCount() + 1);
                return context.Make<ExitNode>(kw);
            case KeywordKind::Return: {
                // A bare 'return' (followed by ';' or a control keyword) is an Exit; otherwise
                // the next token must start an expression. 'function' and 'new' don't count
                // as blockers because they can legitimately start the returned expression.
                context.SetPosition(context.Position() + 1);
                context.SetExitReturnBreakContinueCount(context.ExitReturnBreakContinueCount() + 1);
                if (!context.EndOfCode()) {
                    IToken* nextToken = context.Tokens()[context.Position()];
                    bool semicolon = false;
                    bool blockingKeyword = false;
                    if (auto* nextSep = As<TokenSeparator>(nextToken);
                        nextSep != nullptr && nextSep->Kind == SeparatorKind::Semicolon)
                        semicolon = true;
                    if (auto* nextKw = As<TokenKeyword>(nextToken);
                        nextKw != nullptr && nextKw->Kind != KeywordKind::Function && nextKw->Kind != KeywordKind::New)
                        blockingKeyword = true;
                    if (!semicolon && !blockingKeyword) {
                        IASTNode* returnValue = Expressions::ParseExpression(context);
                        if (returnValue != nullptr) {
                            return context.Make<ReturnNode>(kw, returnValue);
                        }
                    }
                }
                return context.Make<ExitNode>(kw);
            }
            case KeywordKind::Break:
                context.SetPosition(context.Position() + 1);
                context.SetExitReturnBreakContinueCount(context.ExitReturnBreakContinueCount() + 1);
                return context.Make<BreakNode>(kw);
            case KeywordKind::Continue:
                context.SetPosition(context.Position() + 1);
                context.SetExitReturnBreakContinueCount(context.ExitReturnBreakContinueCount() + 1);
                return context.Make<ContinueNode>(kw);
            case KeywordKind::Throw:
                if (context.CompileContextRef().GameContext().UsingGMLv2()) {
                    context.SetThrowCount(context.ThrowCount() + 1);
                    return ThrowNode::Parse(context);
                }
                context.SetPosition(context.Position() + 1);
                context.CompileContextRef().PushError("Cannot throw before GMLv2 (GameMaker 2.3+)", token);
                return nullptr;
            case KeywordKind::Delete:
                // 'delete x' compiles down to 'x = undefined'; there is no dedicated opcode.
                context.SetPosition(context.Position() + 1);
                if (context.CompileContextRef().GameContext().UsingGMLv2()) {
                    if (auto* deleteTarget =
                            dynamic_cast<IAssignableASTNode*>(Expressions::ParseChainExpression(context))) {
                        return context.Make<AssignNode>(AssignNode::AssignKind::Normal, deleteTarget,
                                                        SimpleVariableNode::CreateUndefined(context));
                    }
                    context.CompileContextRef().PushError("Failed to find valid expression to delete", token);
                    return nullptr;
                }
                context.CompileContextRef().PushError("Cannot delete before GMLv2 (GameMaker 2.3+)", token);
                return nullptr;
            default:
                break;
        }
    }

    if (IASTNode* stmt = ParseAssignmentOrExpressionStatement(context))
        return stmt;

    context.CompileContextRef().PushError("Failed to find a valid statement", token);
    return nullptr;
}

} // namespace Underanalyzer::Compiler::Parser

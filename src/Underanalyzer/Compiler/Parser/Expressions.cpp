
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Parser/Expressions.h"

#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/IBuiltins.h"
#include "Underanalyzer/Compiler/Lexer/Token.h"
#include "Underanalyzer/Compiler/Nodes/AccessorNode.h"
#include "Underanalyzer/Compiler/Nodes/AssetReferenceNode.h"
#include "Underanalyzer/Compiler/Nodes/BinaryChainNode.h"
#include "Underanalyzer/Compiler/Nodes/BooleanNode.h"
#include "Underanalyzer/Compiler/Nodes/ConditionalNode.h"
#include "Underanalyzer/Compiler/Nodes/DotVariableNode.h"
#include "Underanalyzer/Compiler/Nodes/FunctionCallNode.h"
#include "Underanalyzer/Compiler/Nodes/FunctionDeclNode.h"
#include "Underanalyzer/Compiler/Nodes/IASTNode.h"
#include "Underanalyzer/Compiler/Nodes/Int64Node.h"
#include "Underanalyzer/Compiler/Nodes/NewObjectNode.h"
#include "Underanalyzer/Compiler/Nodes/NullishCoalesceNode.h"
#include "Underanalyzer/Compiler/Nodes/NumberNode.h"
#include "Underanalyzer/Compiler/Nodes/PostfixNode.h"
#include "Underanalyzer/Compiler/Nodes/PrefixNode.h"
#include "Underanalyzer/Compiler/Nodes/SimpleFunctionCallNode.h"
#include "Underanalyzer/Compiler/Nodes/SimpleVariableNode.h"
#include "Underanalyzer/Compiler/Nodes/StringNode.h"
#include "Underanalyzer/Compiler/Nodes/UnaryNode.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"
#include "Underanalyzer/IGameContext.h"

#include <stdexcept>
#include <vector>

namespace Underanalyzer::Compiler::Parser {

using namespace Lexer;
using namespace Nodes;

IASTNode* Expressions::ParseExpression(ParseContext& context) {
    return ParseConditional(context);
}

IASTNode* Expressions::ParseConditional(ParseContext& context) {
    IASTNode* lhs = ParseNullishExpression(context);
    if (lhs == nullptr)
        return nullptr;

    if (!context.EndOfCode()) {
        auto* tokenConditional = As<TokenOperator>(context.Tokens()[context.Position()]);
        if (tokenConditional != nullptr && tokenConditional->Kind == OperatorKind::Conditional) {
            context.SetPosition(context.Position() + 1);
            if (IASTNode* trueExpr = ParseOrExpression(context)) {
                context.EnsureToken(SeparatorKind::Colon);
                if (IASTNode* falseExpr = ParseOrExpression(context)) {
                    lhs = context.Make<ConditionalNode>(tokenConditional, lhs, trueExpr, falseExpr);
                }
            }
        }
    }
    return lhs;
}

IASTNode* Expressions::ParseNullishExpression(ParseContext& context) {
    IASTNode* lhs = ParseOrExpression(context);
    if (lhs == nullptr)
        return nullptr;

    if (!context.EndOfCode()) {
        auto* tokenNullishCoalesce = As<TokenOperator>(context.Tokens()[context.Position()]);
        if (tokenNullishCoalesce != nullptr && tokenNullishCoalesce->Kind == OperatorKind::NullishCoalesce) {
            context.SetPosition(context.Position() + 1);

            if (!context.CompileContextRef().GameContext().UsingNullishOperator()) {
                context.CompileContextRef().PushError(
                    "Nullish coalesce operator (?\?) is not supported in this GameMaker version (must be 2.3.7+)",
                    tokenNullishCoalesce);
            }

            if (IASTNode* rightExpr = ParseOrExpression(context)) {
                lhs = context.Make<NullishCoalesceNode>(tokenNullishCoalesce, lhs, rightExpr);
            }
        }
    }
    return lhs;
}

static bool MatchOpOrKw(IToken* token, std::initializer_list<OperatorKind> ops,
                        std::initializer_list<KeywordKind> kws) {
    if (auto* op = As<TokenOperator>(token)) {
        for (auto k : ops)
            if (op->Kind == k)
                return true;
    }
    if (auto* kw = As<TokenKeyword>(token)) {
        for (auto k : kws)
            if (kw->Kind == k)
                return true;
    }
    return false;
}

// Collapse a left-associative run of operators of equal precedence into a single
// BinaryChainNode (e.g. a + b + c becomes one node, not nested adds).
template <class NextFn, class PredFn>
static IASTNode* ParseBinaryChain(ParseContext& context, NextFn next, PredFn matches) {
    IASTNode* lhs = next(context);
    if (lhs == nullptr)
        return nullptr;
    if (context.EndOfCode())
        return lhs;

    IToken* firstToken = context.Tokens()[context.Position()];
    if (!matches(firstToken))
        return lhs;
    context.SetPosition(context.Position() + 1);

    IASTNode* rightExpr = next(context);
    if (rightExpr == nullptr)
        return lhs;

    std::vector<IASTNode*> arguments = { lhs, rightExpr };
    std::vector<BinaryChainNode::BinaryOperation> operations = { BinaryChainNode::OperationKindFromToken(firstToken) };

    while (!context.EndOfCode()) {
        IToken* nextToken = context.Tokens()[context.Position()];
        if (!matches(nextToken))
            break;
        context.SetPosition(context.Position() + 1);
        IASTNode* nextRightExpr = next(context);
        if (nextRightExpr == nullptr)
            break;
        arguments.push_back(nextRightExpr);
        operations.push_back(BinaryChainNode::OperationKindFromToken(nextToken));
    }

    return context.Make<BinaryChainNode>(firstToken, std::move(arguments), std::move(operations));
}

IASTNode* Expressions::ParseOrExpression(ParseContext& context) {
    return ParseBinaryChain(context, ParseAndExpression,
                            [](IToken* t) { return MatchOpOrKw(t, { OperatorKind::LogicalOr }, { KeywordKind::Or }); });
}

IASTNode* Expressions::ParseAndExpression(ParseContext& context) {
    return ParseBinaryChain(context, ParseXorExpression, [](IToken* t) {
        return MatchOpOrKw(t, { OperatorKind::LogicalAnd }, { KeywordKind::And });
    });
}

IASTNode* Expressions::ParseXorExpression(ParseContext& context) {
    return ParseBinaryChain(context, ParseCompareExpression, [](IToken* t) {
        return MatchOpOrKw(t, { OperatorKind::LogicalXor }, { KeywordKind::Xor });
    });
}

// GML quirk: '=' and ':=' inside an expression are equality, not assignment.
IASTNode* Expressions::ParseCompareExpression(ParseContext& context) {
    return ParseBinaryChain(context, ParseBitwiseExpression, [](IToken* t) {
        return MatchOpOrKw(t,
                           { OperatorKind::CompareEqual, OperatorKind::Assign, OperatorKind::Assign2,
                             OperatorKind::CompareNotEqual, OperatorKind::CompareNotEqual2,
                             OperatorKind::CompareGreater, OperatorKind::CompareGreaterEqual,
                             OperatorKind::CompareLesser, OperatorKind::CompareLesserEqual },
                           {});
    });
}

IASTNode* Expressions::ParseBitwiseExpression(ParseContext& context) {
    return ParseBinaryChain(context, ParseBitwiseShiftExpression, [](IToken* t) {
        return MatchOpOrKw(t, { OperatorKind::BitwiseAnd, OperatorKind::BitwiseOr, OperatorKind::BitwiseXor }, {});
    });
}

IASTNode* Expressions::ParseBitwiseShiftExpression(ParseContext& context) {
    return ParseBinaryChain(context, ParseAddSubtractExpression, [](IToken* t) {
        return MatchOpOrKw(t, { OperatorKind::BitwiseShiftLeft, OperatorKind::BitwiseShiftRight }, {});
    });
}

IASTNode* Expressions::ParseAddSubtractExpression(ParseContext& context) {
    return ParseBinaryChain(context, ParseMultiplyDivideExpression, [](IToken* t) {
        return MatchOpOrKw(t, { OperatorKind::Plus, OperatorKind::Minus }, {});
    });
}

IASTNode* Expressions::ParseMultiplyDivideExpression(ParseContext& context) {
    return ParseBinaryChain(
        context, [](ParseContext& c) { return ParseChainExpression(c); },
        [](IToken* t) {
            return MatchOpOrKw(t, { OperatorKind::Times, OperatorKind::Divide, OperatorKind::Mod },
                               { KeywordKind::Mod, KeywordKind::Div });
        });
}

IASTNode* Expressions::ParseChainExpression(ParseContext& context, bool stopAtFunctionCall) {
    IASTNode* lhs = ParseLeftmostExpression(context);
    if (lhs == nullptr)
        return nullptr;

    while (!context.EndOfCode()) {
        IToken* currentToken = context.Tokens()[context.Position()];

        if (auto* tokenArrayOpen = As<TokenSeparator>(currentToken)) {
            AccessorNode::AccessorKind accessorKind;
            bool isAccessor = true;
            switch (tokenArrayOpen->Kind) {
                case SeparatorKind::ArrayOpen:
                    accessorKind = AccessorNode::AccessorKind::Array;
                    break;
                case SeparatorKind::ArrayOpenDirect:
                    accessorKind = AccessorNode::AccessorKind::ArrayDirect;
                    break;
                case SeparatorKind::ArrayOpenList:
                    accessorKind = AccessorNode::AccessorKind::List;
                    break;
                case SeparatorKind::ArrayOpenMap:
                    accessorKind = AccessorNode::AccessorKind::Map;
                    break;
                case SeparatorKind::ArrayOpenGrid:
                    accessorKind = AccessorNode::AccessorKind::Grid;
                    break;
                case SeparatorKind::ArrayOpenStruct:
                    accessorKind = AccessorNode::AccessorKind::Struct;
                    break;
                default:
                    isAccessor = false;
                    break;
            }
            if (isAccessor) {
                context.SetPosition(context.Position() + 1);
                AccessorNode* accessor = AccessorNode::Parse(context, tokenArrayOpen, lhs, accessorKind);
                if (accessor == nullptr)
                    break;

                // GMLv2 rewrites a[i, j] into chained 1D accessors a[i][j]; older bytecode has
                // a real 2D opcode and rejects nesting accessors at the parser level.
                if (context.CompileContextRef().GameContext().UsingGMLv2()) {
                    accessor = accessor->Convert2DArrayToTwoAccessors(context);
                } else if (dynamic_cast<AccessorNode*>(lhs) != nullptr) {
                    context.CompileContextRef().PushError(
                        "Multidimensional array syntax not supported before GMLv2 (GameMaker 2.3+)", tokenArrayOpen);
                }
                lhs = accessor;
                continue;
            }

            if (!stopAtFunctionCall && tokenArrayOpen->Kind == SeparatorKind::GroupOpen) {
                lhs = context.Make<FunctionCallNode>(context, tokenArrayOpen, lhs);
                continue;
            }

            if (tokenArrayOpen->Kind == SeparatorKind::Dot) {
                context.SetPosition(context.Position() + 1);
                if (context.EndOfCode()) {
                    context.CompileContextRef().PushError("Unexpected end of code", tokenArrayOpen);
                    break;
                }
                IToken* nextToken = context.Tokens()[context.Position()];
                if (auto* tokenVariable = As<TokenVariable>(nextToken)) {
                    context.SetPosition(context.Position() + 1);
                    lhs = context.Make<DotVariableNode>(lhs, tokenVariable);
                } else if (auto* tokenAssetReference = As<TokenAssetReference>(nextToken)) {
                    context.SetPosition(context.Position() + 1);
                    lhs = context.Make<DotVariableNode>(lhs, context.Make<TokenVariable>(*tokenAssetReference));
                } else if (auto* tokenNumber = As<TokenNumber>(nextToken);
                           tokenNumber != nullptr && tokenNumber->IsConstant) {
                    context.SetPosition(context.Position() + 1);
                    lhs = context.Make<DotVariableNode>(lhs, context.Make<TokenVariable>(*tokenNumber));
                } else if (auto* tokenFunction = As<TokenFunction>(nextToken)) {
                    context.SetPosition(context.Position() + 1);
                    lhs = context.Make<DotVariableNode>(lhs, tokenFunction);
                    if (!stopAtFunctionCall) {
                        if (!context.EndOfCode()) {
                            auto* tokenDotOpen = As<TokenSeparator>(context.Tokens()[context.Position()]);
                            if (tokenDotOpen != nullptr && tokenDotOpen->Kind == SeparatorKind::GroupOpen) {
                                lhs = context.Make<FunctionCallNode>(context, tokenDotOpen, lhs);
                                continue;
                            }
                        }
                        context.EnsureToken(SeparatorKind::GroupOpen);
                    }
                } else {
                    context.CompileContextRef().PushError("Expected variable or function call after dot",
                                                          tokenArrayOpen);
                    break;
                }
                continue;
            }
            break;
        }
        break;
    }

    // Some builtin variables (the view_xview family, etc.) implicitly index element 0
    // when written without a subscript; synthesize that accessor here.
    if (auto* simpleVariable = dynamic_cast<SimpleVariableNode*>(lhs)) {
        auto* bv = simpleVariable->BuiltinVariable();
        if (bv != nullptr && bv->IsAutomaticArray()) {
            lhs = context.Make<AccessorNode>(
                simpleVariable->NearbyToken(), simpleVariable, AccessorNode::AccessorKind::Array,
                context.Make<Int64Node>(static_cast<int64_t>(0), simpleVariable->NearbyToken()));
        }
    }

    if (!context.EndOfCode()) {
        if (auto* lhsAssignable = dynamic_cast<IAssignableASTNode*>(lhs)) {
            auto* tokenPostfix = As<TokenOperator>(context.Tokens()[context.Position()]);
            if (tokenPostfix != nullptr &&
                (tokenPostfix->Kind == OperatorKind::Increment || tokenPostfix->Kind == OperatorKind::Decrement)) {
                context.SetPosition(context.Position() + 1);
                lhs = context.Make<PostfixNode>(tokenPostfix, lhsAssignable,
                                                tokenPostfix->Kind == OperatorKind::Increment);
            }
        }
    }
    return lhs;
}

IASTNode* Expressions::ParseLeftmostExpression(ParseContext& context) {
    if (context.EndOfCode()) {
        context.CompileContextRef().PushError("Unexpected end of code");
        return nullptr;
    }

    IToken* token = context.Tokens()[context.Position()];

    if (auto* tokenNumber = As<TokenNumber>(token)) {
        context.SetPosition(context.Position() + 1);
        return context.Make<NumberNode>(tokenNumber, tokenNumber->IsConstant ? &tokenNumber->Text : nullptr);
    }
    if (auto* tokenInt64 = As<TokenInt64>(token)) {
        context.SetPosition(context.Position() + 1);
        return context.Make<Int64Node>(tokenInt64);
    }
    if (auto* tokenString = As<TokenString>(token)) {
        context.SetPosition(context.Position() + 1);
        return context.Make<StringNode>(tokenString);
    }
    if (auto* tokenBoolean = As<TokenBoolean>(token)) {
        context.SetPosition(context.Position() + 1);
        return context.Make<BooleanNode>(tokenBoolean);
    }
    if (auto* tokenAssetReference = As<TokenAssetReference>(token)) {
        context.SetPosition(context.Position() + 1);
        return context.Make<AssetReferenceNode>(tokenAssetReference);
    }
    if (auto* tokenFunction = As<TokenFunction>(token)) {
        context.SetPosition(context.Position() + 1);
        return context.Make<SimpleFunctionCallNode>(context, tokenFunction);
    }
    if (auto* tokenVariable = As<TokenVariable>(token)) {
        context.SetPosition(context.Position() + 1);
        return context.Make<SimpleVariableNode>(tokenVariable);
    }
    if (auto* tokenSep = As<TokenSeparator>(token)) {
        if (tokenSep->Kind == SeparatorKind::GroupOpen) {
            context.SetPosition(context.Position() + 1);
            IASTNode* groupedExpression = ParseExpression(context);
            context.EnsureToken(SeparatorKind::GroupClose);
            return groupedExpression;
        }
        if (tokenSep->Kind == SeparatorKind::ArrayOpen) {
            context.SetPosition(context.Position() + 1);
            if (context.CompileContextRef().GameContext().UsingGMS2OrLater()) {
                return SimpleFunctionCallNode::ParseArrayLiteral(context);
            }
            context.CompileContextRef().PushError("Cannot use array literals before GMS2", token);
            return nullptr;
        }
        if (tokenSep->Kind == SeparatorKind::BlockOpen) {
            context.SetPosition(context.Position() + 1);
            if (context.CompileContextRef().GameContext().UsingGMLv2()) {
                return FunctionDeclNode::ParseStruct(context, tokenSep);
            }
            context.CompileContextRef().PushError("Cannot use structs before GMLv2 (GameMaker 2.3+)", token);
            return nullptr;
        }
    }
    if (auto* tokenOp = As<TokenOperator>(token)) {
        if (tokenOp->Kind == OperatorKind::Increment || tokenOp->Kind == OperatorKind::Decrement) {
            context.SetPosition(context.Position() + 1);
            return PrefixNode::Parse(context, tokenOp, tokenOp->Kind == OperatorKind::Increment);
        }
        if (tokenOp->Kind == OperatorKind::Not || tokenOp->Kind == OperatorKind::BitwiseNegate ||
            tokenOp->Kind == OperatorKind::Plus || tokenOp->Kind == OperatorKind::Minus) {
            context.SetPosition(context.Position() + 1);
            UnaryNode::UnaryKind kind;
            switch (tokenOp->Kind) {
                case OperatorKind::Not:
                    kind = UnaryNode::UnaryKind::BooleanNot;
                    break;
                case OperatorKind::BitwiseNegate:
                    kind = UnaryNode::UnaryKind::BitwiseNegate;
                    break;
                case OperatorKind::Plus:
                    kind = UnaryNode::UnaryKind::Positive;
                    break;
                case OperatorKind::Minus:
                    kind = UnaryNode::UnaryKind::Negative;
                    break;
                default:
                    throw std::runtime_error("Unknown operator kind for unary operation");
            }
            return UnaryNode::Parse(context, tokenOp, kind);
        }
    }
    if (auto* tokenKw = As<TokenKeyword>(token)) {
        if (tokenKw->Kind == KeywordKind::Not) {
            context.SetPosition(context.Position() + 1);
            return UnaryNode::Parse(context, tokenKw, UnaryNode::UnaryKind::BooleanNot);
        }
        if (tokenKw->Kind == KeywordKind::Function) {
            context.SetPosition(context.Position() + 1);
            if (context.CompileContextRef().GameContext().UsingGMLv2()) {
                return FunctionDeclNode::Parse(context, tokenKw);
            }
            context.CompileContextRef().PushError("Cannot declare functions before GMLv2 (GameMaker 2.3+)", token);
            return nullptr;
        }
        if (tokenKw->Kind == KeywordKind::Begin) {
            context.SetPosition(context.Position() + 1);
            if (context.CompileContextRef().GameContext().UsingGMLv2()) {
                return FunctionDeclNode::ParseStruct(context, tokenKw);
            }
            context.CompileContextRef().PushError("Cannot use structs before GMLv2 (GameMaker 2.3+)", token);
            return nullptr;
        }
        if (tokenKw->Kind == KeywordKind::New) {
            if (context.CompileContextRef().GameContext().UsingGMLv2()) {
                return NewObjectNode::Parse(context);
            }
            context.CompileContextRef().PushError("Cannot use new before GMLv2 (GameMaker 2.3+)", token);
            return nullptr;
        }
    }

    context.SetPosition(context.Position() + 1);
    context.CompileContextRef().PushError("Failed to find a valid expression", token);
    return nullptr;
}

} // namespace Underanalyzer::Compiler::Parser


// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/UnaryNode.h"

#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/Nodes/BooleanNode.h"
#include "Underanalyzer/Compiler/Nodes/Int64Node.h"
#include "Underanalyzer/Compiler/Nodes/NumberNode.h"
#include "Underanalyzer/Compiler/Parser/Expressions.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"

#include <cstdint>

namespace Underanalyzer::Compiler::Nodes {

UnaryNode* UnaryNode::Parse(Parser::ParseContext& Context, Lexer::IToken* Token, UnaryKind KindIn) {
    IASTNode* Expr = Parser::Expressions::ParseChainExpression(Context);
    if (Expr == nullptr)
        return nullptr;
    return Context.Make<UnaryNode>(Token, KindIn, Expr);
}

IASTNode* UnaryNode::PostProcess(Parser::ParseContext& Context) {
    Expression = Expression->PostProcess(Context);

    switch (KindValue) {
        case UnaryKind::BooleanNot:
            if (auto* N = As<NumberNode>(Expression))
                return Context.Make<NumberNode>((N->Value > 0.5) ? 1.0 : 0.0, N->NearbyToken());
            if (auto* N = As<Int64Node>(Expression))
                return Context.Make<Int64Node>(static_cast<int64_t>((N->Value > 0.5) ? 1 : 0), N->NearbyToken());
            if (auto* B = As<BooleanNode>(Expression))
                return Context.Make<BooleanNode>(!B->Value, B->NearbyToken());
            break;
        case UnaryKind::BitwiseNegate:
            if (auto* N = As<NumberNode>(Expression))
                return Context.Make<NumberNode>(static_cast<double>(~static_cast<int64_t>(N->Value)), N->NearbyToken());
            if (auto* N = As<Int64Node>(Expression))
                return Context.Make<Int64Node>(~N->Value, N->NearbyToken());
            if (auto* B = As<BooleanNode>(Expression))
                return Context.Make<NumberNode>(static_cast<double>(~(B->Value ? 1 : 0)), B->NearbyToken());
            break;
        case UnaryKind::Positive:
            // Unary + is a no-op on constants; non-constants still emit nothing in GenerateCode.
            if (dynamic_cast<IConstantASTNode*>(Expression) != nullptr)
                return Expression;
            break;
        case UnaryKind::Negative:
            if (auto* N = As<NumberNode>(Expression))
                return Context.Make<NumberNode>(-N->Value, N->NearbyToken());
            if (auto* N = As<Int64Node>(Expression))
                return Context.Make<Int64Node>(-N->Value, N->NearbyToken());
            if (auto* B = As<BooleanNode>(Expression))
                return Context.Make<NumberNode>(static_cast<double>(-(B->Value ? 1 : 0)), B->NearbyToken());
            break;
    }
    return this;
}

IASTNode* UnaryNode::Duplicate(Parser::ParseContext& Context) {
    return Context.Make<UnaryNode>(_NearbyToken, KindValue, Expression->Duplicate(Context));
}

void UnaryNode::GenerateCode(Bytecode::BytecodeContext& Context) {
    using Op = IGMInstruction::Opcode;
    using DT = IGMInstruction::DataType;

    Expression->GenerateCode(Context);
    DT Type = Context.PeekDataType();

    switch (KindValue) {
        case UnaryKind::BooleanNot:
            if (Type == DT::String) {
                Context.CompileContextRef().PushError("Cannot invert a string", _NearbyToken);
            } else if (Type != DT::Boolean) {
                Context.PopDataType();
                Context.Emit(Op::Convert, Type, DT::Boolean);
                Context.PushDataType(DT::Boolean);
            }
            Context.Emit(Op::Not, DT::Boolean);
            break;
        case UnaryKind::Negative:
            if (Type == DT::String) {
                Context.CompileContextRef().PushError("Cannot negate a string", _NearbyToken);
            } else if (Type == DT::Boolean) {
                Context.PopDataType();
                Context.Emit(Op::Convert, DT::Boolean, DT::Int32);
                Context.PushDataType(DT::Int32);
                Type = DT::Int32;
            }
            Context.Emit(Op::Negate, Type);
            break;
        case UnaryKind::BitwiseNegate:
            if (Type == DT::String) {
                Context.CompileContextRef().PushError("Cannot bitwise negate a string", _NearbyToken);
            } else if (Type == DT::Double || Type == DT::Variable) {
                Context.PopDataType();

                Context.Emit(Op::Convert, Type, DT::Int64);
                Context.PushDataType(DT::Int64);
                Type = DT::Int64;
            }
            Context.Emit(Op::Not, Type);
            break;
        case UnaryKind::Positive:

            break;
    }
}

} // namespace Underanalyzer::Compiler::Nodes

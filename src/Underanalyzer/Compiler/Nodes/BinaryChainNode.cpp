
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/BinaryChainNode.h"

#include "Underanalyzer/Compiler/Bytecode/BranchPatch.h"
#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/Lexer/Token.h"
#include "Underanalyzer/Compiler/Nodes/BooleanNode.h"
#include "Underanalyzer/Compiler/Nodes/Int64Node.h"
#include "Underanalyzer/Compiler/Nodes/NumberNode.h"
#include "Underanalyzer/Compiler/Nodes/StringNode.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"
#include "Underanalyzer/IGameContext.h"

#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace Underanalyzer::Compiler::Nodes {

using namespace Lexer;
using Parser::ParseContext;
using Op = IGMInstruction::Opcode;
using DT = IGMInstruction::DataType;
using CT = IGMInstruction::ComparisonType;
using BinOp = BinaryChainNode::BinaryOperation;

BinaryChainNode::BinaryOperation BinaryChainNode::OperationKindFromToken(IToken* Token) {
    if (auto* O = As<TokenOperator>(Token)) {
        switch (O->Kind) {
            case OperatorKind::Plus:
                return BinOp::Add;
            case OperatorKind::Minus:
                return BinOp::Subtract;
            case OperatorKind::Times:
                return BinOp::Multiply;
            case OperatorKind::Divide:
                return BinOp::Divide;
            case OperatorKind::Mod:
                return BinOp::GMLModulo;
            case OperatorKind::LogicalAnd:
                return BinOp::LogicalAnd;
            case OperatorKind::LogicalOr:
                return BinOp::LogicalOr;
            case OperatorKind::LogicalXor:
                return BinOp::LogicalXor;
            case OperatorKind::BitwiseAnd:
                return BinOp::BitwiseAnd;
            case OperatorKind::BitwiseOr:
                return BinOp::BitwiseOr;
            case OperatorKind::BitwiseXor:
                return BinOp::BitwiseXor;
            case OperatorKind::BitwiseShiftLeft:
                return BinOp::BitwiseShiftLeft;
            case OperatorKind::BitwiseShiftRight:
                return BinOp::BitwiseShiftRight;
            case OperatorKind::CompareEqual:
            case OperatorKind::Assign:
            case OperatorKind::Assign2:
                return BinOp::CompareEqual;
            case OperatorKind::CompareNotEqual:
            case OperatorKind::CompareNotEqual2:
                return BinOp::CompareNotEqual;
            case OperatorKind::CompareGreater:
                return BinOp::CompareGreater;
            case OperatorKind::CompareGreaterEqual:
                return BinOp::CompareGreaterEqual;
            case OperatorKind::CompareLesser:
                return BinOp::CompareLesser;
            case OperatorKind::CompareLesserEqual:
                return BinOp::CompareLesserEqual;
            default:
                break;
        }
    } else if (auto* K = As<TokenKeyword>(Token)) {
        switch (K->Kind) {
            case KeywordKind::Div:
                return BinOp::GMLDivRemainder;
            case KeywordKind::Mod:
                return BinOp::GMLModulo;
            case KeywordKind::And:
                return BinOp::LogicalAnd;
            case KeywordKind::Or:
                return BinOp::LogicalOr;
            case KeywordKind::Xor:
                return BinOp::LogicalXor;
            default:
                break;
        }
    }
    throw std::runtime_error("Unknown operator");
}

static int64_t CheckDivisionByZero(ParseContext& Context, IASTNode* Node, int64_t Number) {
    if (Number == 0) {
        Context.CompileContextRef().PushError("Division by zero", Node->NearbyToken());
        return 1;
    }
    return Number;
}

// If bit 31 is set, the result must stay 64-bit; folding it into a double would flip
// the sign or trash high bits when the value is reinterpreted as int32 at runtime.
static IConstantASTNode* GetBitwiseNumberResult(ParseContext& Context, int64_t Number, Lexer::IToken* NearbyTokenIn) {

    if ((Number & 0x80000000) != 0) {
        return Context.Make<Int64Node>(Number, NearbyTokenIn);
    }
    return Context.Make<NumberNode>(static_cast<double>(Number), NearbyTokenIn);
}

static BooleanNode* CompareConstants(ParseContext& Context, BinOp Operation, IConstantASTNode* Left,
                                     IConstantASTNode* Right) {
    bool HaveDiff = false;
    double Difference = 0.0;
    auto* LN = As<NumberNode>(Left);
    auto* RN = As<NumberNode>(Right);
    auto* LI = As<Int64Node>(Left);
    auto* RI = As<Int64Node>(Right);
    auto* LS = As<StringNode>(Left);
    auto* RS = As<StringNode>(Right);
    if (LN && RN) {
        HaveDiff = true;
        Difference = LN->Value - RN->Value;
    } else if (LN && RI) {
        HaveDiff = true;
        Difference = LN->Value - static_cast<double>(RI->Value);
    } else if (LI && RN) {
        HaveDiff = true;
        Difference = static_cast<double>(LI->Value) - RN->Value;
    } else if (LI && RI) {
        HaveDiff = true;
        Difference = static_cast<double>(LI->Value - RI->Value);
    } else if (LS && RS) {
        HaveDiff = true;
        Difference = static_cast<double>(LS->Value.compare(RS->Value));
    }
    if (!HaveDiff)
        return nullptr;

    Lexer::IToken* Tok = Left->NearbyToken();
    switch (Operation) {
        case BinOp::CompareEqual:
            return Context.Make<BooleanNode>(Difference == 0, Tok);
        case BinOp::CompareNotEqual:
            return Context.Make<BooleanNode>(Difference != 0, Tok);
        case BinOp::CompareLesser:
            return Context.Make<BooleanNode>(Difference < 0, Tok);
        case BinOp::CompareLesserEqual:
            return Context.Make<BooleanNode>(Difference <= 0, Tok);
        case BinOp::CompareGreater:
            return Context.Make<BooleanNode>(Difference > 0, Tok);
        case BinOp::CompareGreaterEqual:
            return Context.Make<BooleanNode>(Difference >= 0, Tok);
        default:
            return nullptr;
    }
}

static IConstantASTNode* PerformConstantOperation(ParseContext& Context, BinOp Operation, IConstantASTNode* Left,
                                                  IConstantASTNode* Right) {

    if (auto* LB = As<BooleanNode>(Left))
        Left = Context.Make<NumberNode>(LB->Value ? 1.0 : 0.0, LB->NearbyToken());
    if (auto* RB = As<BooleanNode>(Right))
        Right = Context.Make<NumberNode>(RB->Value ? 1.0 : 0.0, RB->NearbyToken());

    auto* LN = As<NumberNode>(Left);
    auto* RN = As<NumberNode>(Right);
    auto* LI = As<Int64Node>(Left);
    auto* RI = As<Int64Node>(Right);
    auto* LS = As<StringNode>(Left);
    auto* RS = As<StringNode>(Right);
    Lexer::IToken* LeftTok = Left->NearbyToken();

    switch (Operation) {
        case BinOp::Add:
            if (LN && RN)
                return Context.Make<NumberNode>(LN->Value + RN->Value, LeftTok);
            if (LN && RI)
                return Context.Make<NumberNode>(LN->Value + static_cast<double>(RI->Value), LeftTok);
            if (LI && RN)
                return Context.Make<NumberNode>(static_cast<double>(LI->Value) + RN->Value, LeftTok);
            if (LI && RI)
                return Context.Make<Int64Node>(LI->Value + RI->Value, LeftTok);
            if (LS && RS)
                return Context.Make<StringNode>(LS->Value + RS->Value, LeftTok);
            return nullptr;
        case BinOp::Subtract:
            if (LN && RN)
                return Context.Make<NumberNode>(LN->Value - RN->Value, LeftTok);
            if (LN && RI)
                return Context.Make<NumberNode>(LN->Value - static_cast<double>(RI->Value), LeftTok);
            if (LI && RN)
                return Context.Make<NumberNode>(static_cast<double>(LI->Value) - RN->Value, LeftTok);
            if (LI && RI)
                return Context.Make<Int64Node>(LI->Value - RI->Value, LeftTok);
            return nullptr;
        case BinOp::Multiply:
            if (LN && RN)
                return Context.Make<NumberNode>(LN->Value * RN->Value, LeftTok);
            if (LN && RI)
                return Context.Make<NumberNode>(LN->Value * static_cast<double>(RI->Value), LeftTok);
            if (LN && RS) {

                std::string Out;
                int N = static_cast<int>(LN->Value);
                if (N > 0)
                    Out.reserve(RS->Value.size() * N);
                for (int i = 0; i < N; i++)
                    Out.append(RS->Value);
                return Context.Make<StringNode>(std::move(Out), LeftTok);
            }
            if (LI && RN)
                return Context.Make<NumberNode>(static_cast<double>(LI->Value) * RN->Value, LeftTok);
            if (LI && RI)
                return Context.Make<Int64Node>(LI->Value * RI->Value, LeftTok);
            return nullptr;
        case BinOp::Divide:
            if (LN && RN)
                return Context.Make<NumberNode>(LN->Value / RN->Value, LeftTok);
            if (LN && RI)
                return Context.Make<NumberNode>(LN->Value / static_cast<double>(RI->Value), LeftTok);
            if (LI && RN)
                return Context.Make<NumberNode>(static_cast<double>(LI->Value) / RN->Value, LeftTok);
            if (LI && RI)
                return Context.Make<Int64Node>(LI->Value / CheckDivisionByZero(Context, RI, RI->Value), LeftTok);
            return nullptr;
        case BinOp::GMLDivRemainder:
            if (LN && RN)
                return Context.Make<NumberNode>(
                    static_cast<double>(static_cast<int64_t>(LN->Value) /
                                        CheckDivisionByZero(Context, RN, static_cast<int64_t>(RN->Value))),
                    LeftTok);
            if (LN && RI)
                return Context.Make<Int64Node>(
                    static_cast<int64_t>(LN->Value) / CheckDivisionByZero(Context, RI, RI->Value), LeftTok);
            if (LI && RN)
                return Context.Make<Int64Node>(
                    LI->Value / CheckDivisionByZero(Context, RN, static_cast<int64_t>(RN->Value)), LeftTok);
            if (LI && RI)
                return Context.Make<Int64Node>(LI->Value / CheckDivisionByZero(Context, RI, RI->Value), LeftTok);
            return nullptr;
        case BinOp::GMLModulo:
            if (LN && RN)
                return Context.Make<NumberNode>(std::fmod(LN->Value, RN->Value), LeftTok);
            if (LN && RI)
                return Context.Make<Int64Node>(
                    static_cast<int64_t>(LN->Value) % CheckDivisionByZero(Context, RI, RI->Value), LeftTok);
            if (LI && RN)
                return Context.Make<Int64Node>(
                    LI->Value % CheckDivisionByZero(Context, RN, static_cast<int64_t>(RN->Value)), LeftTok);
            if (LI && RI)
                return Context.Make<Int64Node>(LI->Value % CheckDivisionByZero(Context, RI, RI->Value), LeftTok);
            return nullptr;
        case BinOp::CompareEqual:
        case BinOp::CompareNotEqual:
        case BinOp::CompareLesser:
        case BinOp::CompareLesserEqual:
        case BinOp::CompareGreater:
        case BinOp::CompareGreaterEqual:
            return CompareConstants(Context, Operation, Left, Right);

        // GML's truthiness threshold for logical ops is 0.5, not the C-style "non-zero".
        case BinOp::LogicalAnd:
            if (LN && RN)
                return Context.Make<NumberNode>(((LN->Value > 0.5) && (RN->Value > 0.5)) ? 1.0 : 0.0, LeftTok);
            if (LN && RI)
                return Context.Make<NumberNode>(((LN->Value > 0.5) && (RI->Value > 0.5)) ? 1.0 : 0.0, LeftTok);
            if (LI && RN)
                return Context.Make<NumberNode>(((LI->Value > 0.5) && (RN->Value > 0.5)) ? 1.0 : 0.0, LeftTok);
            if (LI && RI)
                return Context.Make<NumberNode>(((LI->Value > 0.5) && (RI->Value > 0.5)) ? 1.0 : 0.0, LeftTok);
            return nullptr;
        case BinOp::LogicalOr:
            if (LN && RN)
                return Context.Make<NumberNode>(((LN->Value > 0.5) || (RN->Value > 0.5)) ? 1.0 : 0.0, LeftTok);
            if (LN && RI)
                return Context.Make<NumberNode>(((LN->Value > 0.5) || (RI->Value > 0.5)) ? 1.0 : 0.0, LeftTok);
            if (LI && RN)
                return Context.Make<NumberNode>(((LI->Value > 0.5) || (RN->Value > 0.5)) ? 1.0 : 0.0, LeftTok);
            if (LI && RI)
                return Context.Make<NumberNode>(((LI->Value > 0.5) || (RI->Value > 0.5)) ? 1.0 : 0.0, LeftTok);
            return nullptr;
        case BinOp::LogicalXor:
            if (LN && RN)
                return Context.Make<NumberNode>(((LN->Value > 0.5) != (RN->Value > 0.5)) ? 1.0 : 0.0, LeftTok);
            if (LN && RI)
                return Context.Make<NumberNode>(((LN->Value > 0.5) != (RI->Value > 0.5)) ? 1.0 : 0.0, LeftTok);
            if (LI && RN)
                return Context.Make<NumberNode>(((LI->Value > 0.5) != (RN->Value > 0.5)) ? 1.0 : 0.0, LeftTok);
            if (LI && RI)
                return Context.Make<NumberNode>(((LI->Value > 0.5) != (RI->Value > 0.5)) ? 1.0 : 0.0, LeftTok);
            return nullptr;
        case BinOp::BitwiseAnd:
            if (LN && RN)
                return GetBitwiseNumberResult(
                    Context, static_cast<int64_t>(LN->Value) & static_cast<int64_t>(RN->Value), LeftTok);
            if (LN && RI)
                return Context.Make<Int64Node>(static_cast<int64_t>(LN->Value) & RI->Value, LeftTok);
            if (LI && RN)
                return Context.Make<Int64Node>(LI->Value & static_cast<int64_t>(RN->Value), LeftTok);
            if (LI && RI)
                return Context.Make<Int64Node>(LI->Value & RI->Value, LeftTok);
            return nullptr;
        case BinOp::BitwiseOr:
            if (LN && RN)
                return GetBitwiseNumberResult(
                    Context, static_cast<int64_t>(LN->Value) | static_cast<int64_t>(RN->Value), LeftTok);
            if (LN && RI)
                return Context.Make<Int64Node>(static_cast<int64_t>(LN->Value) | RI->Value, LeftTok);
            if (LI && RN)
                return Context.Make<Int64Node>(LI->Value | static_cast<int64_t>(RN->Value), LeftTok);
            if (LI && RI)
                return Context.Make<Int64Node>(LI->Value | RI->Value, LeftTok);
            return nullptr;
        case BinOp::BitwiseXor:
            if (LN && RN)
                return GetBitwiseNumberResult(
                    Context, static_cast<int64_t>(LN->Value) ^ static_cast<int64_t>(RN->Value), LeftTok);
            if (LN && RI)
                return Context.Make<Int64Node>(static_cast<int64_t>(LN->Value) ^ RI->Value, LeftTok);
            if (LI && RN)
                return Context.Make<Int64Node>(LI->Value ^ static_cast<int64_t>(RN->Value), LeftTok);
            if (LI && RI)
                return Context.Make<Int64Node>(LI->Value ^ RI->Value, LeftTok);
            return nullptr;
        case BinOp::BitwiseShiftLeft: {

            int RShiftI = (LN || LI) && RI   ? static_cast<int>(RI->Value)
                          : (LN || LI) && RN ? static_cast<int>(RN->Value)
                                             : 0;
            int64_t LValI = LI ? LI->Value : LN ? static_cast<int64_t>(LN->Value) : 0;
            // Mask the count like C# long<<int does; negative counts are UB in
            // C++ but well-defined (& 63) upstream.
            int64_t Shifted = (RShiftI >= 64) ? 0 : (LValI << (RShiftI & 63));
            if (LN && RN)
                return GetBitwiseNumberResult(Context, Shifted, LeftTok);
            if (LN && RI)
                return GetBitwiseNumberResult(Context, Shifted, LeftTok);
            if (LI && RN)
                return Context.Make<Int64Node>(Shifted, LeftTok);
            if (LI && RI)
                return Context.Make<Int64Node>(Shifted, LeftTok);
            return nullptr;
        }
        case BinOp::BitwiseShiftRight: {
            int RShiftI = (LN || LI) && RI   ? static_cast<int>(RI->Value)
                          : (LN || LI) && RN ? static_cast<int>(RN->Value)
                                             : 0;
            int64_t LValI = LI ? LI->Value : LN ? static_cast<int64_t>(LN->Value) : 0;
            int64_t Shifted = (RShiftI >= 64) ? 0 : (LValI >> (RShiftI & 63));
            if (LN && RN)
                return GetBitwiseNumberResult(Context, Shifted, LeftTok);
            if (LN && RI)
                return GetBitwiseNumberResult(Context, Shifted, LeftTok);
            if (LI && RN)
                return Context.Make<Int64Node>(Shifted, LeftTok);
            if (LI && RI)
                return Context.Make<Int64Node>(Shifted, LeftTok);
            return nullptr;
        }
    }
    return nullptr;
}

// Constant-fold left-to-right while consecutive arguments are constants. Stops at the
// first non-constant; if the whole chain collapses to one value, return it directly.
IASTNode* BinaryChainNode::PostProcess(ParseContext& Context) {
    for (auto& A : Arguments)
        A = A->PostProcess(Context);

    while (Arguments.size() >= 2) {
        IConstantASTNode* L = dynamic_cast<IConstantASTNode*>(Arguments[0]);
        IConstantASTNode* R = dynamic_cast<IConstantASTNode*>(Arguments[1]);
        if (L == nullptr || R == nullptr)
            break;
        IConstantASTNode* Folded = PerformConstantOperation(Context, Operations[0], L, R);
        if (Folded == nullptr)
            break;
        Arguments.erase(Arguments.begin());
        Arguments[0] = Folded;
        Operations.erase(Operations.begin());
    }
    if (Arguments.size() == 1)
        return Arguments[0];
    return this;
}

IASTNode* BinaryChainNode::Duplicate(ParseContext& Context) {
    std::vector<IASTNode*> NewArgs(Arguments);
    std::vector<BinaryOperation> NewOps(Operations);
    for (auto& A : NewArgs)
        A = A->Duplicate(Context);
    return Context.Make<BinaryChainNode>(_NearbyToken, std::move(NewArgs), std::move(NewOps));
}

static void CoerceBinaryDataType(Bytecode::BytecodeContext& Context, BinOp Operation) {
    DT SourceType = Context.PeekDataType();
    switch (Operation) {
        case BinOp::Add:
        case BinOp::Subtract:
        case BinOp::Multiply:
        case BinOp::GMLDivRemainder:
        case BinOp::GMLModulo:
            if (SourceType == DT::Boolean) {
                Context.Emit(Op::Convert, DT::Boolean, DT::Int32);
                Context.PopDataType();
                Context.PushDataType(DT::Int32);
            }
            break;
        case BinOp::Divide:
            if (SourceType != DT::Double && SourceType != DT::Variable) {
                Context.Emit(Op::Convert, SourceType, DT::Double);
                Context.PopDataType();
                Context.PushDataType(DT::Double);
            }
            break;
        case BinOp::LogicalAnd:
        case BinOp::LogicalOr:
        case BinOp::LogicalXor:
            if (SourceType != DT::Boolean) {
                Context.Emit(Op::Convert, SourceType, DT::Boolean);
                Context.PopDataType();
                Context.PushDataType(DT::Boolean);
            }
            break;
        case BinOp::BitwiseShiftLeft:
        case BinOp::BitwiseShiftRight:
            if (SourceType != DT::Int64) {
                Context.Emit(Op::Convert, SourceType, DT::Int64);
                Context.PopDataType();
                Context.PushDataType(DT::Int64);
            }
            break;
        case BinOp::BitwiseAnd:
        case BinOp::BitwiseOr:
        case BinOp::BitwiseXor:
            if (SourceType == DT::Variable || SourceType == DT::Double) {
                Context.Emit(Op::Convert, SourceType, DT::Int64);
                Context.PopDataType();
                Context.PushDataType(DT::Int64);
            } else if (SourceType == DT::Boolean || SourceType == DT::String) {
                Context.Emit(Op::Convert, SourceType, DT::Int32);
                Context.PopDataType();
                Context.PushDataType(DT::Int32);
            }
            break;
        default:
            break;
    }
}

void BinaryChainNode::GenerateCode(Bytecode::BytecodeContext& Context) {
    Arguments[0]->GenerateCode(Context);
    CoerceBinaryDataType(Context, Operations[0]);

    // Older runtimes evaluate logical && / || eagerly; newer ones short-circuit and
    // need a different codegen path that threads branch patches through each operand.
    if (Context.CompileContextRef().GameContext().UsingLogicalShortCircuit()) {
        for (BinaryOperation O : Operations) {
            if (O == BinOp::LogicalAnd || O == BinOp::LogicalOr) {
                GenerateShortCircuitCode(Context);
                return;
            }
        }
    }

    int64_t LastArrayOwnerID = Context.LastArrayOwnerID();
    bool ArrayOwnerChanged = false;

    for (size_t i = 1; i < Arguments.size(); i++) {
        BinaryOperation CurrentOp = Operations[i - 1];
        Arguments[i]->GenerateCode(Context);
        CoerceBinaryDataType(Context, CurrentOp);

        ArrayOwnerChanged |= (Context.LastArrayOwnerID() != LastArrayOwnerID);

        DT RightType = Context.PopDataType();
        DT LeftType = Context.PopDataType();

        Op Opcode;
        switch (CurrentOp) {
            case BinOp::Add:
                Opcode = Op::Add;
                break;
            case BinOp::Subtract:
                Opcode = Op::Subtract;
                break;
            case BinOp::Multiply:
                Opcode = Op::Multiply;
                break;
            case BinOp::Divide:
                Opcode = Op::Divide;
                break;
            case BinOp::GMLModulo:
                Opcode = Op::GMLModulo;
                break;
            case BinOp::GMLDivRemainder:
                Opcode = Op::GMLDivRemainder;
                break;
            case BinOp::CompareLesser:
            case BinOp::CompareLesserEqual:
            case BinOp::CompareEqual:
            case BinOp::CompareNotEqual:
            case BinOp::CompareGreater:
            case BinOp::CompareGreaterEqual:
                Opcode = Op::Compare;
                break;
            case BinOp::LogicalAnd:
            case BinOp::BitwiseAnd:
                Opcode = Op::And;
                break;
            case BinOp::LogicalOr:
            case BinOp::BitwiseOr:
                Opcode = Op::Or;
                break;
            case BinOp::LogicalXor:
            case BinOp::BitwiseXor:
                Opcode = Op::Xor;
                break;
            case BinOp::BitwiseShiftLeft:
                Opcode = Op::ShiftLeft;
                break;
            case BinOp::BitwiseShiftRight:
                Opcode = Op::ShiftRight;
                break;
            default:
                throw std::runtime_error("Invalid binary operation");
        }
        Context.PushDataType(VMDataTypeExtensions::BinaryResultWith(LeftType, Opcode, RightType));

        if (Opcode == Op::Compare) {
            CT Comparison;
            switch (CurrentOp) {
                case BinOp::CompareLesser:
                    Comparison = CT::LesserThan;
                    break;
                case BinOp::CompareLesserEqual:
                    Comparison = CT::LesserEqualThan;
                    break;
                case BinOp::CompareEqual:
                    Comparison = CT::EqualTo;
                    break;
                case BinOp::CompareNotEqual:
                    Comparison = CT::NotEqualTo;
                    break;
                case BinOp::CompareGreater:
                    Comparison = CT::GreaterThan;
                    break;
                case BinOp::CompareGreaterEqual:
                    Comparison = CT::GreaterEqualThan;
                    break;
                default:
                    throw std::runtime_error("Invalid comparison type");
            }
            Context.Emit(Op::Compare, Comparison, RightType, LeftType);
        } else {
            Context.Emit(Opcode, RightType, LeftType);
        }
    }

    if (ArrayOwnerChanged)
        Context.SetLastArrayOwnerID(-1);
}

void BinaryChainNode::GenerateShortCircuitCode(Bytecode::BytecodeContext& Context) {
    Bytecode::MultiForwardBranchPatch FalseShortCircuit;
    Bytecode::MultiForwardBranchPatch TrueShortCircuit;
    Bytecode::MultiForwardBranchPatch NoShortCircuit;

    int64_t LastArrayOwnerID = Context.LastArrayOwnerID();
    bool ArrayOwnerChanged = false;

    for (size_t i = 1; i < Arguments.size(); i++) {
        Context.ConvertDataType(DT::Boolean);
        if (Operations[i - 1] == BinOp::LogicalAnd) {
            FalseShortCircuit.AddInstruction(Context, Context.Emit(Op::BranchFalse));
        } else {
            TrueShortCircuit.AddInstruction(Context, Context.Emit(Op::BranchTrue));
        }
        Arguments[i]->GenerateCode(Context);
        ArrayOwnerChanged |= (Context.LastArrayOwnerID() != LastArrayOwnerID);
    }

    if (ArrayOwnerChanged)
        Context.SetLastArrayOwnerID(-1);

    Context.ConvertDataType(DT::Boolean);
    Context.PushDataType(DT::Boolean);

    if (FalseShortCircuit.Used()) {
        NoShortCircuit.AddInstruction(Context, Context.Emit(Op::Branch));
        FalseShortCircuit.Patch(Context);
        Context.Emit(Op::Push, static_cast<int16_t>(0), DT::Int16);
    }
    if (TrueShortCircuit.Used()) {
        NoShortCircuit.AddInstruction(Context, Context.Emit(Op::Branch));
        TrueShortCircuit.Patch(Context);
        Context.Emit(Op::Push, static_cast<int16_t>(1), DT::Int16);
    }
    NoShortCircuit.Patch(Context);
}

} // namespace Underanalyzer::Compiler::Nodes

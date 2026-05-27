
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/FunctionCallNode.h"

#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/Bytecode/InstructionPatch.h"
#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/FunctionScope.h"
#include "Underanalyzer/Compiler/Lexer/Token.h"
#include "Underanalyzer/Compiler/Nodes/AccessorNode.h"
#include "Underanalyzer/Compiler/Nodes/DotVariableNode.h"
#include "Underanalyzer/Compiler/Nodes/NumberNode.h"
#include "Underanalyzer/Compiler/Nodes/SimpleFunctionCallNode.h"
#include "Underanalyzer/Compiler/Nodes/SimpleVariableNode.h"
#include "Underanalyzer/Compiler/Parser/Functions.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"
#include "Underanalyzer/IGameContext.h"
#include "Underanalyzer/VMConstants.h"

namespace Underanalyzer::Compiler::Nodes {

using namespace Lexer;
using Parser::ParseContext;
using Op = IGMInstruction::Opcode;
using DT = IGMInstruction::DataType;
using IT = IGMInstruction::InstanceType;
using VT = IGMInstruction::VariableType;

// Max 2047 args matches the GameMaker VM's hard-coded argument count cap.
FunctionCallNode::FunctionCallNode(ParseContext& Context, TokenSeparator* Token, IASTNode* ExpressionIn)
    : Expression(ExpressionIn), Arguments(Parser::Functions::ParseCallArguments(Context, 2047)), _NearbyToken(Token) {
}

IASTNode* FunctionCallNode::PostProcess(ParseContext& Context) {
    Expression = Expression->PostProcess(Context);
    for (auto& A : Arguments)
        A = A->PostProcess(Context);
    return this;
}

IASTNode* FunctionCallNode::Duplicate(ParseContext& Context) {
    std::vector<IASTNode*> NewArgs(Arguments);
    for (auto& A : NewArgs)
        A = A->Duplicate(Context);
    auto* N = Context.Make<FunctionCallNode>(_NearbyToken, Expression->Duplicate(Context), std::move(NewArgs));
    N->SetIsStatement(_IsStatement);
    return N;
}

// Push arguments right-to-left so the callee can pop them left-to-right at runtime.
void FunctionCallNode::GenerateArguments(Bytecode::BytecodeContext& Context) {

    for (int i = static_cast<int>(Arguments.size()) - 1; i >= 0; i--) {
        Arguments[i]->GenerateCode(Context);
        Context.ConvertDataType(DT::Variable);
    }
}

// Detects "obj.method().chain.more()" shapes where the leftmost segment is itself a
// call producing the receiver, since those need the chained-call codegen path below.
bool FunctionCallNode::FunctionCallNodeOnLeftmostSide(DotVariableNode* Node) {
    while (true) {

        if (auto* Fc = As<FunctionCallNode>(Node->LeftExpression)) {

            IASTNode* Inner = Fc->Expression;
            bool Match = false;
            if (auto* SV = As<SimpleVariableNode>(Inner); SV != nullptr && !SV->CollapsedFromDot())
                Match = true;
            if (As<FunctionCallNode>(Inner) != nullptr)
                Match = true;
            if (As<SimpleFunctionCallNode>(Inner) != nullptr)
                Match = true;
            if (As<AccessorNode>(Inner) != nullptr)
                Match = true;
            if (Match)
                return true;
        }
        if (As<SimpleFunctionCallNode>(Node->LeftExpression) != nullptr)
            return true;

        if (auto* FcDot = As<FunctionCallNode>(Node->LeftExpression)) {
            if (auto* InnerDot = As<DotVariableNode>(FcDot->Expression)) {
                Node = InnerDot;
                continue;
            }
        }
        break;
    }
    return false;
}

void FunctionCallNode::GenerateCode(Bytecode::BytecodeContext& Context) {
    GenerateCode(Context, false);
}

void FunctionCallNode::GenerateCode(Bytecode::BytecodeContext& Context, bool InChain) {
    Bytecode::VariablePatch FinalVariable("", IT::Self);
    bool ArgumentsAtTheEnd = false;

    if (As<DotVariableNode>(Expression) != nullptr) {
        InChain = false;
    }
    if (InChain) {
        GenerateArguments(Context);
    }

    if (auto* SimpleVar = As<SimpleVariableNode>(Expression)) {
        std::string_view FunctionToCall;
        int ArgsToUse;
        IT InstType = SimpleVar->ExplicitInstanceType();
        if (static_cast<int16_t>(InstType) >= 0) {

            FunctionToCall = VMConstants::GetInstanceFunction;
            ArgsToUse = 1;
            int Value = static_cast<int>(InstType);
            if (SimpleVar->RoomInstanceVariable())
                Value += 100000;
            NumberNode::GenerateCode(Context, static_cast<double>(Value));
            Context.ConvertDataType(DT::Variable);
        } else {
            switch (InstType) {
                case IT::Other:
                    FunctionToCall = VMConstants::OtherFunction;
                    break;
                case IT::Global:
                    FunctionToCall = VMConstants::GlobalFunction;
                    break;
                default:
                    FunctionToCall = VMConstants::SelfFunction;
                    break;
            }
            ArgsToUse = 0;
        }
        Context.EmitCall(Bytecode::FunctionPatch::FromBuiltin(Context, std::string(FunctionToCall)), ArgsToUse);

        if (InChain)
            Context.EmitDuplicate(DT::Variable, 0);

        FinalVariable = Bytecode::VariablePatch(SimpleVar->VariableName(), IT::Self, VT::Normal,
                                                SimpleVar->BuiltinVariable() != nullptr);
        FinalVariable.InstructionInstanceType = IT::StackTop;
    } else if (auto* DotVar = As<DotVariableNode>(Expression)) {

        if (!InChain && FunctionCallNodeOnLeftmostSide(DotVar)) {
            InChain = true;

            // Older runtimes evaluate chained-call arguments before the receiver;
            // newer ones defer them to after the receiver so side effects observe
            // the post-chain state. Defer is signaled via ArgumentsAtTheEnd.
            if (!Context.CompileContextRef().GameContext().UsingNewChainedFunctionArgumentOrder()) {
                bool PrevGen = Context.CurrentScope().GeneratingDotVariableCall();
                Context.CurrentScope().SetGeneratingDotVariableCall(true);
                GenerateArguments(Context);
                Context.CurrentScope().SetGeneratingDotVariableCall(PrevGen);
            } else {
                ArgumentsAtTheEnd = true;
            }

            if (auto* InnerFc = As<FunctionCallNode>(DotVar->LeftExpression)) {
                InnerFc->GenerateCode(Context, true);
            } else {
                DotVar->LeftExpression->GenerateCode(Context);
            }

            Context.EmitDuplicate(DT::Variable, 0);
            Context.ConvertToInstanceId();

            FinalVariable = Bytecode::VariablePatch(DotVar->VariableName(), IT::Self, VT::StackTop,
                                                    DotVar->BuiltinVariable() != nullptr);
        } else {
            DotVar->LeftExpression->GenerateCode(Context);
            if (Context.ConvertDataType(DT::Variable)) {
                Context.EmitCall(
                    Bytecode::FunctionPatch::FromBuiltin(Context, std::string(VMConstants::GetInstanceFunction)), 1);
            }
            FinalVariable = Bytecode::VariablePatch(DotVar->VariableName(), IT::Self, VT::Normal,
                                                    DotVar->BuiltinVariable() != nullptr);
            FinalVariable.InstructionInstanceType = IT::StackTop;
        }
    } else if (As<FunctionCallNode>(Expression) != nullptr || As<SimpleFunctionCallNode>(Expression) != nullptr ||
               As<AccessorNode>(Expression) != nullptr) {
        if (!InChain)
            GenerateArguments(Context);

        bool DupInstance = false;
        if (auto* Acc = As<AccessorNode>(Expression)) {
            AccessorNode* Leftmost = Acc;
            while (auto* Further = As<AccessorNode>(Leftmost->Expression))
                Leftmost = Further;
            if (As<DotVariableNode>(Leftmost->Expression) != nullptr) {
                DupInstance = true;
            } else if (auto* SVColl = As<SimpleVariableNode>(Leftmost->Expression);
                       SVColl != nullptr && SVColl->CollapsedFromDot()) {
                DupInstance = true;

                if (SVColl->ExplicitInstanceType() == IT::Self) {
                    Context.EmitCall(
                        Bytecode::FunctionPatch::FromBuiltin(Context, std::string(VMConstants::SelfFunction)), 0);
                    SVColl->SetExplicitInstanceType(IT::StackTop);
                }
            }
        }

        if (!DupInstance) {
            Context.EmitCall(Bytecode::FunctionPatch::FromBuiltin(Context, std::string(VMConstants::SelfFunction)), 0);
        }

        if (auto* InnerFc = As<FunctionCallNode>(Expression)) {
            InnerFc->GenerateCode(Context, true);
        } else {
            Expression->GenerateCode(Context);
        }
        Context.PopDataType();

        if (DupInstance)
            Context.EmitDuplicate(DT::Variable, 0);

        if (!Context.CompileContextRef().GameContext().UsingGMLv2()) {
            Context.CompileContextRef().PushError("Cannot call variables as functions before GMLv2 (GameMaker 2.3+)",
                                                  Expression->NearbyToken());
        }

        Context.EmitCallVariable(static_cast<int>(Arguments.size()));
        Context.PushDataType(DT::Variable);

        if (_IsStatement)
            Context.Emit(Op::PopDelete, Context.PopDataType());
        return;
    } else {
        if (!InChain)
            GenerateArguments(Context);

        Context.EmitCall(Bytecode::FunctionPatch::FromBuiltin(Context, std::string(VMConstants::SelfFunction)), 0);
        Expression->GenerateCode(Context);
        Context.PopDataType();

        if (!Context.CompileContextRef().GameContext().UsingGMLv2()) {
            Context.CompileContextRef().PushError("Cannot call variables as functions before GMLv2 (GameMaker 2.3+)",
                                                  Expression->NearbyToken());
        }

        Context.EmitCallVariable(static_cast<int>(Arguments.size()));
        Context.PushDataType(DT::Variable);

        if (_IsStatement)
            Context.Emit(Op::PopDelete, Context.PopDataType());
        return;
    }

    if (!InChain) {
        if (As<DotVariableNode>(Expression) != nullptr) {
            bool PrevGen = Context.CurrentScope().GeneratingDotVariableCall();
            Context.CurrentScope().SetGeneratingDotVariableCall(true);
            GenerateArguments(Context);
            Context.CurrentScope().SetGeneratingDotVariableCall(PrevGen);
        } else {
            GenerateArguments(Context);
        }

        Context.EmitDupSwap(DT::Variable, static_cast<uint8_t>(Arguments.size()), 1);
        Context.EmitDuplicate(DT::Variable, 0);
    }

    Context.Emit(Op::Push, FinalVariable, DT::Variable);

    if (!Context.CompileContextRef().GameContext().UsingGMLv2()) {
        Context.CompileContextRef().PushError("Cannot call variables as functions before GMLv2 (GameMaker 2.3+)",
                                              Expression->NearbyToken());
    }

    if (ArgumentsAtTheEnd) {
        bool PrevGen = Context.CurrentScope().GeneratingDotVariableCall();
        Context.CurrentScope().SetGeneratingDotVariableCall(true);
        GenerateArguments(Context);
        Context.CurrentScope().SetGeneratingDotVariableCall(PrevGen);

        Context.EmitDupSwap(DT::Int16, 2, static_cast<uint8_t>(Arguments.size()));
    }

    Context.EmitCallVariable(static_cast<int>(Arguments.size()));
    Context.PushDataType(DT::Variable);

    if (_IsStatement)
        Context.Emit(Op::PopDelete, Context.PopDataType());
}

std::vector<IASTNode*> FunctionCallNode::EnumerateChildren() {
    std::vector<IASTNode*> Out;
    Out.reserve(1 + Arguments.size());
    Out.push_back(Expression);
    for (auto* A : Arguments)
        Out.push_back(A);
    return Out;
}

} // namespace Underanalyzer::Compiler::Nodes

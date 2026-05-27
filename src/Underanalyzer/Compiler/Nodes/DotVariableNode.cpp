
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/DotVariableNode.h"

#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/Bytecode/InstructionPatch.h"
#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/Lexer/Token.h"
#include "Underanalyzer/Compiler/Nodes/AccessorNode.h"
#include "Underanalyzer/Compiler/Nodes/AssetReferenceNode.h"
#include "Underanalyzer/Compiler/Nodes/AssignNode.h"
#include "Underanalyzer/Compiler/Nodes/EnumDeclaration.h"
#include "Underanalyzer/Compiler/Nodes/Int64Node.h"
#include "Underanalyzer/Compiler/Nodes/NumberNode.h"
#include "Underanalyzer/Compiler/Nodes/SimpleVariableNode.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"
#include "Underanalyzer/IGameContext.h"

namespace Underanalyzer::Compiler::Nodes {

using Parser::ParseContext;
using Op = IGMInstruction::Opcode;
using DT = IGMInstruction::DataType;
using IT = IGMInstruction::InstanceType;
using VT = IGMInstruction::VariableType;
using ICT = Bytecode::BytecodeContext::InstanceConversionType;

DotVariableNode::DotVariableNode(IASTNode* LeftExpressionIn, Lexer::TokenVariable* Token)
    : LeftExpression(LeftExpressionIn), _VariableName(Token->Text), _BuiltinVariable(Token->BuiltinVariable),
      _NearbyToken(Token) {
}

DotVariableNode::DotVariableNode(IASTNode* LeftExpressionIn, Lexer::TokenFunction* Token)
    : LeftExpression(LeftExpressionIn), _VariableName(Token->Text), _BuiltinVariable(nullptr), _NearbyToken(Token) {
}

// Collapse "<integer>.<name>" into a single qualified variable: integers below the
// instance-id range are InstanceTypes, values >= 100000 are room instance ids.
IASTNode* DotVariableNode::PostProcess(ParseContext& Context) {

    if (auto* NumberLeft = As<NumberNode>(LeftExpression)) {
        if (static_cast<double>(static_cast<int>(NumberLeft->Value)) == NumberLeft->Value) {
            SimpleVariableNode* Combined = Context.Make<SimpleVariableNode>(_VariableName, _BuiltinVariable);
            int IntValue = static_cast<int>(NumberLeft->Value);
            if (IntValue >= 100000) {
                IntValue -= 100000;
                Combined->SetRoomInstanceVariable(true);
            }
            int16_t ShortValue = static_cast<int16_t>(IntValue);
            Combined->SetExplicitInstanceType(static_cast<IT>(ShortValue));
            Combined->SetCollapsedFromDot(true);
            return Combined;
        }
    }

    if (!Context.CompileContextRef().GameContext().UsingAssetReferences()) {
        if (auto* AssetLeft = As<AssetReferenceNode>(LeftExpression)) {
            SimpleVariableNode* Combined = Context.Make<SimpleVariableNode>(_VariableName, _BuiltinVariable);
            Combined->SetExplicitInstanceType(static_cast<IT>(AssetLeft->AssetId));
            Combined->SetCollapsedFromDot(true);
            return Combined;
        }
    }

    LeftExpression = LeftExpression->PostProcess(Context);

    // EnumName.Member resolves to its integer value at compile time. Check the
    // in-progress map first so forward references inside the same enum work.
    if (auto* LeftSV = As<SimpleVariableNode>(LeftExpression)) {
        const std::string& EnumName = LeftSV->VariableName();
        auto ParseIt = Context.ParseEnums().find(EnumName);
        if (ParseIt != Context.ParseEnums().end()) {
            auto ValIt = ParseIt->second->IntegerValues.find(_VariableName);
            if (ValIt != ParseIt->second->IntegerValues.end()) {
                return Context.Make<Int64Node>(ValIt->second, _NearbyToken);
            }
        }
        auto EnumIt = Context.CompileContextRef().Enums().find(EnumName);
        if (EnumIt != Context.CompileContextRef().Enums().end()) {
            int64_t Value = 0;
            if (EnumIt->second->TryGetValue(_VariableName, Value)) {
                return Context.Make<Int64Node>(Value, _NearbyToken);
            }
            Context.CompileContextRef().PushError(
                "Failed to find enum value for '" + EnumName + "." + _VariableName + "'", _NearbyToken);
        }
    }

    if (auto* LeftSV = As<SimpleVariableNode>(LeftExpression)) {
        LeftSV->SetLeftmostSideOfDot(true);
    } else if (auto* LeftAcc = As<AccessorNode>(LeftExpression)) {
        LeftAcc->SetLeftmostSideOfDot(true);
    }
    return this;
}

IASTNode* DotVariableNode::Duplicate(ParseContext& Context) {
    return Context.Make<DotVariableNode>(LeftExpression->Duplicate(Context), _VariableName, _BuiltinVariable,
                                         _NearbyToken);
}

void DotVariableNode::GenerateCode(Bytecode::BytecodeContext& Context) {
    LeftExpression->GenerateCode(Context);
    Context.ConvertToInstanceId();
    Bytecode::VariablePatch Patch(_VariableName, IT::Self, VT::StackTop, _BuiltinVariable != nullptr);
    Context.Emit(Op::Push, Patch, DT::Variable);
    Context.PushDataType(DT::Variable);
}

void DotVariableNode::GenerateAssignCode(Bytecode::BytecodeContext& Context) {
    LeftExpression->GenerateCode(Context);
    Context.ConvertToInstanceId();
    Bytecode::VariablePatch Patch(_VariableName, IT::Self, VT::StackTop, _BuiltinVariable != nullptr);
    Context.Emit(Op::Pop, Patch, DT::Variable, Context.PopDataType());
}

void DotVariableNode::GenerateCompoundAssignCode(Bytecode::BytecodeContext& Context, IASTNode* Expression,
                                                 Op OperationOpcode) {
    LeftExpression->GenerateCode(Context);
    ICT ConversionType = Context.ConvertToInstanceId();

    if (ConversionType == ICT::StacktopId) {
        Context.EmitDuplicate(DT::Int32, 4);
    } else {
        Context.EmitDuplicate(DT::Int32, 0);
    }

    Bytecode::VariablePatch Patch(_VariableName, IT::Self, VT::StackTop, _BuiltinVariable != nullptr);
    Context.Emit(Op::Push, Patch, DT::Variable);
    Expression->GenerateCode(Context);
    AssignNode::PerformCompoundOperation(Context, OperationOpcode);
    Context.Emit(Op::Pop, Patch, DT::Int32, DT::Variable);
}

static void PrePostDuplicateAndSwap(Bytecode::BytecodeContext& Context, ICT ConversionType) {
    Context.EmitDuplicate(DT::Variable, 0);
    Context.PushDataType(DT::Variable);
    if (Context.CompileContextRef().GameContext().UsingGMLv2()) {
        if (ConversionType == ICT::StacktopId) {
            Context.EmitDupSwap(DT::Int32, 4, 9);
        } else {
            Context.EmitDupSwap(DT::Int32, 4, 5);
        }
    } else {
        Context.EmitPopSwap(5);
    }
}

void DotVariableNode::GeneratePrePostAssignCode(Bytecode::BytecodeContext& Context, bool IsIncrement, bool IsPre,
                                                bool IsStatement) {
    LeftExpression->GenerateCode(Context);
    ICT ConversionType = Context.ConvertToInstanceId();

    if (ConversionType == ICT::StacktopId) {
        Context.EmitDuplicate(DT::Int32, 4);
    } else {
        Context.EmitDuplicate(DT::Int32, 0);
    }

    Bytecode::VariablePatch Patch(_VariableName, IT::Self, VT::StackTop, _BuiltinVariable != nullptr);
    Context.Emit(Op::Push, Patch, DT::Variable);

    if (!IsStatement && !IsPre)
        PrePostDuplicateAndSwap(Context, ConversionType);

    Context.Emit(Op::Push, static_cast<int16_t>(1), DT::Int16);
    Context.Emit(IsIncrement ? Op::Add : Op::Subtract, DT::Int32, DT::Variable);

    if (!IsStatement && IsPre)
        PrePostDuplicateAndSwap(Context, ConversionType);

    Context.Emit(Op::Pop, Patch, DT::Int32, DT::Variable);
}

} // namespace Underanalyzer::Compiler::Nodes

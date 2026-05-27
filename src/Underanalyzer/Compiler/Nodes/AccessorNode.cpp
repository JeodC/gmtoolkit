
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/AccessorNode.h"

#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/Bytecode/InstructionPatch.h"
#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/FunctionScope.h"
#include "Underanalyzer/Compiler/Lexer/Token.h"
#include "Underanalyzer/Compiler/Nodes/AssignNode.h"
#include "Underanalyzer/Compiler/Nodes/DotVariableNode.h"
#include "Underanalyzer/Compiler/Nodes/NumberNode.h"
#include "Underanalyzer/Compiler/Nodes/SimpleVariableNode.h"
#include "Underanalyzer/Compiler/Nodes/StringNode.h"
#include "Underanalyzer/Compiler/Parser/Expressions.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"
#include "Underanalyzer/IGameContext.h"

#include <stdexcept>

namespace Underanalyzer::Compiler::Nodes {

using namespace Lexer;
using Parser::ParseContext;
using Op = IGMInstruction::Opcode;
using ExtOp = IGMInstruction::ExtendedOpcode;
using DT = IGMInstruction::DataType;
using IT = IGMInstruction::InstanceType;
using VT = IGMInstruction::VariableType;
using ICT = Bytecode::BytecodeContext::InstanceConversionType;

AccessorNode* AccessorNode::Parse(ParseContext& Context, TokenSeparator* Token, IASTNode* ExpressionIn,
                                  AccessorKind KindIn) {
    bool DisallowStrings = (KindIn == AccessorKind::Array || KindIn == AccessorKind::ArrayDirect ||
                            KindIn == AccessorKind::List || KindIn == AccessorKind::Grid);

    IASTNode* AccExpr = Parser::Expressions::ParseExpression(Context);
    if (AccExpr == nullptr)
        return nullptr;
    if (DisallowStrings && As<StringNode>(AccExpr) != nullptr) {
        Context.CompileContextRef().PushError("String used in accessor that does not support strings",
                                              AccExpr->NearbyToken());
    }

    IASTNode* AccExpr2 = nullptr;
    if ((KindIn == AccessorKind::Array || KindIn == AccessorKind::Grid) &&
        Context.IsCurrentToken(SeparatorKind::Comma)) {
        Context.SetPosition(Context.Position() + 1);
        AccExpr2 = Parser::Expressions::ParseExpression(Context);
        if (AccExpr2 == nullptr)
            return nullptr;
        if (DisallowStrings && As<StringNode>(AccExpr2) != nullptr) {
            Context.CompileContextRef().PushError("String used in accessor that does not support strings",
                                                  AccExpr2->NearbyToken());
        }
    } else if (KindIn == AccessorKind::Grid) {
        Context.CompileContextRef().PushError("Expected two arguments to grid accessor", Token);
    }

    Context.EnsureToken(SeparatorKind::ArrayClose);
    return Context.Make<AccessorNode>(Token, ExpressionIn, KindIn, AccExpr, AccExpr2);
}

// Rewrite "a[i, j]" as "a[i][j]" for GMLv2; older bytecode keeps the two indices in a
// single accessor (handled by the CheckArrayIndex / Multiply-by-32000 trick below).
AccessorNode* AccessorNode::Convert2DArrayToTwoAccessors(ParseContext& Context) {
    if (KindValue == AccessorKind::Array && AccessorExpression2 != nullptr) {
        IASTNode* Second = AccessorExpression2;
        AccessorExpression2 = nullptr;
        return Context.Make<AccessorNode>(_NearbyToken, this, KindValue, Second);
    }
    return this;
}

IASTNode* AccessorNode::PostProcess(ParseContext& Context) {

    if (auto* Dot = As<DotVariableNode>(Expression)) {
        if (auto* NumLeft = As<NumberNode>(Dot->LeftExpression)) {
            bool ConstantIsGlobal = NumLeft->ConstantName.has_value() && *NumLeft->ConstantName == "global";
            if (Context.CompileContextRef().GameContext().UsingSelfToBuiltin() || ConstantIsGlobal) {
                Dot->LeftExpression = NumLeft->PostProcess(Context);
            }
        }
    }

    if (auto* SV = As<SimpleVariableNode>(Expression)) {
        if (!SV->CollapsedFromDot() && !SV->HasExplicitInstanceType() &&
            SimpleVariableNode::BuiltinArgumentVariables().count(SV->VariableName()) > 0 &&
            &Context.CurrentScope() == &Context.RootScope() &&
            !Context.CompileContextRef().GameContext().UsingSelfToBuiltin()) {
            SV->SetExplicitInstanceType(IT::Argument);
        }
    }

    Expression = Expression->PostProcess(Context);
    AccessorExpression = AccessorExpression->PostProcess(Context);
    if (AccessorExpression2 != nullptr)
        AccessorExpression2 = AccessorExpression2->PostProcess(Context);

    return this;
}

IASTNode* AccessorNode::Duplicate(ParseContext& Context) {
    return Context.Make<AccessorNode>(_NearbyToken, Expression->Duplicate(Context), KindValue,
                                      AccessorExpression->Duplicate(Context),
                                      AccessorExpression2 ? AccessorExpression2->Duplicate(Context) : nullptr);
}

std::pair<IT, ICT> AccessorNode::GenerateVariableCode(Bytecode::BytecodeContext& Context, IVariableASTNode* Variable) {
    IT InstanceTypeOut;
    ICT InstanceConversionType;

    if (auto* SV = As<SimpleVariableNode>(Variable)) {
        IT StackInstanceType = SV->ExplicitInstanceType();
        if (StackInstanceType == IT::Self) {
            // Self-rooted accesses to builtin variables target the Builtin instance type
            // in newer runtimes; calls always do, regardless of version.
            if (SV->IsFunctionCall() || (!_LeftmostSideOfDot && !SV->CollapsedFromDot() &&
                                         Context.CompileContextRef().GameContext().UsingSelfToBuiltin())) {
                StackInstanceType = IT::Builtin;
            }
        }
        NumberNode::GenerateCode(Context, static_cast<double>(static_cast<int>(StackInstanceType)));
        InstanceConversionType = Context.ConvertToInstanceId();

        InstanceTypeOut = SV->ExplicitInstanceType();
        if (InstanceTypeOut == IT::Other && !Context.CompileContextRef().GameContext().UsingGMLv2()) {
            InstanceTypeOut = IT::Self;
        }
    } else if (auto* Dot = As<DotVariableNode>(Variable)) {
        Dot->LeftExpression->GenerateCode(Context);
        InstanceConversionType = Context.ConvertToInstanceId();
        InstanceTypeOut = IT::Self;
    } else {
        throw std::logic_error("Invalid expression on accessor variable");
    }

    AccessorExpression->GenerateCode(Context);
    Context.ConvertDataType(DT::Int32);

    // Pre-GMLv2 2D arrays: collapse (i, j) into one index via i*32000 + j. The 32000
    // multiplier is baked into the runtime; CheckArrayIndex rejects values >= it.
    if (AccessorExpression2 != nullptr) {
        Context.Emit(ExtOp::CheckArrayIndex);
        Context.Emit(Op::Push, static_cast<int32_t>(32000), DT::Int32);
        Context.Emit(Op::Multiply, DT::Int32, DT::Int32);
        AccessorExpression2->GenerateCode(Context);
        Context.ConvertDataType(DT::Int32);
        Context.Emit(ExtOp::CheckArrayIndex);
        Context.Emit(Op::Add, DT::Int32, DT::Int32);
    }

    return { InstanceTypeOut, InstanceConversionType };
}

void AccessorNode::GenerateChainedCode(Bytecode::BytecodeContext& Context, bool IsPop) {
    if (auto* InnerAcc = As<AccessorNode>(Expression)) {
        InnerAcc->GenerateChainedCode(Context, IsPop);

        AccessorExpression->GenerateCode(Context);
        Context.ConvertDataType(DT::Int32);
        Context.Emit(ExtOp::PushArrayContainer);
    } else if (auto* Var = dynamic_cast<IVariableASTNode*>(Expression)) {
        auto [PushInstanceType, _] = GenerateVariableCode(Context, Var);
        VT VarType = IsPop ? VT::MultiPushPop : VT::MultiPush;
        Bytecode::VariablePatch Patch(Var->VariableName(), PushInstanceType, VarType,
                                      Var->BuiltinVariable() != nullptr);
        Context.Emit(Op::Push, Patch, DT::Variable);
    } else {
        throw std::runtime_error("Invalid expression on accessor");
    }
}

void AccessorNode::GenerateCode(Bytecode::BytecodeContext& Context) {
    if (auto* Var = dynamic_cast<IVariableASTNode*>(Expression)) {
        auto [PushInstanceType, _] = GenerateVariableCode(Context, Var);
        Bytecode::VariablePatch Patch(Var->VariableName(), PushInstanceType, VT::Array,
                                      Var->BuiltinVariable() != nullptr);
        Context.Emit(Op::Push, Patch, DT::Variable);
        Context.PushDataType(DT::Variable);
    } else if (auto* InnerAcc = As<AccessorNode>(Expression)) {
        InnerAcc->GenerateChainedCode(Context, false);
        AccessorExpression->GenerateCode(Context);
        Context.ConvertDataType(DT::Int32);
        Context.Emit(ExtOp::PushArrayFinal);
        Context.PushDataType(DT::Variable);
    } else {
        throw std::runtime_error("Invalid expression on accessor");
    }
}

void AccessorNode::GenerateAssignCode(Bytecode::BytecodeContext& Context) {
    DT StoreType = Context.PopDataType();
    if (StoreType != DT::Variable && Context.CompileContextRef().GameContext().UsingGMLv2()) {
        Context.Emit(Op::Convert, StoreType, DT::Variable);
        StoreType = DT::Variable;
    }

    if (auto* Var = dynamic_cast<IVariableASTNode*>(Expression)) {
        auto [PopInstanceType, _] = GenerateVariableCode(Context, Var);
        Bytecode::VariablePatch Patch(Var->VariableName(), PopInstanceType, VT::Array,
                                      Var->BuiltinVariable() != nullptr);
        Context.Emit(Op::Pop, Patch, DT::Variable, StoreType);
    } else if (auto* InnerAcc = As<AccessorNode>(Expression)) {
        InnerAcc->GenerateChainedCode(Context, true);
        AccessorExpression->GenerateCode(Context);
        Context.ConvertDataType(DT::Int32);
        Context.Emit(ExtOp::PopArrayFinal);
    } else {
        throw std::runtime_error("Invalid expression on accessor");
    }
}

void AccessorNode::GenerateCompoundAssignCode(Bytecode::BytecodeContext& Context, IASTNode* ExpressionIn,
                                              Op OperationOpcode) {
    if (auto* Var = dynamic_cast<IVariableASTNode*>(Expression)) {
        auto [InstanceTypeIn, ConversionType] = GenerateVariableCode(Context, Var);

        if (ConversionType == ICT::StacktopId) {
            Context.EmitDuplicate(DT::Int32, 5);
        } else {
            Context.EmitDuplicate(DT::Int32, 1);
        }

        Bytecode::VariablePatch Patch(Var->VariableName(), InstanceTypeIn, VT::Array,
                                      Var->BuiltinVariable() != nullptr);
        Context.Emit(Op::Push, Patch, DT::Variable);
        ExpressionIn->GenerateCode(Context);
        AssignNode::PerformCompoundOperation(Context, OperationOpcode);
        Context.Emit(Op::Pop, Patch, DT::Int32, DT::Variable);
    } else if (auto* InnerAcc = As<AccessorNode>(Expression)) {
        InnerAcc->GenerateChainedCode(Context, true);
        AccessorExpression->GenerateCode(Context);
        Context.ConvertDataType(DT::Int32);
        Context.EmitDuplicate(DT::Int32, 4);
        Context.Emit(ExtOp::SaveArrayReference);
        Context.Emit(ExtOp::PushArrayFinal);
        ExpressionIn->GenerateCode(Context);
        AssignNode::PerformCompoundOperation(Context, OperationOpcode);
        Context.Emit(ExtOp::RestoreArrayReference);
        Context.EmitDupSwap(DT::Int32, 4, 5);
        Context.Emit(ExtOp::PopArrayFinal);
    } else {
        throw std::runtime_error("Invalid expression on accessor");
    }
}

static void PrePostDuplicateAndSwap(Bytecode::BytecodeContext& Context, ICT ConversionType) {
    Context.EmitDuplicate(DT::Variable, 0);
    Context.PushDataType(DT::Variable);
    if (Context.CompileContextRef().GameContext().UsingGMLv2()) {
        if (ConversionType == ICT::StacktopId) {
            Context.EmitDupSwap(DT::Int32, 4, 10);
        } else {
            Context.EmitDupSwap(DT::Int32, 4, 6);
        }
    } else {
        Context.EmitPopSwap(6);
    }
}

static void MultiArrayPrePostDuplicateAndSwap(Bytecode::BytecodeContext& Context) {
    Context.EmitDuplicate(DT::Variable, 0);
    Context.PushDataType(DT::Variable);
    Context.EmitDupSwap(DT::Int32, 4, 9);
}

void AccessorNode::GeneratePrePostAssignCode(Bytecode::BytecodeContext& Context, bool IsIncrement, bool IsPre,
                                             bool IsStatement) {
    if (auto* Var = dynamic_cast<IVariableASTNode*>(Expression)) {
        auto [InstanceTypeIn, ConversionType] = GenerateVariableCode(Context, Var);

        if (ConversionType == ICT::StacktopId) {
            Context.EmitDuplicate(DT::Int32, 5);
        } else {
            if (Context.CompileContextRef().GameContext().UsingGMLv2() ||
                Context.CompileContextRef().GameContext().Bytecode14OrLower()) {
                Context.EmitDuplicate(DT::Int32, 1);
            } else {
                Context.EmitDuplicate(DT::Int64, 0);
            }
        }

        Bytecode::VariablePatch Patch(Var->VariableName(), InstanceTypeIn, VT::Array,
                                      Var->BuiltinVariable() != nullptr);
        Context.Emit(Op::Push, Patch, DT::Variable);

        if (!IsStatement && !IsPre)
            PrePostDuplicateAndSwap(Context, ConversionType);

        Context.Emit(Op::Push, static_cast<int16_t>(1), DT::Int16);
        Context.Emit(IsIncrement ? Op::Add : Op::Subtract, DT::Int32, DT::Variable);

        if (!IsStatement && IsPre)
            PrePostDuplicateAndSwap(Context, ConversionType);

        if (Context.CompileContextRef().GameContext().UsingGMLv2()) {
            Patch.KeepInstanceType = true;
        }
        Context.Emit(Op::Pop, Patch, DT::Int32, DT::Variable);
    } else if (auto* InnerAcc = As<AccessorNode>(Expression)) {
        InnerAcc->GenerateChainedCode(Context, true);
        AccessorExpression->GenerateCode(Context);
        Context.ConvertDataType(DT::Int32);
        Context.EmitDuplicate(DT::Int32, 4);
        Context.Emit(ExtOp::PushArrayFinal);

        if (!IsStatement && !IsPre)
            MultiArrayPrePostDuplicateAndSwap(Context);

        Context.Emit(Op::Push, static_cast<int16_t>(1), DT::Int16);
        Context.Emit(IsIncrement ? Op::Add : Op::Subtract, DT::Int32, DT::Variable);

        if (!IsStatement && IsPre)
            MultiArrayPrePostDuplicateAndSwap(Context);

        Context.EmitDupSwap(DT::Int32, 4, 5);
        Context.Emit(ExtOp::PopArrayFinal);
    } else {
        throw std::runtime_error("Invalid expression on accessor");
    }
}

std::vector<IASTNode*> AccessorNode::EnumerateChildren() {
    std::vector<IASTNode*> Out;
    Out.reserve(3);
    Out.push_back(Expression);
    Out.push_back(AccessorExpression);
    if (AccessorExpression2 != nullptr)
        Out.push_back(AccessorExpression2);
    return Out;
}

} // namespace Underanalyzer::Compiler::Nodes

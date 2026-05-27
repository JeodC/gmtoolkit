
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/AssignNode.h"

#include "Underanalyzer/Compiler/Bytecode/ArrayOwners.h"
#include "Underanalyzer/Compiler/Bytecode/BranchPatch.h"
#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/Nodes/EmptyNode.h"
#include "Underanalyzer/Compiler/Nodes/SimpleVariableNode.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"
#include "Underanalyzer/IGameContext.h"

#include <stdexcept>

namespace Underanalyzer::Compiler::Nodes {

using Parser::ParseContext;

Lexer::IToken* AssignNode::NearbyToken() const {
    return Destination->NearbyToken();
}

IASTNode* AssignNode::PostProcess(ParseContext& Context) {

    IASTNode* ProcessedDest = Destination->PostProcess(Context);
    auto* AsAssignable = dynamic_cast<IAssignableASTNode*>(ProcessedDest);
    if (AsAssignable == nullptr)
        throw std::runtime_error("Destination no longer assignable");
    Destination = AsAssignable;
    Expression = Expression->PostProcess(Context);

    // Elide pointless "x = x" self-assignments on newer bytecode; bytecode 14 still
    // generates them because the runtime relies on the side effect of the lookup.
    if (KindValue == AssignKind::Normal && !Context.CompileContextRef().GameContext().Bytecode14OrLower()) {
        SimpleVariableNode* D = As<SimpleVariableNode>(Destination);
        SimpleVariableNode* E = As<SimpleVariableNode>(Expression);
        if (D != nullptr && E != nullptr && !D->CollapsedFromDot() && !E->CollapsedFromDot() &&
            D->VariableName() == E->VariableName()) {
            return EmptyNode::Create();
        }
    }
    return this;
}

IASTNode* AssignNode::Duplicate(ParseContext& Context) {
    auto* DupDest = dynamic_cast<IAssignableASTNode*>(Destination->Duplicate(Context));
    if (DupDest == nullptr)
        throw std::runtime_error("Destination no longer assignable");
    return Context.Make<AssignNode>(KindValue, DupDest, Expression->Duplicate(Context));
}

void AssignNode::GenerateCode(Bytecode::BytecodeContext& Context) {
    using Op = IGMInstruction::Opcode;
    using ExtOp = IGMInstruction::ExtendedOpcode;
    using DT = IGMInstruction::DataType;

    bool CanGen = Context.CanGenerateArrayOwners();
    if (CanGen) {
        if (Bytecode::ArrayOwners::ContainsArrayAccessor(Destination) ||
            Bytecode::ArrayOwners::ContainsNewArrayLiteral(Expression) ||
            Bytecode::ArrayOwners::IsArraySetFunctionOrContainsSubLiteral(Destination)) {
            Context.SetCanGenerateArrayOwners(false);
            Bytecode::ArrayOwners::GenerateSetArrayOwner(Context, Destination);
        }
    }

    switch (KindValue) {
        case AssignKind::Normal:
            Expression->GenerateCode(Context);
            Destination->GenerateAssignCode(Context);
            break;
        case AssignKind::CompoundPlus:
            Destination->GenerateCompoundAssignCode(Context, Expression, Op::Add);
            break;
        case AssignKind::CompoundMinus:
            Destination->GenerateCompoundAssignCode(Context, Expression, Op::Subtract);
            break;
        case AssignKind::CompoundTimes:
            Destination->GenerateCompoundAssignCode(Context, Expression, Op::Multiply);
            break;
        case AssignKind::CompoundDivide:
            Destination->GenerateCompoundAssignCode(Context, Expression, Op::Divide);
            break;
        case AssignKind::CompoundMod:
            Destination->GenerateCompoundAssignCode(Context, Expression, Op::GMLModulo);
            break;
        case AssignKind::CompoundBitwiseAnd:
            Destination->GenerateCompoundAssignCode(Context, Expression, Op::And);
            break;
        case AssignKind::CompoundBitwiseOr:
            Destination->GenerateCompoundAssignCode(Context, Expression, Op::Or);
            break;
        case AssignKind::CompoundBitwiseXor:
            Destination->GenerateCompoundAssignCode(Context, Expression, Op::Xor);
            break;
        case AssignKind::CompoundNullishCoalesce: {
            // a ??= b: load a, if it's nullish drop it and assign b to a, otherwise
            // drop the loaded a and skip the assignment entirely.
            Destination->GenerateCode(Context);
            Context.ConvertDataType(DT::Variable);

            Context.Emit(ExtOp::IsNullishValue);
            Bytecode::SingleForwardBranchPatch SkipRightSide(Context, Context.Emit(Op::BranchFalse));

            Context.Emit(Op::PopDelete, DT::Variable);
            Expression->GenerateCode(Context);
            Context.ConvertDataType(DT::Variable);

            Context.PushDataType(DT::Variable);
            Destination->GenerateAssignCode(Context);
            Bytecode::SingleForwardBranchPatch SkipDestinationPop(Context, Context.Emit(Op::Branch));

            SkipRightSide.Patch(Context);
            Context.Emit(Op::PopDelete, DT::Variable);
            SkipDestinationPop.Patch(Context);
            break;
        }
    }

    Context.SetCanGenerateArrayOwners(CanGen);
}

void AssignNode::PerformCompoundOperation(Bytecode::BytecodeContext& Context, IGMInstruction::Opcode OperationOpcode) {
    using Op = IGMInstruction::Opcode;
    using DT = IGMInstruction::DataType;

    DT OperationDataType = Context.PeekDataType();
    // Bitwise compound ops widened to 64-bit in a later runtime version; the older
    // path forces Int32 unless the right side already arrived as Int64.
    if (OperationOpcode == Op::And || OperationOpcode == Op::Or || OperationOpcode == Op::Xor) {
        if (Context.CompileContextRef().GameContext().UsingLongCompoundBitwise()) {
            Context.ConvertDataType(DT::Int64);
            OperationDataType = DT::Int64;
        } else {
            if (OperationDataType == DT::Int64) {
                Context.PopDataType();
            } else {
                Context.ConvertDataType(DT::Int32);
                OperationDataType = DT::Int32;
            }
        }
    } else if (OperationDataType == DT::Boolean) {
        Context.Emit(Op::Convert, DT::Boolean, DT::Int32);
        Context.PopDataType();
        OperationDataType = DT::Int32;
    } else {
        Context.PopDataType();
    }
    Context.Emit(OperationOpcode, OperationDataType, DT::Variable);
}

} // namespace Underanalyzer::Compiler::Nodes

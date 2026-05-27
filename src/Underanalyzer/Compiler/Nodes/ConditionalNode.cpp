
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/ConditionalNode.h"

#include "Underanalyzer/Compiler/Bytecode/BranchPatch.h"
#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"

namespace Underanalyzer::Compiler::Nodes {

IASTNode* ConditionalNode::PostProcess(Parser::ParseContext& Context) {
    Condition = Condition->PostProcess(Context);
    TrueExpression = TrueExpression->PostProcess(Context);
    FalseExpression = FalseExpression->PostProcess(Context);
    return this;
}

IASTNode* ConditionalNode::Duplicate(Parser::ParseContext& Context) {
    return Context.Make<ConditionalNode>(_NearbyToken, Condition->Duplicate(Context),
                                         TrueExpression->Duplicate(Context), FalseExpression->Duplicate(Context));
}

void ConditionalNode::GenerateCode(Bytecode::BytecodeContext& Context) {
    using Op = IGMInstruction::Opcode;
    using DT = IGMInstruction::DataType;

    Condition->GenerateCode(Context);
    Context.ConvertDataType(DT::Boolean);

    Bytecode::SingleForwardBranchPatch ConditionBranch(Context, Context.Emit(Op::BranchFalse));

    TrueExpression->GenerateCode(Context);
    Context.ConvertDataType(DT::Variable);
    Bytecode::SingleForwardBranchPatch SkipElseBranch(Context, Context.Emit(Op::Branch));

    int64_t LastArrayOwnerID = Context.LastArrayOwnerID();

    ConditionBranch.Patch(Context);
    FalseExpression->GenerateCode(Context);
    Context.ConvertDataType(DT::Variable);

    // If only one branch updated the array-owner, we can't know which runs at runtime;
    // invalidate so the next array access re-emits SetArrayOwner.
    if (LastArrayOwnerID != Context.LastArrayOwnerID()) {
        Context.SetLastArrayOwnerID(-1);
    }

    SkipElseBranch.Patch(Context);
    Context.PushDataType(DT::Variable);
}

} // namespace Underanalyzer::Compiler::Nodes

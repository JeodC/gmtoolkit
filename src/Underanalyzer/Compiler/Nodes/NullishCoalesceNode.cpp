
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/NullishCoalesceNode.h"

#include "Underanalyzer/Compiler/Bytecode/BranchPatch.h"
#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"

namespace Underanalyzer::Compiler::Nodes {

IASTNode* NullishCoalesceNode::PostProcess(Parser::ParseContext& Context) {
    Left = Left->PostProcess(Context);
    Right = Right->PostProcess(Context);
    return this;
}

IASTNode* NullishCoalesceNode::Duplicate(Parser::ParseContext& Context) {
    return Context.Make<NullishCoalesceNode>(_NearbyToken, Left->Duplicate(Context), Right->Duplicate(Context));
}

void NullishCoalesceNode::GenerateCode(Bytecode::BytecodeContext& Context) {
    using Op = IGMInstruction::Opcode;
    using ExtOp = IGMInstruction::ExtendedOpcode;
    using DT = IGMInstruction::DataType;

    // Evaluate left; IsNullishValue leaves a bool above it. If false, branch over the
    // right-side path and keep the left value; otherwise drop it and evaluate right.
    Left->GenerateCode(Context);
    Context.ConvertDataType(DT::Variable);

    Context.Emit(ExtOp::IsNullishValue);
    Bytecode::SingleForwardBranchPatch SkipRightSide(Context, Context.Emit(Op::BranchFalse));

    Context.Emit(Op::PopDelete, DT::Variable);
    Right->GenerateCode(Context);
    Context.ConvertDataType(DT::Variable);

    SkipRightSide.Patch(Context);
    Context.PushDataType(DT::Variable);
}

} // namespace Underanalyzer::Compiler::Nodes

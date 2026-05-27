
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/DoUntilLoopNode.h"

#include "Underanalyzer/Compiler/Bytecode/BranchPatch.h"
#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/Bytecode/ControlFlowContext.h"
#include "Underanalyzer/Compiler/Lexer/Token.h"
#include "Underanalyzer/Compiler/Parser/Expressions.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"
#include "Underanalyzer/Compiler/Parser/Statements.h"

namespace Underanalyzer::Compiler::Nodes {

using namespace Lexer;
using Parser::ParseContext;

DoUntilLoopNode* DoUntilLoopNode::Parse(ParseContext& Context) {
    TokenKeyword* TokenDo = Context.EnsureToken(KeywordKind::Do);
    if (TokenDo == nullptr)
        return nullptr;
    IASTNode* BodyIn = Parser::Statements::ParseStatement(Context);
    if (BodyIn == nullptr)
        return nullptr;
    Context.SkipSemicolons();
    TokenKeyword* TokenUntil = Context.EnsureToken(KeywordKind::Until);
    if (TokenUntil == nullptr)
        return nullptr;
    IASTNode* ConditionIn = Parser::Expressions::ParseExpression(Context);
    if (ConditionIn == nullptr)
        return nullptr;
    return Context.Make<DoUntilLoopNode>(TokenDo, BodyIn, ConditionIn);
}

IASTNode* DoUntilLoopNode::PostProcess(ParseContext& Context) {
    Body = Body->PostProcess(Context);
    Condition = Condition->PostProcess(Context);
    return this;
}

IASTNode* DoUntilLoopNode::Duplicate(ParseContext& Context) {
    return Context.Make<DoUntilLoopNode>(_NearbyToken, Body->Duplicate(Context), Condition->Duplicate(Context));
}

void DoUntilLoopNode::GenerateCode(Bytecode::BytecodeContext& Context) {
    using Op = IGMInstruction::Opcode;
    using DT = IGMInstruction::DataType;

    Bytecode::MultiBackwardBranchPatch HeadPatch(Context);
    Bytecode::MultiForwardBranchPatch ConditionPatch;
    Bytecode::MultiForwardBranchPatch TailPatch;

    Bytecode::BasicLoopContext LoopCtx(&TailPatch, &ConditionPatch);
    Context.PushControlFlowContext(&LoopCtx);
    Body->GenerateCode(Context);
    Context.PopControlFlowContext();

    ConditionPatch.Patch(Context);
    Condition->GenerateCode(Context);
    Context.ConvertDataType(DT::Boolean);

    HeadPatch.AddInstruction(Context, Context.Emit(Op::BranchFalse));

    TailPatch.Patch(Context);

    if (Context.CanGenerateArrayOwners() && (TailPatch.Used() || ConditionPatch.Used())) {
        Context.SetLastArrayOwnerID(-1);
    }
}

} // namespace Underanalyzer::Compiler::Nodes

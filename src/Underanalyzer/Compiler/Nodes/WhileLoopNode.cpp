
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/WhileLoopNode.h"

#include "Underanalyzer/Compiler/Bytecode/BranchPatch.h"
#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/Bytecode/ControlFlowContext.h"
#include "Underanalyzer/Compiler/Lexer/Token.h"
#include "Underanalyzer/Compiler/Parser/Expressions.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"
#include "Underanalyzer/Compiler/Parser/Statements.h"
#include "Underanalyzer/Compiler/Parser/TryStatementContext.h"

namespace Underanalyzer::Compiler::Nodes {

using namespace Lexer;
using Parser::ParseContext;

WhileLoopNode* WhileLoopNode::Parse(ParseContext& Context) {
    TokenKeyword* TokenKw = Context.EnsureToken(KeywordKind::While);
    if (TokenKw == nullptr)
        return nullptr;
    IASTNode* ConditionIn = Parser::Expressions::ParseExpression(Context);
    if (ConditionIn == nullptr)
        return nullptr;
    if (Context.IsCurrentToken(KeywordKind::Do))
        Context.SetPosition(Context.Position() + 1);
    IASTNode* BodyIn = Parser::Statements::ParseStatement(Context);
    if (BodyIn == nullptr)
        return nullptr;
    return Context.Make<WhileLoopNode>(TokenKw, ConditionIn, BodyIn);
}

IASTNode* WhileLoopNode::PostProcess(ParseContext& Context) {
    bool PrevProcessingSwitch = Context.ProcessingSwitch();
    Context.SetProcessingSwitch(false);
    if (Parser::TryStatementContext* TryCtx = Context.TryStatementCtx()) {

        TryCtx->SetShouldGenerateBreakContinueCode(false);
    }

    Condition = Condition->PostProcess(Context);
    Body = Body->PostProcess(Context);

    Context.SetProcessingSwitch(PrevProcessingSwitch);
    return this;
}

IASTNode* WhileLoopNode::Duplicate(ParseContext& Context) {
    return Context.Make<WhileLoopNode>(_NearbyToken, Condition->Duplicate(Context), Body->Duplicate(Context));
}

void WhileLoopNode::GenerateCode(Bytecode::BytecodeContext& Context) {
    using Op = IGMInstruction::Opcode;
    using DT = IGMInstruction::DataType;

    Bytecode::MultiBackwardBranchPatchTracked HeadPatch(Context);
    Bytecode::MultiForwardBranchPatch TailPatch;

    Condition->GenerateCode(Context);
    Context.ConvertDataType(DT::Boolean);
    TailPatch.AddInstruction(Context, Context.Emit(Op::BranchFalse));

    int64_t LastArrayOwnerID = Context.LastArrayOwnerID();

    Bytecode::BasicLoopContext LoopCtx(&TailPatch, &HeadPatch);
    Context.PushControlFlowContext(&LoopCtx);
    Body->GenerateCode(Context);
    Context.PopControlFlowContext();

    HeadPatch.AddInstruction(Context, Context.Emit(Op::Branch));
    TailPatch.Patch(Context);

    if (Context.CanGenerateArrayOwners() &&
        (LastArrayOwnerID != Context.LastArrayOwnerID() || TailPatch.NumberUsed() > 1 || HeadPatch.NumberUsed() > 1)) {
        Context.SetLastArrayOwnerID(-1);
    }
}

} // namespace Underanalyzer::Compiler::Nodes

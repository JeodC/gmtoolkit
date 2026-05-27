
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/RepeatLoopNode.h"

#include "Underanalyzer/Compiler/Bytecode/BranchPatch.h"
#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/Bytecode/ControlFlowContext.h"
#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/FunctionScope.h"
#include "Underanalyzer/Compiler/Lexer/Token.h"
#include "Underanalyzer/Compiler/Parser/Expressions.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"
#include "Underanalyzer/Compiler/Parser/Statements.h"
#include "Underanalyzer/Compiler/Parser/TryStatementContext.h"
#include "Underanalyzer/IGameContext.h"

namespace Underanalyzer::Compiler::Nodes {

using namespace Lexer;
using Parser::ParseContext;

RepeatLoopNode* RepeatLoopNode::Parse(ParseContext& Context) {
    TokenKeyword* TokenKw = Context.EnsureToken(KeywordKind::Repeat);
    if (TokenKw == nullptr)
        return nullptr;
    IASTNode* Times = Parser::Expressions::ParseExpression(Context);
    if (Times == nullptr)
        return nullptr;
    IASTNode* BodyIn = Parser::Statements::ParseStatement(Context);
    if (BodyIn == nullptr)
        return nullptr;
    return Context.Make<RepeatLoopNode>(TokenKw, Times, BodyIn);
}

IASTNode* RepeatLoopNode::PostProcess(ParseContext& Context) {
    bool PrevProcessingSwitch = Context.ProcessingSwitch();
    bool PrevProcessingBreakContinueContext = Context.CurrentScope().ProcessingBreakContinueContext();
    bool PrevShouldGenerateBreakContinueCode = true;
    Context.SetProcessingSwitch(false);
    Context.CurrentScope().SetProcessingBreakContinueContext(true);
    if (Parser::TryStatementContext* TryCtx = Context.TryStatementCtx()) {
        PrevShouldGenerateBreakContinueCode = TryCtx->ShouldGenerateBreakContinueCode();
        TryCtx->SetShouldGenerateBreakContinueCode(false);
    }

    TimesToRepeat = TimesToRepeat->PostProcess(Context);
    Body = Body->PostProcess(Context);

    Context.SetProcessingSwitch(PrevProcessingSwitch);
    Context.CurrentScope().SetProcessingBreakContinueContext(PrevProcessingBreakContinueContext);
    if (Parser::TryStatementContext* TryCtx2 = Context.TryStatementCtx()) {
        TryCtx2->SetShouldGenerateBreakContinueCode(PrevShouldGenerateBreakContinueCode);
    }
    return this;
}

IASTNode* RepeatLoopNode::Duplicate(ParseContext& Context) {
    return Context.Make<RepeatLoopNode>(_NearbyToken, TimesToRepeat->Duplicate(Context), Body->Duplicate(Context));
}

void RepeatLoopNode::GenerateCode(Bytecode::BytecodeContext& Context) {
    using Op = IGMInstruction::Opcode;
    using DT = IGMInstruction::DataType;
    using CT = IGMInstruction::ComparisonType;

    TimesToRepeat->GenerateCode(Context);
    Context.ConvertDataType(DT::Int32);

    // Guard against repeat(N <= 0) by checking up front and skipping straight to the tail;
    // the counter sits on the stack and gets decremented after each body run.
    Context.Emit(Op::Duplicate, DT::Int32);
    Context.Emit(Op::Push, static_cast<int32_t>(0), DT::Int32);
    Context.Emit(Op::Compare, CT::LesserEqualThan, DT::Int32, DT::Int32);

    Bytecode::MultiForwardBranchPatch TailPatch;
    Bytecode::MultiForwardBranchPatch DecrementorPatch;

    TailPatch.AddInstruction(Context, Context.Emit(Op::BranchTrue));

    int64_t LastArrayOwnerID = Context.LastArrayOwnerID();

    Bytecode::MultiBackwardBranchPatch BodyPatch(Context);
    Bytecode::RepeatLoopContext LoopCtx(&TailPatch, &DecrementorPatch);
    Context.PushControlFlowContext(&LoopCtx);
    Body->GenerateCode(Context);
    Context.PopControlFlowContext();

    if (Context.CanGenerateArrayOwners() &&
        (LastArrayOwnerID != Context.LastArrayOwnerID() || TailPatch.NumberUsed() > 1 || DecrementorPatch.Used())) {
        Context.SetLastArrayOwnerID(-1);
    }

    // Decrement-and-test. Newer runtimes use a 32-bit Push 1 and explicit Int32->Bool
    // conversion before BranchTrue; older ones use a cheaper 16-bit immediate.
    DecrementorPatch.Patch(Context);
    if (Context.CompileContextRef().GameContext().UsingExtraRepeatInstruction()) {
        Context.Emit(Op::Push, static_cast<int32_t>(1), DT::Int32);
    } else {
        Context.Emit(Op::PushImmediate, static_cast<int16_t>(1), DT::Int16);
    }
    Context.Emit(Op::Subtract, DT::Int32, DT::Int32);
    Context.Emit(Op::Duplicate, DT::Int32);
    if (Context.CompileContextRef().GameContext().UsingExtraRepeatInstruction()) {
        Context.Emit(Op::Convert, DT::Int32, DT::Boolean);
    }
    BodyPatch.AddInstruction(Context, Context.Emit(Op::BranchTrue));

    TailPatch.Patch(Context);
    Context.Emit(Op::PopDelete, DT::Int32);
}

} // namespace Underanalyzer::Compiler::Nodes

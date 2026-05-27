
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/WithLoopNode.h"

#include "Underanalyzer/Compiler/Bytecode/BranchPatch.h"
#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/Bytecode/ControlFlowContext.h"
#include "Underanalyzer/Compiler/FunctionScope.h"
#include "Underanalyzer/Compiler/Lexer/Token.h"
#include "Underanalyzer/Compiler/Parser/Expressions.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"
#include "Underanalyzer/Compiler/Parser/Statements.h"
#include "Underanalyzer/Compiler/Parser/TryStatementContext.h"

namespace Underanalyzer::Compiler::Nodes {

using namespace Lexer;
using Parser::ParseContext;

WithLoopNode* WithLoopNode::Parse(ParseContext& Context) {
    TokenKeyword* TokenKw = Context.EnsureToken(KeywordKind::With);
    if (TokenKw == nullptr)
        return nullptr;
    IASTNode* Expr = Parser::Expressions::ParseExpression(Context);
    if (Expr == nullptr)
        return nullptr;
    if (Context.IsCurrentToken(KeywordKind::Do))
        Context.SetPosition(Context.Position() + 1);
    IASTNode* BodyIn = Parser::Statements::ParseStatement(Context);
    if (BodyIn == nullptr)
        return nullptr;
    return Context.Make<WithLoopNode>(TokenKw, Expr, BodyIn);
}

IASTNode* WithLoopNode::PostProcess(ParseContext& Context) {
    bool PrevProcessingSwitch = Context.ProcessingSwitch();
    bool PrevProcessingBreakContinueContext = Context.CurrentScope().ProcessingBreakContinueContext();
    bool PrevShouldGenerateBreakContinueCode = true;
    Context.SetProcessingSwitch(false);
    Context.CurrentScope().SetProcessingBreakContinueContext(true);
    if (Parser::TryStatementContext* TryCtx = Context.TryStatementCtx()) {
        PrevShouldGenerateBreakContinueCode = TryCtx->ShouldGenerateBreakContinueCode();
        TryCtx->SetShouldGenerateBreakContinueCode(false);
    }

    Expression = Expression->PostProcess(Context);
    Body = Body->PostProcess(Context);

    Context.SetProcessingSwitch(PrevProcessingSwitch);
    Context.CurrentScope().SetProcessingBreakContinueContext(PrevProcessingBreakContinueContext);
    if (Parser::TryStatementContext* TryCtx2 = Context.TryStatementCtx()) {
        TryCtx2->SetShouldGenerateBreakContinueCode(PrevShouldGenerateBreakContinueCode);
    }
    return this;
}

IASTNode* WithLoopNode::Duplicate(ParseContext& Context) {
    return Context.Make<WithLoopNode>(_NearbyToken, Expression->Duplicate(Context), Body->Duplicate(Context));
}

void WithLoopNode::GenerateCode(Bytecode::BytecodeContext& Context) {
    using Op = IGMInstruction::Opcode;

    // PushWithContext both saves the current self and iterates the matching instances;
    // each iteration's exit point branches to a PopWithContext at the tail of the loop.
    Expression->GenerateCode(Context);
    Context.ConvertToInstanceId();

    Bytecode::MultiForwardBranchPatch PopWithContextPatch;
    PopWithContextPatch.AddInstruction(Context, Context.Emit(Op::PushWithContext));

    Bytecode::MultiBackwardBranchPatch PushWithContextPatch(Context);
    Bytecode::MultiForwardBranchPatch BreakPatch;

    int64_t LastArrayOwnerID = Context.LastArrayOwnerID();

    Bytecode::WithLoopContext LoopCtx(&BreakPatch, &PopWithContextPatch);
    Context.PushControlFlowContext(&LoopCtx);
    Body->GenerateCode(Context);
    Context.PopControlFlowContext();

    if (Context.CanGenerateArrayOwners() &&
        (LastArrayOwnerID != Context.LastArrayOwnerID() || BreakPatch.Used() || PopWithContextPatch.NumberUsed() > 1)) {
        Context.SetLastArrayOwnerID(-1);
    }

    PopWithContextPatch.Patch(Context);
    PushWithContextPatch.AddInstruction(Context, Context.Emit(Op::PopWithContext));

    // 'break' jumps here so the with-context can be popped cleanly before continuing
    // past the loop; the surrounding jump skips this fixup on the normal exit path.
    if (BreakPatch.Used()) {
        Bytecode::SingleForwardBranchPatch SkipBreakBlock(Context, Context.Emit(Op::Branch));
        BreakPatch.Patch(Context);
        Context.EmitPopWithExit();
        SkipBreakBlock.Patch(Context);
    }
}

} // namespace Underanalyzer::Compiler::Nodes

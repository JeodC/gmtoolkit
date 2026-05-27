
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/ForLoopNode.h"

#include "Underanalyzer/Compiler/Bytecode/BranchPatch.h"
#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/Bytecode/ControlFlowContext.h"
#include "Underanalyzer/Compiler/FunctionScope.h"
#include "Underanalyzer/Compiler/Lexer/Token.h"
#include "Underanalyzer/Compiler/Nodes/EmptyNode.h"
#include "Underanalyzer/Compiler/Nodes/Int64Node.h"
#include "Underanalyzer/Compiler/Parser/Expressions.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"
#include "Underanalyzer/Compiler/Parser/Statements.h"
#include "Underanalyzer/Compiler/Parser/TryStatementContext.h"

namespace Underanalyzer::Compiler::Nodes {

using namespace Lexer;
using Parser::ParseContext;

ForLoopNode* ForLoopNode::Parse(ParseContext& Context) {
    TokenKeyword* TokenKw = Context.EnsureToken(KeywordKind::For);
    if (TokenKw == nullptr)
        return nullptr;
    Context.EnsureToken(SeparatorKind::GroupOpen);

    IASTNode* InitializerIn;
    if (Context.IsCurrentToken(SeparatorKind::Semicolon)) {
        InitializerIn = EmptyNode::Create(Context, Context.Tokens()[Context.Position()]);
        Context.SetPosition(Context.Position() + 1);
    } else {
        IASTNode* Stmt = Parser::Statements::ParseStatement(Context);
        if (Stmt == nullptr)
            return nullptr;
        InitializerIn = Stmt;
        if (Context.IsCurrentToken(SeparatorKind::Semicolon))
            Context.SetPosition(Context.Position() + 1);
    }

    IASTNode* ConditionIn;
    // Missing condition (for(;;)) becomes a literal 1 so the loop runs forever.
    if (Context.IsCurrentToken(SeparatorKind::Semicolon)) {
        ConditionIn = Context.Make<Int64Node>(static_cast<int64_t>(1), Context.Tokens()[Context.Position()]);
        Context.SetPosition(Context.Position() + 1);
    } else {
        IASTNode* Expr = Parser::Expressions::ParseExpression(Context);
        if (Expr == nullptr)
            return nullptr;
        ConditionIn = Expr;
        if (Context.IsCurrentToken(SeparatorKind::Semicolon))
            Context.SetPosition(Context.Position() + 1);
    }

    IASTNode* IncrementorIn;
    if (Context.IsCurrentToken(SeparatorKind::GroupClose)) {
        IncrementorIn = EmptyNode::Create(Context, Context.Tokens()[Context.Position()]);
    } else {
        IASTNode* Stmt = Parser::Statements::ParseStatement(Context);
        if (Stmt == nullptr)
            return nullptr;
        IncrementorIn = Stmt;
        Context.SkipSemicolons();
    }

    Context.EnsureToken(SeparatorKind::GroupClose);

    IASTNode* BodyIn = Parser::Statements::ParseStatement(Context);
    if (BodyIn == nullptr)
        return nullptr;

    return Context.Make<ForLoopNode>(TokenKw, InitializerIn, ConditionIn, IncrementorIn, BodyIn);
}

IASTNode* ForLoopNode::PostProcess(ParseContext& Context) {
    bool PrevProcessingSwitch = Context.ProcessingSwitch();
    bool PrevProcessingBreakContinueContext = Context.CurrentScope().ProcessingBreakContinueContext();
    bool PrevShouldGenerateBreakContinueCode = true;
    Context.SetProcessingSwitch(false);
    Context.CurrentScope().SetProcessingBreakContinueContext(true);
    if (Parser::TryStatementContext* TryCtx = Context.TryStatementCtx()) {
        PrevShouldGenerateBreakContinueCode = TryCtx->ShouldGenerateBreakContinueCode();
        TryCtx->SetShouldGenerateBreakContinueCode(false);
    }

    Initializer = Initializer->PostProcess(Context);
    Condition = Condition->PostProcess(Context);
    Incrementor = Incrementor->PostProcess(Context);
    Body = Body->PostProcess(Context);

    Context.SetProcessingSwitch(PrevProcessingSwitch);
    Context.CurrentScope().SetProcessingBreakContinueContext(PrevProcessingBreakContinueContext);
    if (Parser::TryStatementContext* TryCtx2 = Context.TryStatementCtx()) {
        TryCtx2->SetShouldGenerateBreakContinueCode(PrevShouldGenerateBreakContinueCode);
    }
    return this;
}

IASTNode* ForLoopNode::Duplicate(ParseContext& Context) {
    return Context.Make<ForLoopNode>(_NearbyToken, Initializer->Duplicate(Context), Condition->Duplicate(Context),
                                     Incrementor->Duplicate(Context), Body->Duplicate(Context));
}

void ForLoopNode::GenerateCode(Bytecode::BytecodeContext& Context) {
    using Op = IGMInstruction::Opcode;
    using DT = IGMInstruction::DataType;

    Initializer->GenerateCode(Context);

    Bytecode::MultiBackwardBranchPatch HeadPatch(Context);
    Bytecode::MultiForwardBranchPatch TailPatch;
    Bytecode::MultiForwardBranchPatch IncrementorPatch;

    Condition->GenerateCode(Context);
    Context.ConvertDataType(DT::Boolean);

    TailPatch.AddInstruction(Context, Context.Emit(Op::BranchFalse));

    int64_t LastArrayOwnerID = Context.LastArrayOwnerID();

    Bytecode::BasicLoopContext LoopCtx(&TailPatch, &IncrementorPatch);
    Context.PushControlFlowContext(&LoopCtx);
    Body->GenerateCode(Context);

    // 'continue' inside the body jumps here so the incrementor still runs; the
    // incrementor itself isn't a valid continue target, so disable that for its codegen.
    IncrementorPatch.Patch(Context);
    LoopCtx.SetCanContinueBeUsed(false);
    Incrementor->GenerateCode(Context);
    Context.PopControlFlowContext();

    HeadPatch.AddInstruction(Context, Context.Emit(Op::Branch));
    TailPatch.Patch(Context);

    if (Context.CanGenerateArrayOwners() &&
        (LastArrayOwnerID != Context.LastArrayOwnerID() || TailPatch.NumberUsed() > 1 || IncrementorPatch.Used())) {
        Context.SetLastArrayOwnerID(-1);
    }
}

} // namespace Underanalyzer::Compiler::Nodes

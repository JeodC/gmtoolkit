
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/TryCatchNode.h"

#include "Underanalyzer/Compiler/Bytecode/BranchPatch.h"
#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/Bytecode/InstructionPatch.h"
#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/FunctionScope.h"
#include "Underanalyzer/Compiler/ICodeBuilder.h"
#include "Underanalyzer/Compiler/Lexer/Token.h"
#include "Underanalyzer/Compiler/Nodes/AssignNode.h"
#include "Underanalyzer/Compiler/Nodes/BlockNode.h"
#include "Underanalyzer/Compiler/Nodes/BooleanNode.h"
#include "Underanalyzer/Compiler/Nodes/BreakNode.h"
#include "Underanalyzer/Compiler/Nodes/ContinueNode.h"
#include "Underanalyzer/Compiler/Nodes/IfNode.h"
#include "Underanalyzer/Compiler/Nodes/NumberNode.h"
#include "Underanalyzer/Compiler/Nodes/SimpleVariableNode.h"
#include "Underanalyzer/Compiler/Nodes/WhileLoopNode.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"
#include "Underanalyzer/Compiler/Parser/Statements.h"
#include "Underanalyzer/Compiler/Parser/TryStatementContext.h"
#include "Underanalyzer/IGameContext.h"
#include "Underanalyzer/VMConstants.h"

namespace Underanalyzer::Compiler::Nodes {

using namespace Lexer;
using Parser::ParseContext;
using Op = IGMInstruction::Opcode;
using DT = IGMInstruction::DataType;
using IT = IGMInstruction::InstanceType;

IASTNode* TryCatchNode::Parse(ParseContext& Context) {
    TokenKeyword* TokenKw = Context.EnsureToken(KeywordKind::Try);
    if (TokenKw == nullptr)
        return nullptr;

    int PreviousEscapeCount = Context.ExitReturnBreakContinueCount();
    IASTNode* TryIn = Parser::Statements::ParseStatement(Context);
    if (TryIn == nullptr)
        return nullptr;
    bool TryEscape = (Context.ExitReturnBreakContinueCount() != PreviousEscapeCount);

    IASTNode* CatchIn = nullptr;
    std::optional<std::string> CatchVariableName;
    bool CatchEscape = false;
    if (Context.IsCurrentToken(KeywordKind::Catch)) {
        Context.SetPosition(Context.Position() + 1);

        Context.EnsureToken(SeparatorKind::GroupOpen);
        if (!Context.EndOfCode()) {
            if (TokenVariable* Var = As<TokenVariable>(Context.Tokens()[Context.Position()])) {
                Context.SetPosition(Context.Position() + 1);
                CatchVariableName = Var->Text;
                Context.CurrentScope().DeclareLocal(Var->Text);
            }
        }
        Context.EnsureToken(SeparatorKind::GroupClose);

        PreviousEscapeCount = Context.ExitReturnBreakContinueCount() + Context.ThrowCount();
        CatchIn = Parser::Statements::ParseStatement(Context);
        CatchEscape = (Context.ExitReturnBreakContinueCount() + Context.ThrowCount() != PreviousEscapeCount);
    }

    IASTNode* FinallyIn = nullptr;
    if (Context.IsCurrentToken(KeywordKind::Finally)) {
        Context.SetPosition(Context.Position() + 1);
        FinallyIn = Parser::Statements::ParseStatement(Context);
    }

    // A 'try' with neither catch nor finally degenerates to the try body itself.
    if (CatchIn == nullptr && FinallyIn == nullptr) {

        return TryIn;
    }
    return Context.Make<TryCatchNode>(TokenKw, TryIn, CatchIn, std::move(CatchVariableName), FinallyIn, TryEscape,
                                      CatchEscape);
}

// Wraps a try/catch body in a "while(true) { if continue_flag break; body; break; }" loop
// so a break/continue inside the body can be modeled as setting a sentinel local and
// breaking out, which the outer machinery then translates back into real control flow.
WhileLoopNode* TryCatchNode::GenerateBlockLoop(ParseContext& Context, Parser::TryStatementContext& TryCtx,
                                               IASTNode* Block) {
    BlockNode* InnerBlock = BlockNode::CreateEmpty(Context, _NearbyToken, 3);

    Context.CurrentScope().DeclareLocal(TryCtx.ContinueVariableName());
    InnerBlock->Children().push_back(Context.Make<IfNode>(
        _NearbyToken,
        Context.Make<SimpleVariableNode>(TryCtx.ContinueVariableName(), (IBuiltinVariable*)nullptr, IT::Local),
        (IASTNode*)Context.Make<BreakNode>(_NearbyToken), (IASTNode*)nullptr));

    InnerBlock->Children().push_back(Block);

    InnerBlock->Children().push_back(Context.Make<BreakNode>(_NearbyToken));

    return Context.Make<WhileLoopNode>(_NearbyToken, (IASTNode*)Context.Make<BooleanNode>(true, _NearbyToken),
                                       (IASTNode*)InnerBlock);
}

IASTNode* TryCatchNode::PostProcess(ParseContext& Context) {
    if (Finally != nullptr) {

        bool PrevProcessingFinally = Context.ProcessingFinally();
        Context.SetProcessingFinally(true);
        Finally = Finally->PostProcess(Context);
        Context.SetProcessingFinally(PrevProcessingFinally);

        Context.CurrentScope().TryFinallyNodes().push_back(Finally);
    }

    int UniqueIndex = Context.CompileContextRef().GameContext().CodeBuilder().GenerateTryVariableID(
        Context.TryStatementProcessIndex());
    Context.SetTryStatementProcessIndex(Context.TryStatementProcessIndex() + 1);
    std::string BreakName = std::string(VMConstants::TryBreakVariable) + std::to_string(UniqueIndex);
    std::string ContinueName = std::string(VMConstants::TryContinueVariable) + std::to_string(UniqueIndex);

    Parser::TryStatementContext* PrevTryCtx = Context.TryStatementCtx();
    Parser::TryStatementContext NewTryCtx(BreakName, ContinueName, Finally != nullptr);
    Context.SetTryStatementCtx(&NewTryCtx);
    bool BreakContinueUsedAnywhere = false;

    NewTryCtx.SetThrowFinallyGeneration(TryEscape || Catch == nullptr);

    NewTryCtx.SetShouldGenerateBreakContinueCode(true);
    NewTryCtx.SetHasBreakContinueVariable(false);
    Try = Try->PostProcess(Context);
    if (NewTryCtx.HasBreakContinueVariable()) {
        Try = GenerateBlockLoop(Context, NewTryCtx, Try);
        BreakContinueUsedAnywhere = true;
    }

    if (Catch != nullptr) {
        NewTryCtx.SetThrowFinallyGeneration(CatchEscape);
        NewTryCtx.SetShouldGenerateBreakContinueCode(true);
        NewTryCtx.SetHasBreakContinueVariable(false);
        Catch = Catch->PostProcess(Context);
        if (NewTryCtx.HasBreakContinueVariable()) {
            Catch = GenerateBlockLoop(Context, NewTryCtx, Catch);
            BreakContinueUsedAnywhere = true;
        }
    }

    Context.SetTryStatementCtx(PrevTryCtx);

    if (Finally != nullptr) {
        Context.CurrentScope().TryFinallyNodes().pop_back();
    }

    if (BreakContinueUsedAnywhere) {
        bool InsideBreakContinueContext = Context.CurrentScope().ProcessingBreakContinueContext();
        BlockNode* NewBlock = BlockNode::CreateEmpty(Context, _NearbyToken, InsideBreakContinueContext ? 5 : 3);

        Context.CurrentScope().DeclareLocal(BreakName);
        Context.CurrentScope().DeclareLocal(ContinueName);
        NewBlock->Children().push_back(Context.Make<AssignNode>(
            AssignNode::AssignKind::Normal,
            (IAssignableASTNode*)Context.Make<SimpleVariableNode>(BreakName, (IBuiltinVariable*)nullptr, IT::Local),
            (IASTNode*)Context.Make<NumberNode>(0.0, _NearbyToken)));
        NewBlock->Children().push_back(Context.Make<AssignNode>(
            AssignNode::AssignKind::Normal,
            (IAssignableASTNode*)Context.Make<SimpleVariableNode>(ContinueName, (IBuiltinVariable*)nullptr, IT::Local),
            (IASTNode*)Context.Make<NumberNode>(0.0, _NearbyToken)));
        NewBlock->Children().push_back(this);

        if (InsideBreakContinueContext) {
            NewBlock->Children().push_back(Context.Make<IfNode>(
                _NearbyToken,
                (IASTNode*)Context.Make<SimpleVariableNode>(ContinueName, (IBuiltinVariable*)nullptr, IT::Local),
                (IASTNode*)Context.Make<ContinueNode>(_NearbyToken), (IASTNode*)nullptr));
            NewBlock->Children().push_back(Context.Make<IfNode>(
                _NearbyToken,
                (IASTNode*)Context.Make<SimpleVariableNode>(BreakName, (IBuiltinVariable*)nullptr, IT::Local),
                (IASTNode*)Context.Make<BreakNode>(_NearbyToken), (IASTNode*)nullptr));
        }
        return NewBlock;
    }
    return this;
}

IASTNode* TryCatchNode::Duplicate(ParseContext& Context) {
    return Context.Make<TryCatchNode>(_NearbyToken, Try->Duplicate(Context),
                                      Catch ? Catch->Duplicate(Context) : nullptr, CatchVariableName,
                                      Finally ? Finally->Duplicate(Context) : nullptr, TryEscape, CatchEscape);
}

void TryCatchNode::GenerateCode(Bytecode::BytecodeContext& Context) {
    using namespace Bytecode;

    // The catch/finally target addresses aren't known yet; push placeholder -1 ints
    // and patch them with PatchPush once those code regions exist.
    IGMInstruction* FinallyInstr = Context.Emit(Op::Push, static_cast<int32_t>(-1), DT::Int32);
    Context.Emit(Op::Convert, DT::Int32, DT::Variable);
    IGMInstruction* CatchInstr = Context.Emit(Op::Push, static_cast<int32_t>(-1), DT::Int32);
    Context.Emit(Op::Convert, DT::Int32, DT::Variable);
    Context.EmitCall(FunctionPatch::FromBuiltin(Context, std::string(VMConstants::TryHookFunction)), 2);
    Context.Emit(Op::PopDelete, DT::Variable);

    const std::string* PrevFunctionCallBeforeExit = Context.FunctionCallBeforeExit();
    std::optional<std::string> PrevFCBE =
        PrevFunctionCallBeforeExit ? std::optional<std::string>(*PrevFunctionCallBeforeExit) : std::nullopt;
    Context.SetFunctionCallBeforeExit(std::string(VMConstants::TryUnhookFunction));
    Try->GenerateCode(Context);
    Context.SetFunctionCallBeforeExit(PrevFCBE);

    if (Context.CanGenerateArrayOwners())
        Context.SetLastArrayOwnerID(-1);

    if (Catch != nullptr) {
        SingleForwardBranchPatch SkipCatch(Context, Context.Emit(Op::Branch));

        Context.PatchPush(CatchInstr, Context.Position());

        Context.Emit(Op::Pop, VariablePatch(*CatchVariableName, IT::Local), DT::Variable, DT::Variable);
        Context.EmitCall(FunctionPatch::FromBuiltin(Context, std::string(VMConstants::TryUnhookFunction)), 0);
        Context.Emit(Op::PopDelete, DT::Variable);

        Context.SetFunctionCallBeforeExit(std::string(VMConstants::FinishCatchFunction));
        Catch->GenerateCode(Context);
        Context.SetFunctionCallBeforeExit(PrevFCBE);

        Context.EmitCall(FunctionPatch::FromBuiltin(Context, std::string(VMConstants::FinishCatchFunction)), 0);
        Context.Emit(Op::PopDelete, DT::Variable);
        SingleForwardBranchPatch SkipRegularTryUnhook(Context, Context.Emit(Op::Branch));

        Context.PatchPush(FinallyInstr, Context.Position());

        SkipCatch.Patch(Context);
        Context.EmitCall(FunctionPatch::FromBuiltin(Context, std::string(VMConstants::TryUnhookFunction)), 0);
        Context.Emit(Op::PopDelete, DT::Variable);
        SkipRegularTryUnhook.Patch(Context);

        if (Context.CanGenerateArrayOwners())
            Context.SetLastArrayOwnerID(-1);
    } else {
        Context.PatchPush(FinallyInstr, Context.Position());
        Context.EmitCall(FunctionPatch::FromBuiltin(Context, std::string(VMConstants::TryUnhookFunction)), 0);
        Context.Emit(Op::PopDelete, DT::Variable);
    }

    if (Finally != nullptr) {
        Finally->GenerateCode(Context);
        Context.EmitCall(FunctionPatch::FromBuiltin(Context, std::string(VMConstants::FinishFinallyFunction)), 0);
        Context.Emit(Op::PopDelete, DT::Variable);

        SingleForwardBranchPatch Useless(Context, Context.Emit(Op::Branch));
        Useless.Patch(Context);

        if (Context.CanGenerateArrayOwners())
            Context.SetLastArrayOwnerID(-1);
    }
}

std::vector<IASTNode*> TryCatchNode::EnumerateChildren() {
    std::vector<IASTNode*> Out;
    Out.reserve(3);
    Out.push_back(Try);
    if (Catch != nullptr)
        Out.push_back(Catch);
    if (Finally != nullptr)
        Out.push_back(Finally);
    return Out;
}

} // namespace Underanalyzer::Compiler::Nodes

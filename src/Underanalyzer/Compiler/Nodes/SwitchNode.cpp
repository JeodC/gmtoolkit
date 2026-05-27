
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/SwitchNode.h"

#include "Underanalyzer/Compiler/Bytecode/BranchPatch.h"
#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/Bytecode/ControlFlowContext.h"
#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/Lexer/Token.h"
#include "Underanalyzer/Compiler/Nodes/SwitchCaseNode.h"
#include "Underanalyzer/Compiler/Parser/Expressions.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"
#include "Underanalyzer/Compiler/Parser/Statements.h"

namespace Underanalyzer::Compiler::Nodes {

using namespace Lexer;
using Parser::ParseContext;

SwitchNode* SwitchNode::Parse(ParseContext& Context) {
    TokenKeyword* TokenKw = Context.EnsureToken(KeywordKind::Switch);
    if (TokenKw == nullptr)
        return nullptr;
    IASTNode* Expr = Parser::Expressions::ParseExpression(Context);
    if (Expr == nullptr)
        return nullptr;

    std::vector<IASTNode*> Children;
    Children.reserve(32);
    Context.EnsureToken(SeparatorKind::BlockOpen, KeywordKind::Begin);
    Context.SkipSemicolons();
    while (!Context.EndOfCode() && !Context.IsCurrentToken(SeparatorKind::BlockClose, KeywordKind::End)) {
        IToken* CurrToken = Context.Tokens()[Context.Position()];
        if (TokenKeyword* TokenCase = As<TokenKeyword>(CurrToken);
            TokenCase != nullptr && TokenCase->Kind == KeywordKind::Case) {
            Context.SetPosition(Context.Position() + 1);
            if (IASTNode* CaseExpr = Parser::Expressions::ParseExpression(Context)) {
                SwitchCaseNode* CaseNode = Context.Make<SwitchCaseNode>(TokenCase, CaseExpr);
                Context.EnsureToken(SeparatorKind::Colon);
                Children.push_back(CaseNode);
            }
        } else if (TokenKeyword* TokenDefault = As<TokenKeyword>(CurrToken);
                   TokenDefault != nullptr && TokenDefault->Kind == KeywordKind::Default) {
            Context.SetPosition(Context.Position() + 1);
            SwitchCaseNode* DefaultNode = Context.Make<SwitchCaseNode>(TokenDefault, (IASTNode*)nullptr);
            Context.EnsureToken(SeparatorKind::Colon);
            Children.push_back(DefaultNode);
        } else if (IASTNode* Statement = Parser::Statements::ParseStatement(Context)) {
            Children.push_back(Statement);
        } else {
            break;
        }
        Context.SkipSemicolons();
    }
    Context.EnsureToken(SeparatorKind::BlockClose, KeywordKind::End);

    return Context.Make<SwitchNode>(TokenKw, Expr, std::move(Children));
}

IASTNode* SwitchNode::PostProcess(ParseContext& Context) {
    bool PrevProcessingSwitch = Context.ProcessingSwitch();
    Context.SetProcessingSwitch(true);

    Expression = Expression->PostProcess(Context);
    if (Children.empty()) {
        Context.CompileContextRef().PushError("Switch statement is empty", _NearbyToken);
    } else {
        if (As<SwitchCaseNode>(Children[0]) == nullptr) {
            Context.CompileContextRef().PushError("Switch statement body must begin with \"case\" or \"default\"",
                                                  _NearbyToken);
        }
        for (auto& C : Children)
            C = C->PostProcess(Context);
    }

    Context.SetProcessingSwitch(PrevProcessingSwitch);
    return this;
}

IASTNode* SwitchNode::Duplicate(ParseContext& Context) {
    std::vector<IASTNode*> NewChildren(Children);
    for (auto& C : NewChildren)
        C = C->Duplicate(Context);
    return Context.Make<SwitchNode>(_NearbyToken, Expression->Duplicate(Context), std::move(NewChildren));
}

void SwitchNode::GenerateCode(Bytecode::BytecodeContext& Context) {
    using Op = IGMInstruction::Opcode;
    using DT = IGMInstruction::DataType;
    using CT = IGMInstruction::ComparisonType;

    Expression->GenerateCode(Context);
    DT ExpressionType = Context.PopDataType();

    int64_t LastArrayOwnerID = Context.LastArrayOwnerID();
    bool ArrayOwnerChanged = false;

    Bytecode::MultiForwardBranchPatch TailPatch;
    Bytecode::MultiForwardBranchPatch ContinuePatch;
    Bytecode::MultiForwardBranchPatch DefaultPatch;
    bool DefaultCaseExists = false;

    struct SwitchCase {
        Bytecode::MultiForwardBranchPatch* Branch;
        int ChildIndex;
    };
    std::vector<SwitchCase> Cases;
    Cases.reserve(16);

    Compiler::NodeArena& Arena = Context.Arena();

    // First pass: emit one Compare + BranchTrue per case at the top, body code comes later.
    // The switch value is duplicated each time so the original remains on the stack.
    for (int i = 0; i < static_cast<int>(Children.size()); i++) {
        if (SwitchCaseNode* CaseNode = As<SwitchCaseNode>(Children[i])) {
            if (CaseNode->Expression != nullptr) {
                Context.SetLastArrayOwnerID(LastArrayOwnerID);

                Context.Emit(Op::Duplicate, ExpressionType);
                CaseNode->Expression->GenerateCode(Context);
                Context.Emit(Op::Compare, CT::EqualTo, Context.PopDataType(), ExpressionType);

                auto* CasePatch = Arena.New<Bytecode::MultiForwardBranchPatch>();
                CasePatch->AddInstruction(Context, Context.Emit(Op::BranchTrue));
                Cases.push_back({ CasePatch, i });

                ArrayOwnerChanged |= (Context.LastArrayOwnerID() != LastArrayOwnerID);
            } else {
                DefaultCaseExists = true;
                Cases.push_back({ &DefaultPatch, i });
            }
        }
    }

    if (DefaultCaseExists) {
        DefaultPatch.AddInstruction(Context, Context.Emit(Op::Branch));
    }
    TailPatch.AddInstruction(Context, Context.Emit(Op::Branch));

    Bytecode::SwitchContext SwitchCtx(ExpressionType, &TailPatch, &ContinuePatch);
    Context.PushControlFlowContext(&SwitchCtx);
    for (int i = 0; i < static_cast<int>(Cases.size()); i++) {
        SwitchCase& Current = Cases[i];
        int StartIndex = Current.ChildIndex + 1;
        int EndIndexExclusive =
            (i + 1 < static_cast<int>(Cases.size())) ? Cases[i + 1].ChildIndex : static_cast<int>(Children.size());

        Context.SetLastArrayOwnerID(LastArrayOwnerID);
        Current.Branch->Patch(Context);
        for (int j = StartIndex; j < EndIndexExclusive; j++) {
            Children[j]->GenerateCode(Context);
        }
        ArrayOwnerChanged |= (Context.LastArrayOwnerID() != LastArrayOwnerID);
    }
    Context.PopControlFlowContext();

    // 'continue' in a switch inside a loop has to pop the switched value before
    // jumping to the surrounding loop's continue target.
    if (ContinuePatch.Used()) {
        TailPatch.AddInstruction(Context, Context.Emit(Op::Branch));
        ContinuePatch.Patch(Context);
        Context.Emit(Op::PopDelete, ExpressionType);
        Context.GetTopControlFlowContext()->UseContinue(Context, Context.Emit(Op::Branch));
    }

    TailPatch.Patch(Context);
    Context.Emit(Op::PopDelete, ExpressionType);

    if (ArrayOwnerChanged) {
        Context.SetLastArrayOwnerID(-1);
    }
}

std::vector<IASTNode*> SwitchNode::EnumerateChildren() {
    std::vector<IASTNode*> Out;
    Out.reserve(1 + Children.size());
    Out.push_back(Expression);
    for (IASTNode* C : Children)
        Out.push_back(C);
    return Out;
}

} // namespace Underanalyzer::Compiler::Nodes

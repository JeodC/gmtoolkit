
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/IfNode.h"

#include "Underanalyzer/Compiler/Bytecode/BranchPatch.h"
#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/Lexer/Token.h"
#include "Underanalyzer/Compiler/Nodes/BooleanNode.h"
#include "Underanalyzer/Compiler/Nodes/EmptyNode.h"
#include "Underanalyzer/Compiler/Nodes/Int64Node.h"
#include "Underanalyzer/Compiler/Nodes/NumberNode.h"
#include "Underanalyzer/Compiler/Parser/Expressions.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"
#include "Underanalyzer/Compiler/Parser/Statements.h"

namespace Underanalyzer::Compiler::Nodes {

using namespace Lexer;
using Parser::ParseContext;

IfNode* IfNode::Parse(ParseContext& Context) {
    TokenKeyword* TokenKw = Context.EnsureToken(KeywordKind::If);
    if (TokenKw == nullptr)
        return nullptr;
    IASTNode* Cond = Parser::Expressions::ParseExpression(Context);
    if (Cond == nullptr)
        return nullptr;
    if (Context.IsCurrentToken(KeywordKind::Then))
        Context.SetPosition(Context.Position() + 1);
    IASTNode* TrueStmt = Parser::Statements::ParseStatement(Context);
    if (TrueStmt == nullptr)
        return nullptr;
    IASTNode* FalseStmt = nullptr;
    Context.SkipSemicolons();
    if (Context.IsCurrentToken(KeywordKind::Else)) {
        Context.SetPosition(Context.Position() + 1);
        FalseStmt = Parser::Statements::ParseStatement(Context);
        if (FalseStmt == nullptr)
            return nullptr;
    }
    return Context.Make<IfNode>(TokenKw, Cond, TrueStmt, FalseStmt);
}

// Dead-branch elimination: a constant-truthy condition replaces the if with the true
// branch; a constant-falsy one drops to the else (or to EmptyNode if there is none).
IASTNode* IfNode::PostProcess(ParseContext& Context) {
    Condition = Condition->PostProcess(Context);

    if (auto* B = As<BooleanNode>(Condition); B != nullptr && B->Value)
        return TrueStatement->PostProcess(Context);
    if (auto* N = As<NumberNode>(Condition); N != nullptr && N->Value > 0.5)
        return TrueStatement->PostProcess(Context);
    if (auto* I = As<Int64Node>(Condition); I != nullptr && I->Value >= 1)
        return TrueStatement->PostProcess(Context);

    bool IsFalse = false;
    if (auto* B = As<BooleanNode>(Condition); B != nullptr && !B->Value)
        IsFalse = true;
    else if (auto* N = As<NumberNode>(Condition); N != nullptr && N->Value <= 0.5)
        IsFalse = true;
    else if (auto* I = As<Int64Node>(Condition); I != nullptr && I->Value < 1)
        IsFalse = true;
    if (IsFalse) {
        if (FalseStatement != nullptr)
            return FalseStatement->PostProcess(Context);
        return EmptyNode::Create();
    }

    TrueStatement = TrueStatement->PostProcess(Context);
    if (FalseStatement != nullptr)
        FalseStatement = FalseStatement->PostProcess(Context);
    return this;
}

IASTNode* IfNode::Duplicate(ParseContext& Context) {
    return Context.Make<IfNode>(_NearbyToken, Condition->Duplicate(Context), TrueStatement->Duplicate(Context),
                                FalseStatement ? FalseStatement->Duplicate(Context) : nullptr);
}

void IfNode::GenerateCode(Bytecode::BytecodeContext& Context) {
    using Op = IGMInstruction::Opcode;
    using DT = IGMInstruction::DataType;

    Condition->GenerateCode(Context);
    Context.ConvertDataType(DT::Boolean);

    int64_t InitialLastArrayOwnerID = Context.LastArrayOwnerID();
    Bytecode::SingleForwardBranchPatch ConditionBranch(Context, Context.Emit(Op::BranchFalse));

    TrueStatement->GenerateCode(Context);
    int64_t PostTrueLastArrayOwnerID = Context.LastArrayOwnerID();

    if (FalseStatement != nullptr) {
        Context.SetLastArrayOwnerID(InitialLastArrayOwnerID);
        Bytecode::SingleForwardBranchPatch SkipElseBranch(Context, Context.Emit(Op::Branch));
        ConditionBranch.Patch(Context);
        FalseStatement->GenerateCode(Context);
        SkipElseBranch.Patch(Context);
    } else {
        ConditionBranch.Patch(Context);
    }

    if (Context.LastArrayOwnerID() != PostTrueLastArrayOwnerID) {
        Context.SetLastArrayOwnerID(-1);
    }
}

} // namespace Underanalyzer::Compiler::Nodes

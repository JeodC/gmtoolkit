
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/ThrowNode.h"

#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/Bytecode/InstructionPatch.h"
#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/FunctionScope.h"
#include "Underanalyzer/Compiler/Lexer/Token.h"
#include "Underanalyzer/Compiler/Nodes/BlockNode.h"
#include "Underanalyzer/Compiler/Parser/Expressions.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"
#include "Underanalyzer/Compiler/Parser/TryStatementContext.h"
#include "Underanalyzer/IGameContext.h"
#include "Underanalyzer/VMConstants.h"

namespace Underanalyzer::Compiler::Nodes {

ThrowNode* ThrowNode::Parse(Parser::ParseContext& Context) {
    Lexer::TokenKeyword* TokenKw = Context.EnsureToken(Lexer::KeywordKind::Throw);
    if (TokenKw == nullptr)
        return nullptr;
    IASTNode* Expr = Parser::Expressions::ParseExpression(Context);
    if (Expr == nullptr)
        return nullptr;
    return Context.Make<ThrowNode>(TokenKw, Expr);
}

// Older runtimes don't run finally before propagating a throw; emit a duplicate of
// the finally block ahead of the throw on those versions so the semantics line up.
IASTNode* ThrowNode::PostProcess(Parser::ParseContext& Context) {
    Expression = Expression->PostProcess(Context);

    Parser::TryStatementContext* TryCtx = Context.TryStatementCtx();
    if (TryCtx != nullptr && TryCtx->HasFinally() && TryCtx->ThrowFinallyGeneration() &&
        Context.CompileContextRef().GameContext().UsingFinallyBeforeThrow()) {
        BlockNode* NewBlock = BlockNode::CreateEmpty(Context, _NearbyToken, 2);
        NewBlock->Children().push_back(Context.CurrentScope().TryFinallyNodes().back()->Duplicate(Context));
        NewBlock->Children().push_back(this);
        return NewBlock;
    }
    return this;
}

IASTNode* ThrowNode::Duplicate(Parser::ParseContext& Context) {
    return Context.Make<ThrowNode>(_NearbyToken, Expression->Duplicate(Context));
}

void ThrowNode::GenerateCode(Bytecode::BytecodeContext& Context) {
    using DT = IGMInstruction::DataType;
    Expression->GenerateCode(Context);
    Context.ConvertDataType(DT::Variable);
    Context.EmitCall(Bytecode::FunctionPatch::FromBuiltin(Context, std::string(VMConstants::ThrowFunction)), 1);
}

} // namespace Underanalyzer::Compiler::Nodes

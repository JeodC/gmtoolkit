
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/PrefixNode.h"

#include "Underanalyzer/Compiler/Bytecode/ArrayOwners.h"
#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/Parser/Expressions.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"

#include <stdexcept>

namespace Underanalyzer::Compiler::Nodes {

PrefixNode* PrefixNode::Parse(Parser::ParseContext& Context, Lexer::TokenOperator* Token, bool IsIncrement) {
    IASTNode* Expr = Parser::Expressions::ParseChainExpression(Context);
    if (Expr == nullptr)
        return nullptr;
    auto* Assignable = dynamic_cast<IAssignableASTNode*>(Expr);
    if (Assignable == nullptr)
        return nullptr;
    return Context.Make<PrefixNode>(Token, Assignable, IsIncrement);
}

IASTNode* PrefixNode::PostProcess(Parser::ParseContext& Context) {
    IASTNode* Processed = Expression->PostProcess(Context);
    auto* AsAssignable = dynamic_cast<IAssignableASTNode*>(Processed);
    if (AsAssignable == nullptr)
        throw std::runtime_error("Destination no longer assignable");
    Expression = AsAssignable;
    return this;
}

IASTNode* PrefixNode::Duplicate(Parser::ParseContext& Context) {
    auto* Dup = dynamic_cast<IAssignableASTNode*>(Expression->Duplicate(Context));
    if (Dup == nullptr)
        throw std::runtime_error("Destination no longer assignable");
    PrefixNode* Node = Context.Make<PrefixNode>(_NearbyToken, Dup, IsIncrement);
    Node->SetIsStatement(_IsStatement);
    return Node;
}

void PrefixNode::GenerateCode(Bytecode::BytecodeContext& Context) {
    using Op = IGMInstruction::Opcode;
    if (Context.CanGenerateArrayOwners()) {
        if (Bytecode::ArrayOwners::ContainsArrayAccessor(Expression) ||
            Bytecode::ArrayOwners::IsArraySetFunctionOrContainsSubLiteral(Expression)) {
            Context.SetCanGenerateArrayOwners(false);
            if (!Bytecode::ArrayOwners::GenerateSetArrayOwner(Context, Expression)) {
                Expression->GenerateCode(Context);
                Context.Emit(Op::PopDelete, Context.PopDataType());
            }
            Expression->GeneratePrePostAssignCode(Context, IsIncrement, true, false);
            if (_IsStatement) {
                Context.Emit(Op::PopDelete, Context.PopDataType());
            }
            Context.SetCanGenerateArrayOwners(true);
            return;
        }
    }
    Expression->GeneratePrePostAssignCode(Context, IsIncrement, true, _IsStatement);
}

} // namespace Underanalyzer::Compiler::Nodes

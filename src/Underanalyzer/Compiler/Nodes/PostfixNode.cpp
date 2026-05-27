
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/PostfixNode.h"

#include "Underanalyzer/Compiler/Bytecode/ArrayOwners.h"
#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"

#include <stdexcept>

namespace Underanalyzer::Compiler::Nodes {

IASTNode* PostfixNode::PostProcess(Parser::ParseContext& Context) {
    IASTNode* Processed = Expression->PostProcess(Context);
    auto* AsAssignable = dynamic_cast<IAssignableASTNode*>(Processed);
    if (AsAssignable == nullptr)
        throw std::runtime_error("Destination no longer assignable");
    Expression = AsAssignable;
    return this;
}

IASTNode* PostfixNode::Duplicate(Parser::ParseContext& Context) {
    auto* Dup = dynamic_cast<IAssignableASTNode*>(Expression->Duplicate(Context));
    if (Dup == nullptr)
        throw std::runtime_error("Destination no longer assignable");
    PostfixNode* Node = Context.Make<PostfixNode>(_NearbyToken, Dup, IsIncrement);
    Node->SetIsStatement(_IsStatement);
    return Node;
}

void PostfixNode::GenerateCode(Bytecode::BytecodeContext& Context) {
    using Op = IGMInstruction::Opcode;
    if (Context.CanGenerateArrayOwners()) {
        if (Bytecode::ArrayOwners::ContainsArrayAccessor(Expression) ||
            Bytecode::ArrayOwners::IsArraySetFunctionOrContainsSubLiteral(Expression)) {
            Context.SetCanGenerateArrayOwners(false);
            if (!Bytecode::ArrayOwners::GenerateSetArrayOwner(Context, Expression)) {

                Expression->GenerateCode(Context);
                Context.Emit(Op::PopDelete, Context.PopDataType());
            }

            Expression->GeneratePrePostAssignCode(Context, IsIncrement, false, false);
            if (_IsStatement) {
                Context.Emit(Op::PopDelete, Context.PopDataType());
            }
            Context.SetCanGenerateArrayOwners(true);
            return;
        }
    }
    Expression->GeneratePrePostAssignCode(Context, IsIncrement, false, _IsStatement);
}

} // namespace Underanalyzer::Compiler::Nodes


// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/SwitchCaseNode.h"

#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/Nodes/AccessorNode.h"
#include "Underanalyzer/Compiler/Nodes/DotVariableNode.h"
#include "Underanalyzer/Compiler/Nodes/SimpleVariableNode.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"

#include <stdexcept>

namespace Underanalyzer::Compiler::Nodes {

IASTNode* SwitchCaseNode::PostProcess(Parser::ParseContext& Context) {
    if (Expression != nullptr)
        Expression = Expression->PostProcess(Context);

    if (Expression != nullptr) {
        bool Ok = dynamic_cast<IConstantASTNode*>(Expression) != nullptr ||
                  As<SimpleVariableNode>(Expression) != nullptr || As<DotVariableNode>(Expression) != nullptr ||
                  As<AccessorNode>(Expression) != nullptr;
        if (!Ok) {
            Context.CompileContextRef().PushError("Failed to resolve switch case to a constant value or variable",
                                                  Expression->NearbyToken());
        }
    }
    return this;
}

IASTNode* SwitchCaseNode::Duplicate(Parser::ParseContext& Context) {
    return Context.Make<SwitchCaseNode>(_NearbyToken, Expression ? Expression->Duplicate(Context) : nullptr);
}

void SwitchCaseNode::GenerateCode(Bytecode::BytecodeContext&) {
    throw std::logic_error("SwitchCaseNode::GenerateCode should not be called directly");
}

} // namespace Underanalyzer::Compiler::Nodes

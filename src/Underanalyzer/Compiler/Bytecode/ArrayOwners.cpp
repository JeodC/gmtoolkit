
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Bytecode/ArrayOwners.h"

#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/FunctionScope.h"
#include "Underanalyzer/Compiler/Nodes/AccessorNode.h"
#include "Underanalyzer/Compiler/Nodes/DotVariableNode.h"
#include "Underanalyzer/Compiler/Nodes/FunctionCallNode.h"
#include "Underanalyzer/Compiler/Nodes/FunctionDeclNode.h"
#include "Underanalyzer/Compiler/Nodes/IASTNode.h"
#include "Underanalyzer/Compiler/Nodes/NumberNode.h"
#include "Underanalyzer/Compiler/Nodes/SimpleFunctionCallNode.h"
#include "Underanalyzer/Compiler/Nodes/SimpleVariableNode.h"
#include "Underanalyzer/VMConstants.h"

namespace Underanalyzer::Compiler::Bytecode {

using namespace Nodes;

// Recurse looking for an array literal, but stop at call/function-decl boundaries:
// those introduce a new array-owner scope, so anything inside them isn't ours to track.
bool ArrayOwners::ContainsNewArrayLiteral(IASTNode* node) {
    if (auto* call = dynamic_cast<SimpleFunctionCallNode*>(node);
        call != nullptr && call->FunctionName == VMConstants::NewArrayFunction) {
        return true;
    }
    if (dynamic_cast<SimpleFunctionCallNode*>(node) == nullptr && dynamic_cast<FunctionCallNode*>(node) == nullptr &&
        dynamic_cast<FunctionDeclNode*>(node) == nullptr) {
        for (IASTNode* child : node->EnumerateChildren()) {
            if (ContainsNewArrayLiteral(child))
                return true;
        }
    }
    return false;
}

bool ArrayOwners::ContainsArrayAccessor(IASTNode* node) {
    if (auto* accessor = dynamic_cast<AccessorNode*>(node);
        accessor != nullptr && accessor->KindValue == AccessorNode::AccessorKind::Array) {
        return true;
    }
    if (dynamic_cast<SimpleFunctionCallNode*>(node) == nullptr && dynamic_cast<FunctionCallNode*>(node) == nullptr &&
        dynamic_cast<FunctionDeclNode*>(node) == nullptr) {
        for (IASTNode* child : node->EnumerateChildren()) {
            if (ContainsArrayAccessor(child))
                return true;
        }
    }
    return false;
}

bool ArrayOwners::IsArraySetFunction(IASTNode* node) {
    auto* call = dynamic_cast<SimpleFunctionCallNode*>(node);
    if (call == nullptr)
        return false;
    const std::string& n = call->FunctionName;
    return n == "array_set" || n == "array_set_pre" || n == "array_set_post" || n == "array_set_2D" ||
           n == "array_set_2D_pre" || n == "array_set_2D_post" || n == "array_create" ||
           n == VMConstants::NewArrayFunction;
}

bool ArrayOwners::IsArraySetFunctionOrContainsSubLiteral(IASTNode* node) {
    if (IsArraySetFunction(node))
        return true;
    for (IASTNode* child : node->EnumerateChildren()) {
        if (ContainsNewArrayLiteral(child))
            return true;
    }
    return false;
}

IVariableASTNode* ArrayOwners::FindArrayVariable(IASTNode* expression) {
    if (auto* var = dynamic_cast<IVariableASTNode*>(expression)) {
        return var;
    }
    if (auto* accessor = dynamic_cast<AccessorNode*>(expression)) {
        return FindArrayVariable(accessor->Expression);
    }
    return nullptr;
}

// Emit SetArrayOwner only if the owner id actually changed since the last emission;
// this is the copy-on-write tracking used by arrays in modern bytecode.
bool ArrayOwners::GenerateSetArrayOwner(BytecodeContext& context, IASTNode* expression) {
    int64_t id;
    if (IVariableASTNode* variableNode = FindArrayVariable(expression)) {
        bool isDot = dynamic_cast<DotVariableNode*>(variableNode) != nullptr ||
                     (dynamic_cast<SimpleVariableNode*>(variableNode) != nullptr &&
                      static_cast<SimpleVariableNode*>(variableNode)->CollapsedFromDot());
        const std::string& name = variableNode->VariableName();
        id = context.GenerateArrayOwnerID(&name, context.CurrentScope().ArrayOwnerID(), isDot);
    } else {
        id = context.GenerateArrayOwnerID(nullptr, context.CurrentScope().ArrayOwnerID(), false);
    }

    if (id != context.LastArrayOwnerID()) {
        context.SetLastArrayOwnerID(id);
        NumberNode::GenerateCode(context, static_cast<double>(id));
        context.ConvertDataType(IGMInstruction::DataType::Int32);
        context.Emit(IGMInstruction::ExtendedOpcode::SetArrayOwner);
        return true;
    }
    return false;
}

} // namespace Underanalyzer::Compiler::Bytecode


// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

namespace Underanalyzer::Compiler::Nodes {
class IASTNode;
class IVariableASTNode;
} // namespace Underanalyzer::Compiler::Nodes

namespace Underanalyzer::Compiler::Bytecode {

class BytecodeContext;

class ArrayOwners {
  public:
    static bool ContainsNewArrayLiteral(Nodes::IASTNode* node);
    static bool ContainsArrayAccessor(Nodes::IASTNode* node);
    static bool IsArraySetFunction(Nodes::IASTNode* node);
    static bool IsArraySetFunctionOrContainsSubLiteral(Nodes::IASTNode* node);
    static bool GenerateSetArrayOwner(BytecodeContext& context, Nodes::IASTNode* expression);

  private:
    static Nodes::IVariableASTNode* FindArrayVariable(Nodes::IASTNode* expression);
};

} // namespace Underanalyzer::Compiler::Bytecode


// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/Compiler/Nodes/IASTNode.h"

#include <string>

namespace Underanalyzer::Compiler::Lexer {
class TokenVariable;
class TokenFunction;
} // namespace Underanalyzer::Compiler::Lexer

namespace Underanalyzer::Compiler::Nodes {

class DotVariableNode final : public IAssignableASTNode, public IVariableASTNode {
  public:
    static constexpr ASTNodeKind kKind = ASTNodeKind::DotVariable;

    DotVariableNode(IASTNode* LeftExpressionIn, Lexer::TokenVariable* Token);
    DotVariableNode(IASTNode* LeftExpressionIn, Lexer::TokenFunction* Token);
    DotVariableNode(IASTNode* LeftExpressionIn, std::string VariableNameIn, IBuiltinVariable* BuiltinVariableIn,
                    Lexer::IToken* NearbyTokenIn)
        : LeftExpression(LeftExpressionIn), _VariableName(std::move(VariableNameIn)),
          _BuiltinVariable(BuiltinVariableIn), _NearbyToken(NearbyTokenIn) {
    }

    IASTNode* LeftExpression;

    const std::string& VariableName() const override {
        return _VariableName;
    }
    IBuiltinVariable* BuiltinVariable() const override {
        return _BuiltinVariable;
    }

    ASTNodeKind Kind() const override {
        return kKind;
    }
    Lexer::IToken* NearbyToken() const override {
        return _NearbyToken;
    }
    IASTNode* PostProcess(Parser::ParseContext& Context) override;
    IASTNode* Duplicate(Parser::ParseContext& Context) override;
    void GenerateCode(Bytecode::BytecodeContext& Context) override;
    std::vector<IASTNode*> EnumerateChildren() override {
        return { LeftExpression };
    }

    void GenerateAssignCode(Bytecode::BytecodeContext& Context) override;
    void GenerateCompoundAssignCode(Bytecode::BytecodeContext& Context, IASTNode* Expression,
                                    IGMInstruction::Opcode OperationOpcode) override;
    void GeneratePrePostAssignCode(Bytecode::BytecodeContext& Context, bool IsIncrement, bool IsPre,
                                   bool IsStatement) override;

  private:
    std::string _VariableName;
    IBuiltinVariable* _BuiltinVariable;
    Lexer::IToken* _NearbyToken;
};

} // namespace Underanalyzer::Compiler::Nodes

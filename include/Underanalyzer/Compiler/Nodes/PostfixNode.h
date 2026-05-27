
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/Compiler/Nodes/IASTNode.h"

namespace Underanalyzer::Compiler::Nodes {

class PostfixNode final : public IMaybeStatementASTNode {
  public:
    static constexpr ASTNodeKind kKind = ASTNodeKind::Postfix;

    PostfixNode(Lexer::IToken* NearbyTokenIn, IAssignableASTNode* ExpressionIn, bool IsIncrementIn)
        : Expression(ExpressionIn), IsIncrement(IsIncrementIn), _NearbyToken(NearbyTokenIn) {
    }

    IAssignableASTNode* Expression;
    bool IsIncrement;

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
        return { Expression };
    }

    bool IsStatement() const override {
        return _IsStatement;
    }
    void SetIsStatement(bool v) override {
        _IsStatement = v;
    }

  private:
    Lexer::IToken* _NearbyToken;
    bool _IsStatement = false;
};

} // namespace Underanalyzer::Compiler::Nodes

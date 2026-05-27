
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/Compiler/Nodes/IASTNode.h"

namespace Underanalyzer::Compiler::Nodes {

class NewObjectNode final : public IMaybeStatementASTNode {
  public:
    static constexpr ASTNodeKind kKind = ASTNodeKind::NewObject;

    NewObjectNode(Lexer::IToken* NearbyTokenIn, IASTNode* ExpressionIn, std::vector<IASTNode*> ArgumentsIn)
        : Expression(ExpressionIn), Arguments(std::move(ArgumentsIn)), _NearbyToken(NearbyTokenIn) {
    }

    IASTNode* Expression;
    std::vector<IASTNode*> Arguments;

    static NewObjectNode* Parse(Parser::ParseContext& Context);

    ASTNodeKind Kind() const override {
        return kKind;
    }
    Lexer::IToken* NearbyToken() const override {
        return _NearbyToken;
    }
    IASTNode* PostProcess(Parser::ParseContext& Context) override;
    IASTNode* Duplicate(Parser::ParseContext& Context) override;
    void GenerateCode(Bytecode::BytecodeContext& Context) override;
    std::vector<IASTNode*> EnumerateChildren() override;

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


// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/Compiler/Nodes/IASTNode.h"

namespace Underanalyzer::Compiler::Nodes {

class NullishCoalesceNode final : public IASTNode {
  public:
    static constexpr ASTNodeKind kKind = ASTNodeKind::NullishCoalesce;

    NullishCoalesceNode(Lexer::IToken* NearbyTokenIn, IASTNode* LeftIn, IASTNode* RightIn)
        : Left(LeftIn), Right(RightIn), _NearbyToken(NearbyTokenIn) {
    }

    IASTNode* Left;
    IASTNode* Right;

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
        return { Left, Right };
    }

  private:
    Lexer::IToken* _NearbyToken;
};

} // namespace Underanalyzer::Compiler::Nodes

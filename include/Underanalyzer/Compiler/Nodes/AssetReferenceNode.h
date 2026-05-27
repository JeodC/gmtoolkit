
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/Compiler/Nodes/IASTNode.h"

namespace Underanalyzer::Compiler::Lexer {
class TokenAssetReference;
}

namespace Underanalyzer::Compiler::Nodes {

class AssetReferenceNode final : public IConstantASTNode {
  public:
    static constexpr ASTNodeKind kKind = ASTNodeKind::AssetReference;

    explicit AssetReferenceNode(Lexer::TokenAssetReference* Token);

    int AssetId;

    ASTNodeKind Kind() const override;
    Lexer::IToken* NearbyToken() const override;
    IASTNode* PostProcess(Parser::ParseContext& Context) override;
    IASTNode* Duplicate(Parser::ParseContext&) override {
        return this;
    }
    void GenerateCode(Bytecode::BytecodeContext& Context) override;
    std::vector<IASTNode*> EnumerateChildren() override {
        return {};
    }

  private:
    Lexer::TokenAssetReference* _Token;
};

} // namespace Underanalyzer::Compiler::Nodes

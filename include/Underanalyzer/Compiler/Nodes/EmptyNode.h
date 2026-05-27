
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/Compiler/Nodes/IASTNode.h"

namespace Underanalyzer::Compiler::Nodes {

class EmptyNode final : public IASTNode {
  public:
    static constexpr ASTNodeKind kKind = ASTNodeKind::Empty;

    static EmptyNode* Create();
    static EmptyNode* Create(Parser::ParseContext& Context, Lexer::IToken* NearbyToken);

    ASTNodeKind Kind() const override {
        return kKind;
    }
    Lexer::IToken* NearbyToken() const override {
        return _NearbyToken;
    }
    IASTNode* PostProcess(Parser::ParseContext&) override {
        return this;
    }
    IASTNode* Duplicate(Parser::ParseContext&) override {
        return this;
    }
    void GenerateCode(Bytecode::BytecodeContext&) override {
    }
    std::vector<IASTNode*> EnumerateChildren() override {
        return {};
    }

    explicit EmptyNode(Lexer::IToken* nearbyToken) : _NearbyToken(nearbyToken) {
    }

  private:
    Lexer::IToken* _NearbyToken;
};

} // namespace Underanalyzer::Compiler::Nodes

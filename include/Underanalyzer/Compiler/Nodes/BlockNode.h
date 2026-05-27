
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/Compiler/Nodes/IASTNode.h"

namespace Underanalyzer::Compiler::Nodes {

class BlockNode final : public IASTNode {
  public:
    static constexpr ASTNodeKind kKind = ASTNodeKind::Block;

    static BlockNode* ParseRoot(Parser::ParseContext& Context);
    static BlockNode* ParseRegular(Parser::ParseContext& Context);
    static BlockNode* CreateEmpty(Parser::ParseContext& Context, Lexer::IToken* NearbyTokenIn, int Capacity = 4);

    std::vector<IASTNode*>& Children() {
        return _Children;
    }

    ASTNodeKind Kind() const override {
        return kKind;
    }
    Lexer::IToken* NearbyToken() const override {
        return _NearbyToken;
    }
    void SetNearbyToken(Lexer::IToken* t) {
        _NearbyToken = t;
    }
    IASTNode* PostProcess(Parser::ParseContext& Context) override;
    void PostProcessChildrenOnly(Parser::ParseContext& Context);
    IASTNode* Duplicate(Parser::ParseContext& Context) override;
    void GenerateCode(Bytecode::BytecodeContext& Context) override;
    void GenerateStaticCode(Bytecode::BytecodeContext& Context);
    std::vector<IASTNode*> EnumerateChildren() override {
        return _Children;
    }

    BlockNode(std::vector<IASTNode*> ChildrenIn, Lexer::IToken* NearbyTokenIn)
        : _Children(std::move(ChildrenIn)), _NearbyToken(NearbyTokenIn) {
    }

  private:
    std::vector<IASTNode*> _Children;
    Lexer::IToken* _NearbyToken;
};

} // namespace Underanalyzer::Compiler::Nodes


// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/Compiler/Nodes/IASTNode.h"

#include <string>

namespace Underanalyzer::Compiler::Nodes {

class LocalVarDeclNode final : public IASTNode {
  public:
    static constexpr ASTNodeKind kKind = ASTNodeKind::LocalVarDecl;

    LocalVarDeclNode(Lexer::IToken* NearbyTokenIn, std::vector<std::string> DeclaredLocalsIn,
                     std::vector<IASTNode*> AssignedValuesIn)
        : DeclaredLocals(std::move(DeclaredLocalsIn)), AssignedValues(std::move(AssignedValuesIn)),
          _NearbyToken(NearbyTokenIn) {
    }

    std::vector<std::string> DeclaredLocals;
    std::vector<IASTNode*> AssignedValues;

    static LocalVarDeclNode* Parse(Parser::ParseContext& Context);

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

  private:
    Lexer::IToken* _NearbyToken;
};

} // namespace Underanalyzer::Compiler::Nodes

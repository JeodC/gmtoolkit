
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/Compiler/Nodes/IASTNode.h"

#include <optional>
#include <string>

namespace Underanalyzer::Compiler::Parser {
class TryStatementContext;
}

namespace Underanalyzer::Compiler::Nodes {

class WhileLoopNode;

class TryCatchNode final : public IASTNode {
  public:
    static constexpr ASTNodeKind kKind = ASTNodeKind::TryCatch;

    TryCatchNode(Lexer::IToken* NearbyTokenIn, IASTNode* TryIn, IASTNode* CatchIn,
                 std::optional<std::string> CatchVariableNameIn, IASTNode* FinallyIn, bool TryEscapeIn,
                 bool CatchEscapeIn)
        : Try(TryIn), Catch(CatchIn), CatchVariableName(std::move(CatchVariableNameIn)), Finally(FinallyIn),
          TryEscape(TryEscapeIn), CatchEscape(CatchEscapeIn), _NearbyToken(NearbyTokenIn) {
    }

    IASTNode* Try;
    IASTNode* Catch;
    std::optional<std::string> CatchVariableName;
    IASTNode* Finally;
    bool TryEscape;
    bool CatchEscape;

    static IASTNode* Parse(Parser::ParseContext& Context);

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
    WhileLoopNode* GenerateBlockLoop(Parser::ParseContext& Context, Parser::TryStatementContext& TryCtx,
                                     IASTNode* Block);

    Lexer::IToken* _NearbyToken;
};

} // namespace Underanalyzer::Compiler::Nodes

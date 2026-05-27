
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/Compiler/Nodes/IASTNode.h"

#include <optional>
#include <string>

namespace Underanalyzer::Compiler {
class FunctionScope;
}
namespace Underanalyzer::Compiler::Lexer {
class TokenKeyword;
}

namespace Underanalyzer::Compiler::Nodes {

class BlockNode;
class SimpleFunctionCallNode;

class FunctionDeclNode final : public IMaybeStatementASTNode {
  public:
    static constexpr ASTNodeKind kKind = ASTNodeKind::FunctionDecl;

    FunctionDeclNode(FunctionScope* ScopeIn, Lexer::IToken* NearbyTokenIn, std::optional<std::string> FunctionNameIn,
                     std::vector<std::string> ArgumentNamesIn, BlockNode* DefaultValueBlockIn, BlockNode* BodyIn,
                     SimpleFunctionCallNode* InheritanceCallIn, bool IsStructIn, bool IsConstructorIn)
        : Scope(ScopeIn), FunctionName(std::move(FunctionNameIn)), ArgumentNames(std::move(ArgumentNamesIn)),
          DefaultValueBlock(DefaultValueBlockIn), Body(BodyIn), InheritanceCall(InheritanceCallIn),
          IsStruct(IsStructIn), IsConstructor(IsConstructorIn), _NearbyToken(NearbyTokenIn) {
    }

    FunctionScope* Scope;
    std::optional<std::string> FunctionName;
    std::vector<std::string> ArgumentNames;
    BlockNode* DefaultValueBlock;
    BlockNode* Body;
    SimpleFunctionCallNode* InheritanceCall;
    bool IsStruct;
    bool IsConstructor;

    static FunctionDeclNode* Parse(Parser::ParseContext& Context, Lexer::TokenKeyword* TokenKw);
    static SimpleFunctionCallNode* ParseStruct(Parser::ParseContext& Context, Lexer::IToken* TokenOpen);

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

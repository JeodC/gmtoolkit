
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/Nodes/IASTNode.h"

#include <utility>

namespace Underanalyzer::Compiler::Lexer {
class TokenSeparator;
}

namespace Underanalyzer::Compiler::Nodes {

class AccessorNode final : public IAssignableASTNode {
  public:
    static constexpr ASTNodeKind kKind = ASTNodeKind::Accessor;

    enum class AccessorKind { Array, ArrayDirect, List, Map, Grid, Struct };

    AccessorNode(Lexer::IToken* NearbyTokenIn, IASTNode* ExpressionIn, AccessorKind KindIn,
                 IASTNode* AccessorExpressionIn, IASTNode* AccessorExpression2In = nullptr)
        : Expression(ExpressionIn), AccessorExpression(AccessorExpressionIn),
          AccessorExpression2(AccessorExpression2In), KindValue(KindIn), _NearbyToken(NearbyTokenIn) {
    }

    IASTNode* Expression;
    IASTNode* AccessorExpression;
    IASTNode* AccessorExpression2;
    AccessorKind KindValue;

    AccessorKind Kind_() const {
        return KindValue;
    }

    bool LeftmostSideOfDot() const {
        return _LeftmostSideOfDot;
    }
    void SetLeftmostSideOfDot(bool v) {
        _LeftmostSideOfDot = v;
    }

    static AccessorNode* Parse(Parser::ParseContext& Context, Lexer::TokenSeparator* Token, IASTNode* Expression,
                               AccessorKind KindIn);
    AccessorNode* Convert2DArrayToTwoAccessors(Parser::ParseContext& Context);

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

    void GenerateAssignCode(Bytecode::BytecodeContext& Context) override;
    void GenerateCompoundAssignCode(Bytecode::BytecodeContext& Context, IASTNode* Expression,
                                    IGMInstruction::Opcode OperationOpcode) override;
    void GeneratePrePostAssignCode(Bytecode::BytecodeContext& Context, bool IsIncrement, bool IsPre,
                                   bool IsStatement) override;

  private:
    std::pair<IGMInstruction::InstanceType, Bytecode::BytecodeContext::InstanceConversionType>
    GenerateVariableCode(Bytecode::BytecodeContext& Context, IVariableASTNode* Variable);
    void GenerateChainedCode(Bytecode::BytecodeContext& Context, bool IsPop);

    Lexer::IToken* _NearbyToken;
    bool _LeftmostSideOfDot = false;
};

} // namespace Underanalyzer::Compiler::Nodes

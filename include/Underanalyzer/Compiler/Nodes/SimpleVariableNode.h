
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/Compiler/Nodes/IASTNode.h"

#include <string>
#include <unordered_set>

namespace Underanalyzer::Compiler {
class ISubCompileContext;
}
namespace Underanalyzer::Compiler::Lexer {
class TokenVariable;
}
namespace Underanalyzer::Compiler::Bytecode {
struct VariablePatch;
}

namespace Underanalyzer::Compiler::Nodes {

class SimpleVariableNode final : public IAssignableASTNode, public IVariableASTNode {
  public:
    static constexpr ASTNodeKind kKind = ASTNodeKind::SimpleVariable;

    explicit SimpleVariableNode(Lexer::TokenVariable* Token);
    SimpleVariableNode(std::string VariableNameIn, IBuiltinVariable* BuiltinVariableIn)
        : _VariableName(std::move(VariableNameIn)), _BuiltinVariable(BuiltinVariableIn) {
    }
    SimpleVariableNode(std::string VariableNameIn, IBuiltinVariable* BuiltinVariableIn,
                       IGMInstruction::InstanceType ExplicitInstanceTypeIn)
        : _VariableName(std::move(VariableNameIn)), _BuiltinVariable(BuiltinVariableIn) {
        SetExplicitInstanceType(ExplicitInstanceTypeIn);
    }

    const std::string& VariableName() const override {
        return _VariableName;
    }
    IBuiltinVariable* BuiltinVariable() const override {
        return _BuiltinVariable;
    }

    bool HasExplicitInstanceType() const {
        return _HasExplicitInstanceType;
    }
    IGMInstruction::InstanceType ExplicitInstanceType() const {
        return _ExplicitInstanceType;
    }
    void SetExplicitInstanceType(IGMInstruction::InstanceType t) {
        _ExplicitInstanceType = t;
        _HasExplicitInstanceType = true;
    }

    bool IsFunctionCall() const {
        return _IsFunctionCall;
    }
    void SetIsFunctionCall(bool v) {
        _IsFunctionCall = v;
    }
    bool CollapsedFromDot() const {
        return _CollapsedFromDot;
    }
    void SetCollapsedFromDot(bool v) {
        _CollapsedFromDot = v;
    }
    bool LeftmostSideOfDot() const {
        return _LeftmostSideOfDot;
    }
    void SetLeftmostSideOfDot(bool v) {
        _LeftmostSideOfDot = v;
    }
    bool StructVariable() const {
        return _StructVariable;
    }
    void SetStructVariable(bool v) {
        _StructVariable = v;
    }
    bool RoomInstanceVariable() const {
        return _RoomInstanceVariable;
    }
    void SetRoomInstanceVariable(bool v) {
        _RoomInstanceVariable = v;
    }

    void SetNearbyToken(Lexer::IToken* t) {
        _NearbyToken = t;
    }

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
        return {};
    }

    void GenerateAssignCode(Bytecode::BytecodeContext& Context) override;
    void GenerateCompoundAssignCode(Bytecode::BytecodeContext& Context, IASTNode* Expression,
                                    IGMInstruction::Opcode OperationOpcode) override;
    void GeneratePrePostAssignCode(Bytecode::BytecodeContext& Context, bool IsIncrement, bool IsPre,
                                   bool IsStatement) override;

    IAssignableASTNode* ResolveStandaloneType(ISubCompileContext& Context);

    static SimpleVariableNode* CreateUndefined(Parser::ParseContext& Context);
    static IAssignableASTNode* CreateArgumentVariable(ISubCompileContext& Context, Lexer::IToken* NearbyTokenIn,
                                                      int ArgumentIndex, bool UseBuiltinInstanceType = false);
    static const std::unordered_set<std::string>& BuiltinArgumentVariables();

  private:
    Bytecode::VariablePatch CreateVariablePatch(Bytecode::BytecodeContext& Context) const;

    std::string _VariableName;
    IBuiltinVariable* _BuiltinVariable = nullptr;
    Lexer::IToken* _NearbyToken = nullptr;
    // Defaults to 0 like the C# field (not Self = -1): code paths that compare
    // against Self on a not-yet-resolved node must see the same value upstream sees.
    IGMInstruction::InstanceType _ExplicitInstanceType = static_cast<IGMInstruction::InstanceType>(0);
    bool _HasExplicitInstanceType = false;
    bool _IsFunctionCall = false;
    bool _CollapsedFromDot = false;
    bool _LeftmostSideOfDot = false;
    bool _StructVariable = false;
    bool _RoomInstanceVariable = false;
};

} // namespace Underanalyzer::Compiler::Nodes


// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Underanalyzer {
class IGameContext;
}
namespace Underanalyzer::Compiler::Nodes {
class IASTNode;
class BlockNode;
} // namespace Underanalyzer::Compiler::Nodes
namespace Underanalyzer::Compiler::Bytecode {
class IControlFlowContext;
class FunctionEntry;
} // namespace Underanalyzer::Compiler::Bytecode

namespace Underanalyzer::Compiler {

class FunctionScope {
  public:
    FunctionScope(FunctionScope* parent, bool isFunction) : _parent(parent), _isFunction(isFunction) {
    }

    FunctionScope* Parent() const {
        return _parent;
    }
    bool IsFunction() const {
        return _isFunction;
    }
    int LocalCount() const {
        return static_cast<int>(_declaredLocals.size());
    }

    Nodes::BlockNode* StaticInitializerBlock() const {
        return _staticInitializerBlock;
    }
    void SetStaticInitializerBlock(Nodes::BlockNode* b) {
        _staticInitializerBlock = b;
    }

    std::vector<Bytecode::IControlFlowContext*>& ControlFlowContexts() {
        return _controlFlowContexts;
    }

    bool GeneratingStaticBlock() const {
        return _generatingStaticBlock;
    }
    void SetGeneratingStaticBlock(bool v) {
        _generatingStaticBlock = v;
    }
    bool GeneratingDotVariableCall() const {
        return _generatingDotVariableCall;
    }
    void SetGeneratingDotVariableCall(bool v) {
        _generatingDotVariableCall = v;
    }
    bool GeneratingFunctionDeclHeader() const {
        return _generatingFunctionDeclHeader;
    }
    void SetGeneratingFunctionDeclHeader(bool v) {
        _generatingFunctionDeclHeader = v;
    }
    bool ProcessingBreakContinueContext() const {
        return _processingBreakContinueContext;
    }
    void SetProcessingBreakContinueContext(bool v) {
        _processingBreakContinueContext = v;
    }

    const std::string* StaticVariableName() const {
        return _staticVariableName.has_value() ? &*_staticVariableName : nullptr;
    }
    void SetStaticVariableName(std::optional<std::string> name) {
        _staticVariableName = std::move(name);
    }

    std::vector<Nodes::IASTNode*>& TryFinallyNodes() {
        return _tryFinallyNodes;
    }

    int64_t ArrayOwnerID() const {
        return _arrayOwnerID;
    }
    void SetArrayOwnerID(int64_t v) {
        _arrayOwnerID = v;
    }

    bool DeclareLocal(const std::string& name);
    bool IsLocalDeclared(const std::string& name) const;
    bool DeclareStatic(const std::string& name);
    bool IsStaticDeclared(const std::string& name) const;
    void DeclareArguments(const std::vector<std::string>& argumentNames);
    bool TryGetArgumentIndex(const std::string& name, int& index) const;
    bool TryDeclareFunction(const std::string& name);
    void AssignFunctionEntry(const std::string& name, Bytecode::FunctionEntry* entry);
    bool TryGetDeclaredFunction(IGameContext& context, const std::string& name, Bytecode::FunctionEntry*& entry);
    bool IsFunctionDeclared(IGameContext& context, const std::string& name) const;
    bool IsFunctionDeclaredImmediately(const std::string& name) const;

    const std::vector<std::string>& LocalsOrder() const {
        return _localsOrder;
    }

  private:
    FunctionScope* _parent;
    bool _isFunction;

    Nodes::BlockNode* _staticInitializerBlock = nullptr;
    std::vector<Bytecode::IControlFlowContext*> _controlFlowContexts;
    bool _generatingStaticBlock = false;
    bool _generatingDotVariableCall = false;
    bool _generatingFunctionDeclHeader = false;
    bool _processingBreakContinueContext = false;
    std::optional<std::string> _staticVariableName;
    std::vector<Nodes::IASTNode*> _tryFinallyNodes;
    int64_t _arrayOwnerID = 1;

    std::unordered_set<std::string> _declaredLocals;
    std::vector<std::string> _localsOrder;
    std::unordered_set<std::string> _declaredStatics;
    std::unordered_map<std::string, int> _declaredArguments;

    std::unordered_map<std::string, Bytecode::FunctionEntry*> _declaredFunctions;
};

} // namespace Underanalyzer::Compiler

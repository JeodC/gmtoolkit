
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <optional>
#include <string>

namespace Underanalyzer {
class IGMFunction;
}
namespace Underanalyzer::Compiler {
class FunctionScope;
}

namespace Underanalyzer::Compiler::Bytecode {

enum class FunctionEntryKind { FunctionDeclaration, StructInstantiation };

class FunctionEntry {
  public:
    FunctionEntry(FunctionEntry* parent, FunctionScope* scope, int bytecodeOffset, int argumentCount,
                  bool isConstructor, std::optional<std::string> functionName, bool declaredInRootScope,
                  std::optional<std::string> staticVariableName, FunctionEntryKind kind);

    FunctionEntry* Parent() const {
        return _parent;
    }
    FunctionScope* Scope() const {
        return _scope;
    }
    int BytecodeOffset() const {
        return _bytecodeOffset;
    }
    int ArgumentCount() const {
        return _argumentCount;
    }
    bool IsConstructor() const {
        return _isConstructor;
    }
    const std::optional<std::string>& FunctionName() const {
        return _functionName;
    }
    bool DeclaredInRootScope() const {
        return _declaredInRootScope;
    }
    const std::optional<std::string>& StaticVariableName() const {
        return _staticVariableName;
    }
    FunctionEntryKind Kind() const {
        return _kind;
    }
    IGMFunction* Function() const {
        return _function;
    }
    const std::optional<std::string>& ChildFunctionName() const {
        return _childFunctionName;
    }
    const std::optional<std::string>& StructName() const {
        return _structName;
    }

    void ResolveFunction(IGMFunction* function, std::string childFunctionName);
    void ResolveStructName(std::string name);

  private:
    FunctionEntry* _parent;
    FunctionScope* _scope;
    int _bytecodeOffset;
    int _argumentCount;
    bool _isConstructor;
    std::optional<std::string> _functionName;
    bool _declaredInRootScope;
    std::optional<std::string> _staticVariableName;
    FunctionEntryKind _kind;

    IGMFunction* _function = nullptr;
    std::optional<std::string> _childFunctionName;
    std::optional<std::string> _structName;
};

} // namespace Underanalyzer::Compiler::Bytecode

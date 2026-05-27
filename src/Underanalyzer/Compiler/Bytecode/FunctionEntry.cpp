
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Bytecode/FunctionEntry.h"

#include <stdexcept>

namespace Underanalyzer::Compiler::Bytecode {

FunctionEntry::FunctionEntry(FunctionEntry* parent, FunctionScope* scope, int bytecodeOffset, int argumentCount,
                             bool isConstructor, std::optional<std::string> functionName, bool declaredInRootScope,
                             std::optional<std::string> staticVariableName, FunctionEntryKind kind)
    : _parent(parent), _scope(scope), _bytecodeOffset(bytecodeOffset), _argumentCount(argumentCount),
      _isConstructor(isConstructor), _functionName(std::move(functionName)), _declaredInRootScope(declaredInRootScope),
      _staticVariableName(std::move(staticVariableName)), _kind(kind) {
}

void FunctionEntry::ResolveFunction(IGMFunction* function, std::string childFunctionName) {
    if (_function != nullptr) {
        throw std::logic_error("Tried to resolve function when it was already resolved");
    }
    _function = function;
    _childFunctionName = std::move(childFunctionName);
}

void FunctionEntry::ResolveStructName(std::string name) {
    if (_kind != FunctionEntryKind::StructInstantiation) {
        throw std::logic_error("Tried to resolve struct name for non-struct function entry");
    }
    if (_structName.has_value()) {
        throw std::logic_error("Tried to resolve struct name when it was already resolved");
    }
    _structName = std::move(name);
}

} // namespace Underanalyzer::Compiler::Bytecode

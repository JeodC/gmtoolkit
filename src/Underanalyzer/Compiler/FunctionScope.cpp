
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/FunctionScope.h"

#include "Underanalyzer/IGameContext.h"

namespace Underanalyzer::Compiler {

// Two-structure storage: the set dedupes lookups, the vector preserves declaration
// order so the locals chunk gets written in the order the source declared them.
bool FunctionScope::DeclareLocal(const std::string& name) {
    if (_declaredLocals.insert(name).second) {
        _localsOrder.push_back(name);
        return true;
    }
    return false;
}

bool FunctionScope::IsLocalDeclared(const std::string& name) const {
    return _declaredLocals.find(name) != _declaredLocals.end();
}

bool FunctionScope::DeclareStatic(const std::string& name) {
    return _declaredStatics.insert(name).second;
}

bool FunctionScope::IsStaticDeclared(const std::string& name) const {
    return _declaredStatics.find(name) != _declaredStatics.end();
}

void FunctionScope::DeclareArguments(const std::vector<std::string>& argumentNames) {
    for (size_t i = 0; i < argumentNames.size(); i++) {
        _declaredArguments[argumentNames[i]] = static_cast<int>(i);
    }
}

bool FunctionScope::TryGetArgumentIndex(const std::string& name, int& index) const {
    auto it = _declaredArguments.find(name);
    if (it == _declaredArguments.end())
        return false;
    index = it->second;
    return true;
}

// Two-step: first reserve the name with a null entry (during the foresight pass),
// then fill in the real FunctionEntry once the body has been compiled.
bool FunctionScope::TryDeclareFunction(const std::string& name) {
    return _declaredFunctions.try_emplace(name, nullptr).second;
}

void FunctionScope::AssignFunctionEntry(const std::string& name, Bytecode::FunctionEntry* entry) {
    _declaredFunctions[name] = entry;
}

// Newer runtimes walk the scope chain looking for a function name; older ones only
// see functions declared in the immediate scope.
bool FunctionScope::TryGetDeclaredFunction(IGameContext& context, const std::string& name,
                                           Bytecode::FunctionEntry*& entry) {
    auto it = _declaredFunctions.find(name);
    if (it != _declaredFunctions.end()) {
        entry = it->second;
        return entry != nullptr;
    }
    if (context.UsingNewFunctionResolution() && _parent != nullptr) {
        return _parent->TryGetDeclaredFunction(context, name, entry);
    }
    return false;
}

bool FunctionScope::IsFunctionDeclared(IGameContext& context, const std::string& name) const {
    if (_declaredFunctions.find(name) != _declaredFunctions.end())
        return true;
    if (context.UsingNewFunctionResolution() && _parent != nullptr) {
        return _parent->IsFunctionDeclared(context, name);
    }
    return false;
}

bool FunctionScope::IsFunctionDeclaredImmediately(const std::string& name) const {
    return _declaredFunctions.find(name) != _declaredFunctions.end();
}

} // namespace Underanalyzer::Compiler

// SPDX-License-Identifier: MPL-2.0

#include "GMSLib/Compiler/BuiltinList.h"

#include "GMSLib/Compiler/Builtins.h"

namespace GMSLib {

namespace {

class BuiltinFunctionImpl final : public Underanalyzer::Compiler::IBuiltinFunction {
  public:
    BuiltinFunctionImpl(std::string NameIn) : _Name(std::move(NameIn)) {
    }
    const std::string& Name() const override {
        return _Name;
    }
    int MinArguments() const override {
        return 0;
    }
    int MaxArguments() const override {
        return 65535;
    }

  private:
    std::string _Name;
};

class BuiltinVariableImpl final : public Underanalyzer::Compiler::IBuiltinVariable {
  public:
    BuiltinVariableImpl(std::string NameIn, bool IsGlobalIn, bool IsAutomaticArrayIn)
        : _Name(std::move(NameIn)), _IsGlobal(IsGlobalIn), _IsAutomaticArray(IsAutomaticArrayIn) {
    }
    const std::string& Name() const override {
        return _Name;
    }
    bool IsGlobal() const override {
        return _IsGlobal;
    }
    bool IsAutomaticArray() const override {
        return _IsAutomaticArray;
    }
    bool CanSet() const override {
        return true;
    }

  private:
    std::string _Name;
    bool _IsGlobal;
    bool _IsAutomaticArray;
};

} // namespace

Underanalyzer::Compiler::IBuiltinFunction* BuiltinList::LookupBuiltinFunction(const std::string& Name) {
    auto It = _FunctionCache.find(Name);
    if (It != _FunctionCache.end())
        return It->second.get();
    if (!GMSLib::Compiler::lookup_builtin_func(Name))
        return nullptr;
    auto [Inserted, _] = _FunctionCache.emplace(Name, std::make_unique<BuiltinFunctionImpl>(Name));
    return Inserted->second.get();
}

Underanalyzer::Compiler::IBuiltinVariable* BuiltinList::LookupBuiltinVariable(const std::string& Name) {
    auto It = _VariableCache.find(Name);
    if (It != _VariableCache.end())
        return It->second.get();
    GMSLib::Compiler::BuiltinVar Info;
    if (!GMSLib::Compiler::lookup_builtin_var(Name, &Info))
        return nullptr;
    auto [Inserted, _] = _VariableCache.emplace(
        Name, std::make_unique<BuiltinVariableImpl>(Name, Info.is_global, Info.is_automatic_array));
    return Inserted->second.get();
}

bool BuiltinList::LookupConstantDouble(const std::string& Name, double& Value) {
    return GMSLib::Compiler::lookup_constant(Name, &Value);
}

} // namespace GMSLib

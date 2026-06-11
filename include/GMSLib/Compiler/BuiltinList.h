
// Source: github.com/UnderminersTeam/UndertaleModTool @ c4e5c2c3
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/Compiler/IBuiltins.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace GMSLib {

class GMSData;

class BuiltinList final : public Underanalyzer::Compiler::IBuiltins {
  public:
    // Mirrors UTMT's BuiltinList(UndertaleData): every function already present
    // in the game's FUNC chunk that isn't a script becomes a callable builtin
    // (extension functions, DLL exports, newer YYG builtins missing from the
    // static table).
    void LoadFunctionsFromData(const GMSData& Data);

    Underanalyzer::Compiler::IBuiltinFunction* LookupBuiltinFunction(const std::string& Name) override;
    Underanalyzer::Compiler::IBuiltinVariable* LookupBuiltinVariable(const std::string& Name) override;
    bool LookupConstantDouble(const std::string& Name, double& Value) override;

  private:
    std::unordered_set<std::string> _DataFunctions;
    std::unordered_map<std::string, std::unique_ptr<Underanalyzer::Compiler::IBuiltinFunction>> _FunctionCache;
    std::unordered_map<std::string, std::unique_ptr<Underanalyzer::Compiler::IBuiltinVariable>> _VariableCache;
};

} // namespace GMSLib

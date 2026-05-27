
// Source: github.com/UnderminersTeam/UndertaleModTool @ c4e5c2c3
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/Compiler/IBuiltins.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace GMSLib {

class BuiltinList final : public Underanalyzer::Compiler::IBuiltins {
  public:
    Underanalyzer::Compiler::IBuiltinFunction* LookupBuiltinFunction(const std::string& Name) override;
    Underanalyzer::Compiler::IBuiltinVariable* LookupBuiltinVariable(const std::string& Name) override;
    bool LookupConstantDouble(const std::string& Name, double& Value) override;

  private:
    std::unordered_map<std::string, std::unique_ptr<Underanalyzer::Compiler::IBuiltinFunction>> _FunctionCache;
    std::unordered_map<std::string, std::unique_ptr<Underanalyzer::Compiler::IBuiltinVariable>> _VariableCache;
};

} // namespace GMSLib


// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Underanalyzer/IGlobalFunctions.h"

#include <string>
#include <unordered_map>

namespace GMSLib {

class GMSData;

class GlobalFunctions final : public Underanalyzer::IGlobalFunctions {
  public:
    explicit GlobalFunctions(GMSData& Data);

    bool FunctionNameExists(const std::string& Name) const override;
    bool FunctionExists(Underanalyzer::IGMFunction* Function) const override;
    bool TryGetFunction(const std::string& Name, Underanalyzer::IGMFunction*& OutFunction) const override;
    bool TryGetFunctionName(Underanalyzer::IGMFunction* Function, std::string& OutName) const override;
    void DefineFunction(const std::string& Name, Underanalyzer::IGMFunction* Function) override;
    void UndefineFunction(const std::string& Name, Underanalyzer::IGMFunction* Function) override;

  private:
    GMSData* _Data;
    std::unordered_map<std::string, Underanalyzer::IGMFunction*> _Defines;
};

} // namespace GMSLib

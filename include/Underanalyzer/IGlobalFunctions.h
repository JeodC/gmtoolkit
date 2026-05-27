
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <string>

namespace Underanalyzer {

class IGMFunction;

class IGlobalFunctions {
  public:
    virtual ~IGlobalFunctions() = default;
    virtual bool FunctionNameExists(const std::string& Name) const = 0;
    virtual bool FunctionExists(IGMFunction* Function) const = 0;
    virtual bool TryGetFunction(const std::string& Name, IGMFunction*& OutFunction) const = 0;
    virtual bool TryGetFunctionName(IGMFunction* Function, std::string& OutName) const = 0;
    virtual void DefineFunction(const std::string& Name, IGMFunction* Function) = 0;
    virtual void UndefineFunction(const std::string& Name, IGMFunction* Function) = 0;
};

} // namespace Underanalyzer

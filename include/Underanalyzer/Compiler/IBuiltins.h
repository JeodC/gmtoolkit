
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <string>

namespace Underanalyzer::Compiler {

class IBuiltinFunction;
class IBuiltinVariable;

class IBuiltins {
  public:
    virtual ~IBuiltins() = default;
    virtual IBuiltinFunction* LookupBuiltinFunction(const std::string& name) = 0;
    virtual IBuiltinVariable* LookupBuiltinVariable(const std::string& name) = 0;
    virtual bool LookupConstantDouble(const std::string& name, double& value) = 0;
};

class IBuiltinFunction {
  public:
    virtual ~IBuiltinFunction() = default;
    virtual const std::string& Name() const = 0;
    virtual int MinArguments() const = 0;
    virtual int MaxArguments() const = 0;
};

class IBuiltinVariable {
  public:
    virtual ~IBuiltinVariable() = default;
    virtual const std::string& Name() const = 0;
    virtual bool IsGlobal() const = 0;
    virtual bool IsAutomaticArray() const = 0;
    virtual bool CanSet() const = 0;
};

} // namespace Underanalyzer::Compiler

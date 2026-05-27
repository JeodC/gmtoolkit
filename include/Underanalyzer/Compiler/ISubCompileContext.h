
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

namespace Underanalyzer::Compiler {

class CompileContext;
class FunctionScope;

class ISubCompileContext {
  public:
    virtual ~ISubCompileContext() = default;
    virtual CompileContext& CompileContextRef() = 0;
    virtual FunctionScope& CurrentScope() = 0;
    virtual void SetCurrentScope(FunctionScope& scope) = 0;
    virtual FunctionScope& RootScope() = 0;
    virtual void SetRootScope(FunctionScope& scope) = 0;
};

} // namespace Underanalyzer::Compiler

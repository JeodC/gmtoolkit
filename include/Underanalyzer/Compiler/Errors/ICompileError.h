
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <string>

namespace Underanalyzer::Compiler::Errors {

class ICompileError {
  public:
    virtual ~ICompileError() = default;
    virtual const std::string& BaseMessage() const = 0;
    virtual std::string GenerateMessage() const = 0;
};

} // namespace Underanalyzer::Compiler::Errors

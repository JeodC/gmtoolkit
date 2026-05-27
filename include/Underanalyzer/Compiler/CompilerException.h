
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <stdexcept>
#include <string>

namespace Underanalyzer::Compiler {

class CompilerException : public std::runtime_error {
  public:
    explicit CompilerException(const std::string& message) : std::runtime_error(message) {
    }
};

} // namespace Underanalyzer::Compiler

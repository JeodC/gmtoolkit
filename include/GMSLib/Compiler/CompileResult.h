
// Source: github.com/UnderminersTeam/UndertaleModTool @ c4e5c2c3
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <string>
#include <vector>

namespace GMSLib {

class GMSCode;

class CompileError {
  public:
    GMSCode* CodeEntry = nullptr;
    std::string Message;

    CompileError() = default;
    CompileError(GMSCode* Code, std::string MessageIn) : CodeEntry(Code), Message(std::move(MessageIn)) {
    }
};

class CompileResult {
  public:
    bool Successful = false;
    std::vector<CompileError> Errors;

    static CompileResult SuccessfulResult() {
        CompileResult R;
        R.Successful = true;
        return R;
    }
    static CompileResult Failed(std::vector<CompileError> Errs) {
        CompileResult R;
        R.Successful = false;
        R.Errors = std::move(Errs);
        return R;
    }

    std::string PrintAllErrors(bool IncludeCodeEntryNames) const;

    CompileResult CombineWith(const CompileResult& Other) const {
        if (Successful && Other.Successful)
            return SuccessfulResult();
        std::vector<CompileError> Combined;
        Combined.reserve(Errors.size() + Other.Errors.size());
        Combined.insert(Combined.end(), Errors.begin(), Errors.end());
        Combined.insert(Combined.end(), Other.Errors.begin(), Other.Errors.end());
        return Failed(std::move(Combined));
    }
};

} // namespace GMSLib

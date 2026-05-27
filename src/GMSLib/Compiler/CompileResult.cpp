// SPDX-License-Identifier: GPL-3.0-or-later

#include "GMSLib/Compiler/CompileResult.h"

#include "GMSLib/Models/GMSCode.h"
#include "GMSLib/Models/GMSString.h"

namespace GMSLib {

std::string CompileResult::PrintAllErrors(bool IncludeCodeEntryNames) const {
    if (Errors.empty())
        return "(no errors)";
    std::string Out;
    for (const auto& E : Errors) {
        if (IncludeCodeEntryNames && E.CodeEntry != nullptr && E.CodeEntry->NameRef != nullptr) {
            Out += E.CodeEntry->NameRef->Content;
            Out += ": ";
        }
        Out += E.Message;
        Out += '\n';
    }
    return Out;
}

} // namespace GMSLib

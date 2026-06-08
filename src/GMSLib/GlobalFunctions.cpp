// SPDX-License-Identifier: GPL-3.0-or-later

#include "GMSLib/GlobalFunctions.h"

#include "GMSLib/GMSData.h"
#include "GMSLib/Models/GMSFunction.h"
#include "GMSLib/Models/GMSString.h"

namespace GMSLib {

// Every user-defined script function in GameMaker lives in FUNC under the
// "gml_Script_" name prefix; user-facing lookups use the short name.
namespace {
constexpr const char* kScriptPrefix = "gml_Script_";
constexpr std::size_t kScriptPrefixLen = 11;
} // namespace

GlobalFunctions::GlobalFunctions(GMSData& Data) : _Data(&Data) {
}

bool GlobalFunctions::FunctionNameExists(const std::string& Name) const {
    if (_Defines.find(Name) != _Defines.end())
        return true;
    if (_Data->FunctionByName.find("gml_Script_" + Name) != _Data->FunctionByName.end())
        return true;
    return _Data->FunctionByName.find(Name) != _Data->FunctionByName.end();
}

// "Exists as a global" means either explicitly defined via DefineFunction or
// owned by FUNC with the gml_Script_ prefix; sub-functions and anonymous
// helpers don't count even though they live in the same chunk.
bool GlobalFunctions::FunctionExists(Underanalyzer::IGMFunction* Function) const {
    for (const auto& [Name, F] : _Defines) {
        if (F == Function)
            return true;
    }
    const auto* GMSFn = static_cast<const GMSFunction*>(Function);
    if (GMSFn == nullptr || GMSFn->NameRef == nullptr)
        return false;
    const std::string& Full = GMSFn->NameRef->Content;
    if (Full.size() <= kScriptPrefixLen)
        return false;
    if (Full.compare(0, kScriptPrefixLen, kScriptPrefix) != 0)
        return false;
    auto It = _Data->FunctionByName.find(Full);
    return It != _Data->FunctionByName.end() && static_cast<Underanalyzer::IGMFunction*>(It->second) == Function;
}

// Explicit DefineFunction registrations win over the auto-discovered FUNC
// entry; this is how compile-time forward declarations shadow imported scripts.
bool GlobalFunctions::TryGetFunction(const std::string& Name, Underanalyzer::IGMFunction*& OutFunction) const {
    auto It = _Defines.find(Name);
    if (It != _Defines.end()) {
        OutFunction = It->second;
        return true;
    }
    auto It2 = _Data->FunctionByName.find("gml_Script_" + Name);
    if (It2 != _Data->FunctionByName.end()) {
        OutFunction = It2->second;
        return true;
    }
    // Plain-named runtime/built-in function already present in FUNC
    auto It3 = _Data->FunctionByName.find(Name);
    if (It3 != _Data->FunctionByName.end()) {
        OutFunction = It3->second;
        return true;
    }
    return false;
}

bool GlobalFunctions::TryGetFunctionName(Underanalyzer::IGMFunction* Function, std::string& OutName) const {
    for (const auto& [Name, F] : _Defines) {
        if (F == Function) {
            OutName = Name;
            return true;
        }
    }
    for (const auto& UP : _Data->Functions) {
        if (UP.get() == Function && UP->NameRef != nullptr) {
            const std::string& Full = UP->NameRef->Content;
            constexpr const char* Prefix = "gml_Script_";
            constexpr std::size_t PrefixLen = 11;
            if (Full.size() > PrefixLen && Full.compare(0, PrefixLen, Prefix) == 0) {
                OutName = Full.substr(PrefixLen);
                return true;
            }
        }
    }
    return false;
}

void GlobalFunctions::DefineFunction(const std::string& Name, Underanalyzer::IGMFunction* Function) {
    _Defines[Name] = Function;
}

void GlobalFunctions::UndefineFunction(const std::string& Name, Underanalyzer::IGMFunction* Function) {
    auto It = _Defines.find(Name);
    if (It != _Defines.end() && It->second == Function)
        _Defines.erase(It);
}

} // namespace GMSLib

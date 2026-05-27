// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "GMSLib/GMSChunks.h"
#include "GMSLib/Models/GMSCode.h"
#include "GMSLib/Models/GMSCodeLocals.h"
#include "GMSLib/Models/GMSFunction.h"
#include "GMSLib/Models/GMSGeneralInfo.h"
#include "GMSLib/Models/GMSGlobalInit.h"
#include "GMSLib/Models/GMSScript.h"
#include "GMSLib/Models/GMSString.h"
#include "GMSLib/Models/GMSVariable.h"
#include "Toolkit/Buffer.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace GMSLib {

class GMSData {
  public:
    Gmtoolkit::Buffer Buffer;
    std::unordered_map<ChunkTag, ChunkLocation> Chunks;
    std::vector<std::unique_ptr<GMSString>> Strings;
    std::vector<std::unique_ptr<GMSVariable>> Variables;
    std::vector<std::unique_ptr<GMSFunction>> Functions;
    std::vector<std::unique_ptr<GMSCode>> Code;
    std::vector<std::unique_ptr<GMSScript>> Scripts;
    std::vector<std::unique_ptr<GMSCodeLocals>> CodeLocals;
    std::vector<std::unique_ptr<GMSGlobalInit>> GlobalInits;
    GMSGeneralInfo GeneralInfo;
    std::unordered_map<std::string, GMSString*> StringByContent;
    std::unordered_map<std::string, GMSVariable*> VariableByName;
    std::unordered_map<std::string, GMSFunction*> FunctionByName;
    std::unordered_map<std::string, GMSCode*> CodeByName;
    std::unordered_map<std::string, GMSScript*> ScriptByName;
    // Counts as they were right after Load. Entries beyond these indices were
    // added in-memory (compile, mod) and SaveToFile must merge them into chunks.
    std::size_t OriginalStringCount = 0;
    std::size_t OriginalVariableCount = 0;
    std::size_t OriginalFunctionCount = 0;
    std::size_t OriginalScriptCount = 0;
    std::size_t OriginalCodeCount = 0;
    bool IsVersionAtLeast(int Major, int Minor = 0, int Release = 0, int Build = 0) const;
};

} // namespace GMSLib

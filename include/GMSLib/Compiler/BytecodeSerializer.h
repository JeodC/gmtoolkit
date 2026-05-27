// Source: github.com/UnderminersTeam/UndertaleModTool @ c4e5c2c3
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "GMSLib/Models/GMSCode.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Underanalyzer {
class IGMInstruction;
}

namespace GMSLib {

class GMSData;

class BytecodeSerializer {
  public:
    static void Serialize(const std::vector<Underanalyzer::IGMInstruction*>& Instructions, const GMSData& Data,
                          bool Bytecode14OrLower, std::vector<std::uint8_t>& OutBytecode,
                          std::vector<PendingVarRefSlot>& OutVarRefs, std::vector<PendingFuncRefSlot>& OutFuncRefs,
                          std::vector<PendingStringRefSlot>& OutStringRefs);
};

} // namespace GMSLib

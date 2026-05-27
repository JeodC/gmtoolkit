
// Source: github.com/UnderminersTeam/UndertaleModTool @ c4e5c2c3
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Toolkit/Version.h"

#include <cstdint>
#include <string>

namespace GMSLib {

class GMSString;

class GMSGeneralInfo {
  public:
    std::uint8_t IsDebuggerDisabled = 0;
    std::uint8_t BytecodeVersion = 17;
    std::uint16_t Unknown = 0;
    GMSString* Filename = nullptr;
    GMSString* Config = nullptr;
    std::uint32_t LastObj = 0;
    std::uint32_t LastTile = 0;
    std::uint32_t GameID = 0;
    Gmtoolkit::Version Version{};
};

} // namespace GMSLib

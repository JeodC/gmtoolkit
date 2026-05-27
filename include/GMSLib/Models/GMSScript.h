
// Source: github.com/UnderminersTeam/UndertaleModTool @ c4e5c2c3
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

namespace GMSLib {

class GMSString;
class GMSCode;

class GMSScript {
  public:
    GMSString* NameRef = nullptr;
    GMSCode* CodeRef = nullptr;
    bool IsConstructor = false;
    std::int32_t RawCodeId = -1;
};

} // namespace GMSLib

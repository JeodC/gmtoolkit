// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <vector>

namespace GMSLib {

class GMSString;

class GMSCodeLocals {
  public:
    GMSString* NameRef = nullptr;

    struct LocalVar {
        std::uint32_t Index = 0;
        GMSString* NameRef = nullptr;
    };
    std::vector<LocalVar> Locals;
};

} // namespace GMSLib

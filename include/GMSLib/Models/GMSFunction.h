
// Source: github.com/UnderminersTeam/UndertaleModTool @ c4e5c2c3
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Underanalyzer/VMData.h"

#include <cstdint>

namespace GMSLib {

class GMSString;

class GMSFunction final : public Underanalyzer::IGMFunction {
  public:
    GMSString* NameRef = nullptr;
    std::int32_t NameStringID = 0;
    std::uint32_t Occurrences = 0;
    std::uint32_t FirstAddress = 0;

    Underanalyzer::IGMString* Name() const override {
        return reinterpret_cast<Underanalyzer::IGMString*>(NameRef);
    }
};

} // namespace GMSLib

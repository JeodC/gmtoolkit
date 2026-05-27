
// Source: github.com/UnderminersTeam/UndertaleModTool @ c4e5c2c3
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Underanalyzer/VMData.h"

#include <cstdint>

namespace GMSLib {

class GMSString;

class GMSVariable final : public Underanalyzer::IGMVariable {
  public:
    GMSString* NameRef = nullptr;
    Underanalyzer::IGMInstruction::InstanceType InstType = Underanalyzer::IGMInstruction::InstanceType::Self;
    std::int32_t VarID = 0;
    std::uint32_t Occurrences = 0;
    std::uint32_t FirstAddress = 0;
    std::int32_t NameStringID = 0;

    Underanalyzer::IGMString* Name() const override {
        return reinterpret_cast<Underanalyzer::IGMString*>(NameRef);
    }
    Underanalyzer::IGMInstruction::InstanceType InstanceType() const override {
        return InstType;
    }
    int VariableID() const override {
        return VarID;
    }
};

} // namespace GMSLib

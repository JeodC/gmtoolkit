// Source: github.com/UnderminersTeam/UndertaleModTool @ c4e5c2c3
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Underanalyzer/VMData.h"

#include <cstdint>
#include <string>

namespace GMSLib {

class GMSString;
class GMSVariable;
class GMSFunction;

class GMSInstruction final : public Underanalyzer::IGMInstruction {
  public:
    Underanalyzer::IGMInstruction::Opcode KindValue = Underanalyzer::IGMInstruction::Opcode::Push;
    Underanalyzer::IGMInstruction::ExtendedOpcode ExtKindValue =
        Underanalyzer::IGMInstruction::ExtendedOpcode::PushArrayFinal;
    Underanalyzer::IGMInstruction::ComparisonType ComparisonKindValue =
        Underanalyzer::IGMInstruction::ComparisonType::EqualTo;
    Underanalyzer::IGMInstruction::DataType Type1Value = Underanalyzer::IGMInstruction::DataType::Double;
    Underanalyzer::IGMInstruction::DataType Type2Value = Underanalyzer::IGMInstruction::DataType::Double;
    Underanalyzer::IGMInstruction::InstanceType InstTypeValue = Underanalyzer::IGMInstruction::InstanceType::Self;
    Underanalyzer::IGMInstruction::VariableType ReferenceVarTypeValue =
        Underanalyzer::IGMInstruction::VariableType::Normal;

    double ValueDoubleVal = 0;
    std::int64_t ValueLongVal = 0;
    std::int32_t ValueIntVal = 0;
    std::int16_t ValueShortVal = 0;
    GMSString* ValueStringRef = nullptr;
    GMSVariable* ValueVariableRef = nullptr;
    GMSFunction* ValueFunctionRef = nullptr;

    std::int32_t BranchOffsetVal = 0;
    bool PopWithContextExitVal = false;
    std::uint8_t DuplicationSizeVal = 0;
    std::uint8_t DuplicationSize2Val = 0;
    std::uint16_t ArgumentCountVal = 0;
    std::uint8_t PopSwapSizeVal = 0;
    std::int32_t AssetReferenceIdVal = 0;

    Opcode Kind() const override {
        return KindValue;
    }
    ExtendedOpcode ExtKind() const override {
        return ExtKindValue;
    }
    ComparisonType ComparisonKind() const override {
        return ComparisonKindValue;
    }
    DataType Type1() const override {
        return Type1Value;
    }
    DataType Type2() const override {
        return Type2Value;
    }
    InstanceType InstType() const override {
        return InstTypeValue;
    }
    Underanalyzer::IGMVariable* ResolvedVariable() const override {
        return reinterpret_cast<Underanalyzer::IGMVariable*>(ValueVariableRef);
    }
    Underanalyzer::IGMFunction* ResolvedFunction() const override {
        return reinterpret_cast<Underanalyzer::IGMFunction*>(ValueFunctionRef);
    }
    VariableType ReferenceVarType() const override {
        return ReferenceVarTypeValue;
    }
    double ValueDouble() const override {
        return ValueDoubleVal;
    }
    std::int16_t ValueShort() const override {
        return ValueShortVal;
    }
    std::int32_t ValueInt() const override {
        return ValueIntVal;
    }
    std::int64_t ValueLong() const override {
        return ValueLongVal;
    }
    Underanalyzer::IGMString* ValueString() const override {
        return reinterpret_cast<Underanalyzer::IGMString*>(ValueStringRef);
    }
    std::int32_t BranchOffset() const override {
        return BranchOffsetVal;
    }
    bool PopWithContextExit() const override {
        return PopWithContextExitVal;
    }
    std::uint8_t DuplicationSize() const override {
        return DuplicationSizeVal;
    }
    std::uint8_t DuplicationSize2() const override {
        return DuplicationSize2Val;
    }
    int ArgumentCount() const override {
        return ArgumentCountVal;
    }
    int PopSwapSize() const override {
        return PopSwapSizeVal;
    }
    std::int32_t AssetReferenceId() const override {
        return AssetReferenceIdVal;
    }
    Underanalyzer::AssetType GetAssetReferenceType(Underanalyzer::IGameContext&) const override {
        return static_cast<Underanalyzer::AssetType>(AssetReferenceIdVal & 0xF);
    }
    Underanalyzer::IGMVariable* TryFindVariable(Underanalyzer::IGameContext*) const override {
        return ResolvedVariable();
    }
    Underanalyzer::IGMFunction* TryFindFunction(Underanalyzer::IGameContext*) const override {
        return ResolvedFunction();
    }
};

} // namespace GMSLib

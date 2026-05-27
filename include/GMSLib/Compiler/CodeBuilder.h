
// Source: github.com/UnderminersTeam/UndertaleModTool @ c4e5c2c3
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Underanalyzer/Compiler/ICodeBuilder.h"

#include <string>
#include <unordered_map>
#include <utility>

namespace Underanalyzer::Compiler {
class NodeArena;
}

namespace GMSLib {

class CompileGroup;
class GMSData;
class GMSGameContext;
class GMSString;
class GMSVariable;
class GMSFunction;
class GlobalFunctions;

class CodeBuilder final : public Underanalyzer::Compiler::ICodeBuilder {
  public:
    CodeBuilder(GMSGameContext& GameContext, GMSData& Data, GlobalFunctions& Globals);

    void SetCurrentArena(Underanalyzer::Compiler::NodeArena* Arena) {
        _CurrentArena = Arena;
    }
    void SetCurrentGroup(CompileGroup* Group) {
        _CurrentGroup = Group;
    }

    Underanalyzer::IGMInstruction* CreateInstruction(int Address,
                                                     Underanalyzer::IGMInstruction::Opcode Opcode) override;
    Underanalyzer::IGMInstruction* CreateInstruction(int Address, Underanalyzer::IGMInstruction::Opcode Opcode,
                                                     Underanalyzer::IGMInstruction::DataType DataType) override;
    Underanalyzer::IGMInstruction* CreateInstruction(int Address, Underanalyzer::IGMInstruction::Opcode Opcode,
                                                     Underanalyzer::IGMInstruction::DataType DataType1,
                                                     Underanalyzer::IGMInstruction::DataType DataType2) override;
    Underanalyzer::IGMInstruction* CreateInstruction(int Address, Underanalyzer::IGMInstruction::Opcode Opcode,
                                                     std::int16_t Value,
                                                     Underanalyzer::IGMInstruction::DataType DataType1,
                                                     Underanalyzer::IGMInstruction::DataType DataType2) override;
    Underanalyzer::IGMInstruction* CreateInstruction(int Address, Underanalyzer::IGMInstruction::Opcode Opcode,
                                                     std::int32_t Value,
                                                     Underanalyzer::IGMInstruction::DataType DataType1,
                                                     Underanalyzer::IGMInstruction::DataType DataType2) override;
    Underanalyzer::IGMInstruction* CreateInstruction(int Address, Underanalyzer::IGMInstruction::Opcode Opcode,
                                                     std::int64_t Value,
                                                     Underanalyzer::IGMInstruction::DataType DataType1,
                                                     Underanalyzer::IGMInstruction::DataType DataType2) override;
    Underanalyzer::IGMInstruction* CreateInstruction(int Address, Underanalyzer::IGMInstruction::Opcode Opcode,
                                                     double Value, Underanalyzer::IGMInstruction::DataType DataType1,
                                                     Underanalyzer::IGMInstruction::DataType DataType2) override;
    Underanalyzer::IGMInstruction* CreateInstruction(int Address, Underanalyzer::IGMInstruction::Opcode Opcode,
                                                     Underanalyzer::IGMInstruction::ComparisonType ComparisonType,
                                                     Underanalyzer::IGMInstruction::DataType DataType1,
                                                     Underanalyzer::IGMInstruction::DataType DataType2) override;
    Underanalyzer::IGMInstruction*
    CreateInstruction(int Address, Underanalyzer::IGMInstruction::ExtendedOpcode ExtendedOpcode) override;
    Underanalyzer::IGMInstruction*
    CreateInstruction(int Address, Underanalyzer::IGMInstruction::ExtendedOpcode ExtendedOpcode, int Value) override;

    Underanalyzer::IGMInstruction* CreateDuplicateInstruction(int Address,
                                                              Underanalyzer::IGMInstruction::DataType DataType,
                                                              std::uint8_t DuplicationSize) override;
    Underanalyzer::IGMInstruction* CreateDupSwapInstruction(int Address,
                                                            Underanalyzer::IGMInstruction::DataType DataType,
                                                            std::uint8_t DuplicationSize,
                                                            std::uint8_t DuplicationSize2) override;
    Underanalyzer::IGMInstruction* CreatePopSwapInstruction(int Address, std::uint8_t SwapSize) override;
    Underanalyzer::IGMInstruction* CreateWithExitInstruction(int Address) override;
    Underanalyzer::IGMInstruction* CreateCallInstruction(int Address, int ArgumentCount) override;
    Underanalyzer::IGMInstruction* CreateCallVariableInstruction(int Address, int ArgumentCount) override;

    void PatchInstruction(Underanalyzer::IGMInstruction* Instruction, const std::string& VariableName,
                          Underanalyzer::IGMInstruction::InstanceType VariableInstanceType,
                          Underanalyzer::IGMInstruction::InstanceType InstructionInstanceType,
                          Underanalyzer::IGMInstruction::VariableType VariableType, bool IsBuiltin,
                          bool KeepInstanceType) override;
    void PatchInstruction(Underanalyzer::IGMInstruction* Instruction, Underanalyzer::Compiler::FunctionScope& Scope,
                          const std::string& FunctionName,
                          Underanalyzer::Compiler::IBuiltinFunction* BuiltinFunction) override;
    void PatchInstruction(Underanalyzer::IGMInstruction* Instruction,
                          Underanalyzer::Compiler::Bytecode::FunctionEntry& FunctionEntry) override;
    void PatchInstruction(Underanalyzer::IGMInstruction* Instruction, const std::string& StringContent) override;
    void PatchInstruction(Underanalyzer::IGMInstruction* Instruction, int Value) override;

    bool IsGlobalFunctionName(const std::string& Name) override;
    int GenerateTryVariableID(int InternalIndex) override;
    std::int64_t GenerateArrayOwnerID(const std::string* VariableName, std::int64_t FunctionIndex, bool IsDot) override;
    void OnParseNameIdentifier(const std::string& Name) override;

  private:
    Underanalyzer::IGMInstruction::Opcode MapOpcode(Underanalyzer::IGMInstruction::Opcode Opcode) const;

    GMSGameContext* _GameContext;
    GMSData* _Data;
    GlobalFunctions* _Globals;
    Underanalyzer::Compiler::NodeArena* _CurrentArena = nullptr;
    CompileGroup* _CurrentGroup = nullptr;

    // Same name can appear under multiple instance types (Self, Global,
    // Builtin, ...) and each gets its own VARI row, so the lookup key is the
    // (name, instance-type) pair, mixed via the 64-bit golden ratio constant.
    struct _NameInstKeyHash {
        std::size_t operator()(const std::pair<std::string, std::int16_t>& P) const noexcept {
            std::size_t H = std::hash<std::string>{}(P.first);
            return H ^ (static_cast<std::size_t>(static_cast<std::uint16_t>(P.second)) * 0x9E3779B97F4A7C15ULL);
        }
    };
    std::unordered_map<std::pair<std::string, std::int16_t>, GMSVariable*, _NameInstKeyHash> _VariableLookup;
};

} // namespace GMSLib

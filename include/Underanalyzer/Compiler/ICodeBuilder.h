
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/VMData.h"

#include <string>

namespace Underanalyzer::Compiler {

class FunctionScope;
class IBuiltinFunction;
namespace Bytecode {
class FunctionEntry;
}

class ICodeBuilder {
  public:
    virtual ~ICodeBuilder() = default;

    virtual IGMInstruction* CreateInstruction(int address, IGMInstruction::Opcode opcode) = 0;
    virtual IGMInstruction* CreateInstruction(int address, IGMInstruction::Opcode opcode,
                                              IGMInstruction::DataType dataType) = 0;
    virtual IGMInstruction* CreateInstruction(int address, IGMInstruction::Opcode opcode,
                                              IGMInstruction::DataType dataType1,
                                              IGMInstruction::DataType dataType2) = 0;
    virtual IGMInstruction* CreateInstruction(int address, IGMInstruction::Opcode opcode, int16_t value,
                                              IGMInstruction::DataType dataType1,
                                              IGMInstruction::DataType dataType2) = 0;
    virtual IGMInstruction* CreateInstruction(int address, IGMInstruction::Opcode opcode, int32_t value,
                                              IGMInstruction::DataType dataType1,
                                              IGMInstruction::DataType dataType2) = 0;
    virtual IGMInstruction* CreateInstruction(int address, IGMInstruction::Opcode opcode, int64_t value,
                                              IGMInstruction::DataType dataType1,
                                              IGMInstruction::DataType dataType2) = 0;
    virtual IGMInstruction* CreateInstruction(int address, IGMInstruction::Opcode opcode, double value,
                                              IGMInstruction::DataType dataType1,
                                              IGMInstruction::DataType dataType2) = 0;
    virtual IGMInstruction* CreateInstruction(int address, IGMInstruction::Opcode opcode,
                                              IGMInstruction::ComparisonType comparisonType,
                                              IGMInstruction::DataType dataType1,
                                              IGMInstruction::DataType dataType2) = 0;
    virtual IGMInstruction* CreateInstruction(int address, IGMInstruction::ExtendedOpcode extendedOpcode) = 0;
    virtual IGMInstruction* CreateInstruction(int address, IGMInstruction::ExtendedOpcode extendedOpcode,
                                              int value) = 0;

    virtual IGMInstruction* CreateDuplicateInstruction(int address, IGMInstruction::DataType dataType,
                                                       uint8_t duplicationSize) = 0;
    virtual IGMInstruction* CreateDupSwapInstruction(int address, IGMInstruction::DataType dataType,
                                                     uint8_t duplicationSize, uint8_t duplicationSize2) = 0;
    virtual IGMInstruction* CreatePopSwapInstruction(int address, uint8_t swapSize) = 0;
    virtual IGMInstruction* CreateWithExitInstruction(int address) = 0;
    virtual IGMInstruction* CreateCallInstruction(int address, int argumentCount) = 0;
    virtual IGMInstruction* CreateCallVariableInstruction(int address, int argumentCount) = 0;

    virtual void PatchInstruction(IGMInstruction* instruction, const std::string& variableName,
                                  IGMInstruction::InstanceType variableInstanceType,
                                  IGMInstruction::InstanceType instructionInstanceType,
                                  IGMInstruction::VariableType variableType, bool isBuiltin, bool keepInstanceType) = 0;
    virtual void PatchInstruction(IGMInstruction* instruction, FunctionScope& scope, const std::string& functionName,
                                  IBuiltinFunction* builtinFunction) = 0;
    virtual void PatchInstruction(IGMInstruction* instruction, Bytecode::FunctionEntry& functionEntry) = 0;
    virtual void PatchInstruction(IGMInstruction* instruction, const std::string& stringContent) = 0;
    virtual void PatchInstruction(IGMInstruction* instruction, int value) = 0;

    virtual bool IsGlobalFunctionName(const std::string& name) = 0;
    virtual int GenerateTryVariableID(int internalIndex) = 0;
    virtual int64_t GenerateArrayOwnerID(const std::string* variableName, int64_t functionIndex, bool isDot) = 0;
    virtual void OnParseNameIdentifier(const std::string& name) = 0;
};

} // namespace Underanalyzer::Compiler

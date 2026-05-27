
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <cstdint>
#include <string>

namespace Underanalyzer {

class IGameContext;
class IGMVariable;
class IGMFunction;
class IGMString;
enum class AssetType;

class IGMInstruction {
  public:
    virtual ~IGMInstruction() = default;

    // Opcode values match the GameMaker VM's wire format byte-for-byte; do not renumber.
    enum class Opcode : uint8_t {
        Convert = 0x07,
        Multiply = 0x08,
        Divide = 0x09,
        GMLDivRemainder = 0x0A,
        GMLModulo = 0x0B,
        Add = 0x0C,
        Subtract = 0x0D,
        And = 0x0E,
        Or = 0x0F,
        Xor = 0x10,
        Negate = 0x11,
        Not = 0x12,
        ShiftLeft = 0x13,
        ShiftRight = 0x14,
        Compare = 0x15,
        Pop = 0x45,
        Duplicate = 0x86,
        Return = 0x9C,
        Exit = 0x9D,
        PopDelete = 0x9E,
        Branch = 0xB6,
        BranchTrue = 0xB7,
        BranchFalse = 0xB8,
        PushWithContext = 0xBA,
        PopWithContext = 0xBB,
        Push = 0xC0,
        PushLocal = 0xC1,
        PushGlobal = 0xC2,
        PushBuiltin = 0xC3,
        PushImmediate = 0x84,
        Call = 0xD9,
        CallVariable = 0x99,
        Extended = 0xFF
    };

    // Extended opcodes ride on the Opcode::Extended byte; negative numbers distinguish
    // them from the regular opcode space when both are stored in the same field.
    enum class ExtendedOpcode : int16_t {
        CheckArrayIndex = -1,
        PushArrayFinal = -2,
        PopArrayFinal = -3,
        PushArrayContainer = -4,
        SetArrayOwner = -5,
        HasStaticInitialized = -6,
        SetStaticInitialized = -7,
        SaveArrayReference = -8,
        RestoreArrayReference = -9,
        IsNullishValue = -10,
        PushReference = -11
    };

    enum class ComparisonType : uint8_t {
        LesserThan = 1,
        LesserEqualThan = 2,
        EqualTo = 3,
        NotEqualTo = 4,
        GreaterEqualThan = 5,
        GreaterThan = 6
    };

    enum class DataType : uint8_t {
        Double = 0,
        Int32 = 2,
        Int64 = 3,
        Boolean = 4,
        Variable = 5,
        String = 6,
        Int16 = 15
    };

    // Negative ids are sentinels (self/other/all/etc.); positive ids are object asset
    // ids. Room instance ids are positive too, distinguished elsewhere via a flag.
    enum class InstanceType : int16_t {
        Self = -1,
        Other = -2,
        All = -3,
        Noone = -4,
        Global = -5,
        Builtin = -6,
        Local = -7,
        StackTop = -9,
        Argument = -15,
        Static = -16
    };

    enum class VariableType : uint8_t {
        Array = 0,
        StackTop = 0x80,
        Normal = 0xA0,
        Instance = 0xE0,
        MultiPush = 0x10,
        MultiPushPop = 0x90
    };

    virtual Opcode Kind() const = 0;
    virtual ExtendedOpcode ExtKind() const = 0;
    virtual ComparisonType ComparisonKind() const = 0;
    virtual DataType Type1() const = 0;
    virtual DataType Type2() const = 0;
    virtual InstanceType InstType() const = 0;
    virtual IGMVariable* ResolvedVariable() const = 0;
    virtual IGMFunction* ResolvedFunction() const = 0;
    virtual VariableType ReferenceVarType() const = 0;
    virtual double ValueDouble() const = 0;
    virtual int16_t ValueShort() const = 0;
    virtual int32_t ValueInt() const = 0;
    virtual int64_t ValueLong() const = 0;
    virtual IGMString* ValueString() const = 0;
    virtual int32_t BranchOffset() const = 0;
    virtual bool PopWithContextExit() const = 0;
    virtual uint8_t DuplicationSize() const = 0;
    virtual uint8_t DuplicationSize2() const = 0;
    virtual int ArgumentCount() const = 0;
    virtual int PopSwapSize() const = 0;
    virtual int AssetReferenceId() const = 0;
    virtual AssetType GetAssetReferenceType(IGameContext& context) const = 0;
    virtual IGMVariable* TryFindVariable(IGameContext* context) const = 0;
    virtual IGMFunction* TryFindFunction(IGameContext* context) const = 0;

    static int GetSize(const IGMInstruction& instr);
};

class IGMVariable {
  public:
    virtual ~IGMVariable() = default;
    virtual IGMString* Name() const = 0;
    virtual IGMInstruction::InstanceType InstanceType() const = 0;
    virtual int VariableID() const = 0;
};

class IGMFunction {
  public:
    virtual ~IGMFunction() = default;
    virtual IGMString* Name() const = 0;
};

class IGMString {
  public:
    virtual ~IGMString() = default;

    virtual const std::string& Text() const = 0;
};

namespace VMDataTypeExtensions {
IGMInstruction::DataType BinaryResultWith(IGMInstruction::DataType type1, IGMInstruction::Opcode opcode,
                                          IGMInstruction::DataType type2);
}

} // namespace Underanalyzer

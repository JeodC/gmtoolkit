
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/VMData.h"

#include <algorithm>
#include <stdexcept>

namespace Underanalyzer {

// Instruction size in bytes. Pop/Call carry a 4-byte variable or function reference;
// Push carries inline data sized to its DataType; Extended with Int32 carries an int arg.
int IGMInstruction::GetSize(const IGMInstruction& instr) {
    switch (instr.Kind()) {
        case Opcode::Pop:
            if (instr.Type1() != DataType::Int16)
                return 8;
            break;
        case Opcode::Call:
            return 8;
        case Opcode::Push:
        case Opcode::PushLocal:
        case Opcode::PushGlobal:
        case Opcode::PushBuiltin:
        case Opcode::PushImmediate:
            switch (instr.Type1()) {
                case DataType::Double:
                case DataType::Int64:
                    return 12;
                case DataType::Int16:
                    return 4;
                default:
                    return 8;
            }
        case Opcode::Extended:
            if (instr.Type1() == DataType::Int32)
                return 8;
            break;
        default:
            break;
    }
    return 4;
}

namespace VMDataTypeExtensions {

// Bias ranks how much room a value occupies on the VM stack: 32-bit slots, 64-bit slots,
// or the wider Variable slot. Used to decide which operand's type wins in a binary op.
static int StackTypeBias(IGMInstruction::DataType type) {
    using DT = IGMInstruction::DataType;
    switch (type) {
        case DT::Int32:
        case DT::Boolean:
        case DT::String:
            return 0;
        case DT::Double:
        case DT::Int64:
            return 1;
        case DT::Variable:
            return 2;
        default:
            throw std::runtime_error("Unknown data type");
    }
}

IGMInstruction::DataType BinaryResultWith(IGMInstruction::DataType type1, IGMInstruction::Opcode opcode,
                                          IGMInstruction::DataType type2) {
    using Op = IGMInstruction::Opcode;
    using DT = IGMInstruction::DataType;
    switch (opcode) {
        case Op::Subtract:
        case Op::Divide:
        case Op::GMLModulo:
        case Op::And:
        case Op::Or:
        case Op::Xor:
        case Op::ShiftLeft:
        case Op::ShiftRight:
            // These ops never produce a string; a string operand is coerced numerically.
            if (type1 == DT::String || type2 == DT::String)
                return DT::Double;
            break;
        case Op::GMLDivRemainder:
            if ((type1 == DT::String && type2 != DT::Variable) || type2 == DT::String)
                return DT::Double;
            break;
        case Op::Compare:
            return DT::Boolean;
        default:
            break;
    }
    int bias1 = StackTypeBias(type1);
    int bias2 = StackTypeBias(type2);
    if (bias1 == bias2) {
        // Same stack width: the lower DataType enum value wins (matches VM's promotion table).
        return static_cast<DT>(std::min(static_cast<uint8_t>(type1), static_cast<uint8_t>(type2)));
    }
    return bias1 > bias2 ? type1 : type2;
}

} // namespace VMDataTypeExtensions
} // namespace Underanalyzer

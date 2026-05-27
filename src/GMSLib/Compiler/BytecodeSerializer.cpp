
// Source: github.com/UnderminersTeam/UndertaleModTool @ c4e5c2c3
// SPDX-License-Identifier: GPL-3.0-or-later

#include "GMSLib/Compiler/BytecodeSerializer.h"

#include "GMSLib/GMSData.h"
#include "GMSLib/Models/GMSFunction.h"
#include "GMSLib/Models/GMSInstruction.h"
#include "GMSLib/Models/GMSString.h"
#include "GMSLib/Models/GMSVariable.h"
#include "Underanalyzer/IGameContext.h"

#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace GMSLib {

namespace {

using Op = Underanalyzer::IGMInstruction::Opcode;
using DT = Underanalyzer::IGMInstruction::DataType;
using IT = Underanalyzer::IGMInstruction::InstanceType;
using VT = Underanalyzer::IGMInstruction::VariableType;
using CT = Underanalyzer::IGMInstruction::ComparisonType;
using Ext = Underanalyzer::IGMInstruction::ExtendedOpcode;

enum class InstructionType {
    SingleType,
    DoubleType,
    Comparison,
    Goto,
    Pop,
    Push,
    Call,
    Break,
};

InstructionType ClassifyOpcode(Op K) {
    switch (K) {
        case Op::Negate:
        case Op::Not:
        case Op::Duplicate:
        case Op::Return:
        case Op::Exit:
        case Op::PopDelete:
        case Op::CallVariable:
            return InstructionType::SingleType;
        case Op::Convert:
        case Op::Multiply:
        case Op::Divide:
        case Op::GMLDivRemainder:
        case Op::GMLModulo:
        case Op::Add:
        case Op::Subtract:
        case Op::And:
        case Op::Or:
        case Op::Xor:
        case Op::ShiftLeft:
        case Op::ShiftRight:
            return InstructionType::DoubleType;
        case Op::Compare:
            return InstructionType::Comparison;
        case Op::Branch:
        case Op::BranchTrue:
        case Op::BranchFalse:
        case Op::PushWithContext:
        case Op::PopWithContext:
            return InstructionType::Goto;
        case Op::Pop:
            return InstructionType::Pop;
        case Op::Push:
        case Op::PushLocal:
        case Op::PushGlobal:
        case Op::PushBuiltin:
        case Op::PushImmediate:
            return InstructionType::Push;
        case Op::Call:
            return InstructionType::Call;
        case Op::Extended:
            return InstructionType::Break;
    }
    throw std::runtime_error("BytecodeSerializer: unknown opcode");
}

// Bytecode 14 used a different opcode numbering; the in-memory model speaks
// the modern encoding, so on emit we translate back. 0x15 (Compare) is special
// because its old encoding folds the comparison subtype into the opcode byte.
std::uint8_t ConvertNewKindToOldKind(std::uint8_t Kind, CT ComparisonKind = static_cast<CT>(0)) {
    switch (Kind) {
        case 0x07:
            return 0x03;
        case 0x08:
            return 0x04;
        case 0x09:
            return 0x05;
        case 0x0A:
            return 0x06;
        case 0x0B:
            return 0x07;
        case 0x0C:
            return 0x08;
        case 0x0D:
            return 0x09;
        case 0x0E:
            return 0x0A;
        case 0x0F:
            return 0x0B;
        case 0x10:
            return 0x0C;
        case 0x11:
            return 0x0D;
        case 0x12:
            return 0x0E;
        case 0x13:
            return 0x0F;
        case 0x14:
            return 0x10;
        case 0x15:
            return static_cast<std::uint8_t>(static_cast<std::uint8_t>(ComparisonKind) + 0x10);
        case 0x45:
            return 0x41;
        case 0x84:
            return 0xC0;
        case 0x86:
            return 0x82;
        case 0x9C:
            return 0x9D;
        case 0x9D:
            return 0x9E;
        case 0x9E:
            return 0x9F;
        case 0xB6:
            return 0xB7;
        case 0xB7:
            return 0xB8;
        case 0xB8:
            return 0xB9;
        case 0xBA:
            return 0xBB;
        case 0xBB:
            return 0xBC;
        case 0xD9:
            return 0xDA;
        case 0xC1:
            return 0xC0;
        case 0xC2:
            return 0xC0;
        case 0xC3:
            return 0xC0;
        default:
            return Kind;
    }
}

inline void PutU32(std::vector<std::uint8_t>& B, std::uint32_t W) {
    B.push_back(static_cast<std::uint8_t>(W & 0xFF));
    B.push_back(static_cast<std::uint8_t>((W >> 8) & 0xFF));
    B.push_back(static_cast<std::uint8_t>((W >> 16) & 0xFF));
    B.push_back(static_cast<std::uint8_t>((W >> 24) & 0xFF));
}
inline void PutU64(std::vector<std::uint8_t>& B, std::uint64_t W) {
    PutU32(B, static_cast<std::uint32_t>(W & 0xFFFFFFFFu));
    PutU32(B, static_cast<std::uint32_t>(W >> 32));
}

// Every instruction starts with a 4-byte header: [opcode | t2 | t1 | payload].
// The payload bits depend on the instruction class, which ClassifyOpcode picks.
std::uint32_t BuildFirstWord(const Underanalyzer::IGMInstruction& I) {
    std::uint8_t Kind = static_cast<std::uint8_t>(I.Kind());
    std::uint8_t T1 = static_cast<std::uint8_t>(I.Type1()) & 0x0F;
    std::uint8_t T2 = static_cast<std::uint8_t>(I.Type2()) & 0x0F;
    std::uint32_t W = (static_cast<std::uint32_t>(Kind) << 24) | (static_cast<std::uint32_t>(T2) << 20) |
                      (static_cast<std::uint32_t>(T1) << 16);

    switch (ClassifyOpcode(I.Kind())) {
        case InstructionType::SingleType: {

            if (I.Kind() == Op::Duplicate) {
                std::uint8_t E = I.DuplicationSize();
                std::uint8_t E2 = I.DuplicationSize2();
                W |= (static_cast<std::uint32_t>(E2) << 8) | static_cast<std::uint32_t>(E);
            } else if (I.Kind() == Op::CallVariable) {
                W |= static_cast<std::uint32_t>(I.ArgumentCount()) & 0xFFFFu;
            }
            break;
        }
        case InstructionType::DoubleType:
            break;
        case InstructionType::Comparison:
            W |= (static_cast<std::uint32_t>(I.ComparisonKind()) & 0xFFu) << 8;
            break;
        case InstructionType::Goto: {
            std::int32_t J = I.BranchOffset();
            // PopWithContext used as a "with"-block exit is encoded with the
            // sentinel offset 0xF00000 rather than a real branch displacement.
            if (I.Kind() == Op::PopWithContext && I.PopWithContextExit()) {
                W |= 0xF00000u;
            } else {
                W |= static_cast<std::uint32_t>(J) & 0x00FFFFFFu;
            }
            break;
        }
        case InstructionType::Pop: {
            // Type1==Int16 marks a PopSwap variant whose payload is the swap
            // size; otherwise the low word carries the variable instance type.
            if (I.Type1() == DT::Int16) {
                W |= static_cast<std::uint32_t>(I.PopSwapSize()) & 0xFFFFu;
            } else {
                W |= static_cast<std::uint32_t>(static_cast<std::int16_t>(I.InstType())) & 0xFFFFu;
            }
            break;
        }
        case InstructionType::Push: {
            // push.i16 inlines the value in the low word; push.v stashes the
            // variable instance type there and emits the var ref in word 2.
            if (I.Type1() == DT::Int16) {
                W |= static_cast<std::uint32_t>(I.ValueShort()) & 0xFFFFu;
            } else if (I.Type1() == DT::Variable) {
                W |= static_cast<std::uint32_t>(static_cast<std::int16_t>(I.InstType())) & 0xFFFFu;
            }
            break;
        }
        case InstructionType::Call:

            W |= static_cast<std::uint32_t>(I.ArgumentCount()) & 0xFFFFu;
            break;
        case InstructionType::Break:

            W |= static_cast<std::uint32_t>(static_cast<std::int16_t>(I.ExtKind())) & 0xFFFFu;
            break;
    }
    return W;
}

// 2024.4 reshuffled the on-disk AssetType numbering (Script/Font/Timeline
// shifted, Background moved to 13). The compiler-internal enum is stable, so
// translate on emit using the per-version table that matches the target.
std::uint32_t AdaptAssetTypeId(const GMSData& Data, Underanalyzer::AssetType Type) {
    using AT = Underanalyzer::AssetType;
    if (Data.IsVersionAtLeast(2024, 4)) {
        switch (Type) {
            case AT::Object:
                return 0;
            case AT::Sprite:
                return 1;
            case AT::Sound:
                return 2;
            case AT::Room:
                return 3;
            case AT::Path:
                return 4;
            case AT::Script:
                return 5;
            case AT::Font:
                return 6;
            case AT::Timeline:
                return 7;
            case AT::Shader:
                return 8;
            case AT::Sequence:
                return 9;
            case AT::AnimCurve:
                return 10;
            case AT::ParticleSystem:
                return 11;
            case AT::Background:
                return 13;
            case AT::RoomInstance:
                return 14;
        }
    } else {
        switch (Type) {
            case AT::Object:
                return 0;
            case AT::Sprite:
                return 1;
            case AT::Sound:
                return 2;
            case AT::Room:
                return 3;
            case AT::Background:
                return 4;
            case AT::Path:
                return 5;
            case AT::Script:
                return 6;
            case AT::Font:
                return 7;
            case AT::Timeline:
                return 8;
            case AT::Shader:
                return 10;
            case AT::Sequence:
                return 11;
            case AT::AnimCurve:
                return 12;
            case AT::ParticleSystem:
                return 13;
            case AT::RoomInstance:
                return 14;
        }
    }
    throw std::runtime_error("BytecodeSerializer::AdaptAssetTypeId: unknown asset type");
}

// Bytecode 15+ widened the goto offset and shuffled the sign bit from 0x800000
// to 0x400000; the 0xF00000 "with"-exit sentinel must be left alone.
void ApplyGotoBytecode15PlusFixup(std::uint32_t& W) {
    if ((W & 0xFFFFFFu) != 0xF00000u && (W & 0x800000u) != 0) {
        W &= ~0x800000u;
        W |= 0x400000u;
    }
}

} // namespace

void BytecodeSerializer::Serialize(const std::vector<Underanalyzer::IGMInstruction*>& Instructions, const GMSData& Data,
                                   bool Bytecode14OrLower, std::vector<std::uint8_t>& OutBytecode,
                                   std::vector<PendingVarRefSlot>& OutVarRefs,
                                   std::vector<PendingFuncRefSlot>& OutFuncRefs,
                                   std::vector<PendingStringRefSlot>& OutStringRefs) {
    OutBytecode.clear();
    OutVarRefs.clear();
    OutFuncRefs.clear();
    OutStringRefs.clear();

    for (Underanalyzer::IGMInstruction* IPtr : Instructions) {
        const Underanalyzer::IGMInstruction& I = *IPtr;
        std::uint32_t First = BuildFirstWord(I);
        InstructionType Kind = ClassifyOpcode(I.Kind());

        if (Bytecode14OrLower) {
            std::uint8_t NewOp = static_cast<std::uint8_t>(I.Kind());
            std::uint8_t OldOp = (Kind == InstructionType::Comparison)
                                     ? ConvertNewKindToOldKind(NewOp, I.ComparisonKind())
                                     : ConvertNewKindToOldKind(NewOp);
            First = (First & 0x00FFFFFFu) | (static_cast<std::uint32_t>(OldOp) << 24);
            if (Kind == InstructionType::Comparison) {
                First &= ~0x0000FF00u;
            }
        } else if (Kind == InstructionType::Goto) {
            ApplyGotoBytecode15PlusFixup(First);
        }

        PutU32(OutBytecode, First);

        switch (Kind) {
            case InstructionType::SingleType:
            case InstructionType::DoubleType:
            case InstructionType::Comparison:
            case InstructionType::Goto:
                break;

            case InstructionType::Pop: {
                // PopSwap variants have no operand word; everything else writes
                // a packed (ReferenceVarType<<27 | name-string-id) tag here.
                if (I.Type1() == DT::Int16)
                    break;
                std::size_t Off = OutBytecode.size();
                const GMSVariable* V = static_cast<const GMSVariable*>(I.ResolvedVariable());
                std::uint32_t NameSid = V != nullptr ? static_cast<std::uint32_t>(V->NameStringID) : 0xDEADu;
                std::uint32_t RefTypeBits = (static_cast<std::uint32_t>(I.ReferenceVarType()) & 0xF8u) << 24;
                PutU32(OutBytecode, RefTypeBits | (NameSid & 0x07FFFFFFu));
                // Record the slot so the save backend can rewrite the
                // string-id once the final VARI table is laid out.
                OutVarRefs.push_back({ Off, const_cast<GMSVariable*>(V) });
                break;
            }

            case InstructionType::Push: {
                switch (I.Type1()) {
                    case DT::Double: {
                        std::uint64_t Bits;
                        double D = I.ValueDouble();
                        std::memcpy(&Bits, &D, sizeof(Bits));
                        PutU64(OutBytecode, Bits);
                        break;
                    }
                    case DT::Int64:
                        PutU64(OutBytecode, static_cast<std::uint64_t>(I.ValueLong()));
                        break;
                    case DT::Int32: {
                        // push.i can carry a function ref, a variable ref, or a
                        // literal; the compiler tells us which by populating the
                        // matching resolver hook.
                        std::size_t Off = OutBytecode.size();
                        if (auto* F = static_cast<const GMSFunction*>(I.ResolvedFunction())) {
                            std::uint32_t NameSid = static_cast<std::uint32_t>(F->NameStringID);
                            PutU32(OutBytecode, NameSid & 0x07FFFFFFu);
                            OutFuncRefs.push_back({ Off, const_cast<GMSFunction*>(F) });
                        } else if (auto* V = static_cast<const GMSVariable*>(I.ResolvedVariable())) {
                            std::uint32_t NameSid = static_cast<std::uint32_t>(V->NameStringID);
                            PutU32(OutBytecode, NameSid & 0x07FFFFFFu);
                            OutVarRefs.push_back({ Off, const_cast<GMSVariable*>(V) });
                        } else {
                            PutU32(OutBytecode, static_cast<std::uint32_t>(I.ValueInt()));
                        }
                        break;
                    }
                    case DT::Variable: {
                        std::size_t Off = OutBytecode.size();
                        const GMSVariable* V = static_cast<const GMSVariable*>(I.ResolvedVariable());
                        std::uint32_t NameSid = V != nullptr ? static_cast<std::uint32_t>(V->NameStringID) : 0xDEADu;
                        std::uint32_t RefTypeBits = (static_cast<std::uint32_t>(I.ReferenceVarType()) & 0xF8u) << 24;
                        PutU32(OutBytecode, RefTypeBits | (NameSid & 0x07FFFFFFu));
                        OutVarRefs.push_back({ Off, const_cast<GMSVariable*>(V) });
                        break;
                    }
                    case DT::String: {
                        // push.s writes the STRG index directly (not a string
                        // pointer); CompileGroup must have interned by now.
                        std::size_t Off = OutBytecode.size();
                        GMSString* S = static_cast<GMSString*>(I.ValueString());
                        std::int32_t Id = S != nullptr ? S->Id : -1;
                        if (Id < 0)
                            throw std::runtime_error("BytecodeSerializer: push.s string has no Id");
                        PutU32(OutBytecode, static_cast<std::uint32_t>(Id));
                        OutStringRefs.push_back({ Off, S });
                        break;
                    }
                    case DT::Int16:
                        break;
                    default:
                        throw std::runtime_error("BytecodeSerializer: bad push type");
                }
                break;
            }

            case InstructionType::Call: {
                std::size_t Off = OutBytecode.size();
                const GMSFunction* F = static_cast<const GMSFunction*>(I.ResolvedFunction());
                std::uint32_t NameSid = F != nullptr ? static_cast<std::uint32_t>(F->NameStringID) : 0xDEADu;
                PutU32(OutBytecode, NameSid & 0x07FFFFFFu);
                OutFuncRefs.push_back({ Off, const_cast<GMSFunction*>(F) });
                break;
            }

            case InstructionType::Break: {
                // Only the Int32 variant of Extended carries an operand word;
                // when it references a function the AssetType tag goes in the
                // top byte so the runtime treats it as a script asset id.
                if (I.Type1() != DT::Int32)
                    break;
                std::size_t Off = OutBytecode.size();
                if (auto* F = static_cast<const GMSFunction*>(I.ResolvedFunction())) {
                    std::uint32_t NameSid = static_cast<std::uint32_t>(F->NameStringID);
                    std::uint32_t AssetTag = AdaptAssetTypeId(Data, Underanalyzer::AssetType::Script) << 24;
                    PutU32(OutBytecode, (NameSid & 0x07FFFFFFu) | AssetTag);
                    OutFuncRefs.push_back({ Off, const_cast<GMSFunction*>(F) });
                } else {
                    PutU32(OutBytecode, static_cast<std::uint32_t>(I.ValueInt()));
                }
                break;
            }
        }
    }
}

} // namespace GMSLib

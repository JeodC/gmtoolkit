
// Source: github.com/UnderminersTeam/UndertaleModTool @ c4e5c2c3
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace GMSLib {

class GMSString;
class GMSVariable;
class GMSFunction;

struct PendingVarRefSlot {
    std::size_t ByteOffset;
    GMSVariable* Target;
};
struct PendingFuncRefSlot {
    std::size_t ByteOffset;
    GMSFunction* Target;
};
struct PendingStringRefSlot {
    std::size_t ByteOffset;
    GMSString* Target;
};

class GMSCode {
  public:
    GMSString* NameRef = nullptr;
    std::uint32_t Length = 0;
    std::uint16_t LocalsCount = 0;
    std::uint16_t ArgumentsCount = 0;
    std::int32_t BytecodeRelativeAddress = 0;
    std::uint32_t Offset = 0;
    std::size_t BytecodeAbsoluteAddress = 0;

    GMSCode* ParentEntry = nullptr;
    std::vector<GMSCode*> ChildEntries;
    // True when this entry was recompiled and PendingBytecode is the payload.
    // PendingBytecode.empty() alone cannot mark it: a comment-only script
    // legitimately compiles to zero instructions and must still replace.
    bool PendingReplace = false;
    std::vector<std::uint8_t> PendingBytecode;
    std::vector<PendingVarRefSlot> PendingVarRefs;
    std::vector<PendingFuncRefSlot> PendingFuncRefs;
    std::vector<PendingStringRefSlot> PendingStringRefs;
    std::vector<std::uint16_t> PendingChildLocalsCounts;
    std::vector<std::uint16_t> PendingChildArgumentsCounts;
    std::vector<std::string> PendingLocalNames;
    std::unordered_map<std::string, std::vector<std::string>> PendingChildLocalNames;
};

} // namespace GMSLib

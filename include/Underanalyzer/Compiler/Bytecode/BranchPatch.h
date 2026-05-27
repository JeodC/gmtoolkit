
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/VMData.h"

#include <utility>
#include <vector>

namespace Underanalyzer::Compiler::Bytecode {

class BytecodeContext;

class IMultiBranchPatch {
  public:
    virtual ~IMultiBranchPatch() = default;
    virtual void AddInstruction(BytecodeContext& context, IGMInstruction* instruction) = 0;
};

class MultiForwardBranchPatch final : public IMultiBranchPatch {
  public:
    MultiForwardBranchPatch() {
        _instructions.reserve(4);
    }

    bool Used() const {
        return !_instructions.empty();
    }
    int NumberUsed() const {
        return static_cast<int>(_instructions.size());
    }

    void AddInstruction(BytecodeContext& context, IGMInstruction* instruction) override;
    void Patch(BytecodeContext& context);

  private:
    std::vector<std::pair<IGMInstruction*, int>> _instructions;
};

class MultiBackwardBranchPatch final : public IMultiBranchPatch {
  public:
    explicit MultiBackwardBranchPatch(BytecodeContext& context);
    void AddInstruction(BytecodeContext& context, IGMInstruction* instruction) override;

  private:
    int _destAddress;
};

class MultiBackwardBranchPatchTracked final : public IMultiBranchPatch {
  public:
    explicit MultiBackwardBranchPatchTracked(BytecodeContext& context);

    bool Used() const {
        return _numberUsed > 0;
    }
    int NumberUsed() const {
        return _numberUsed;
    }

    void AddInstruction(BytecodeContext& context, IGMInstruction* instruction) override;

  private:
    int _destAddress;
    int _numberUsed = 0;
};

class SingleForwardBranchPatch {
  public:
    SingleForwardBranchPatch(BytecodeContext& context, IGMInstruction* instruction);
    void Patch(BytecodeContext& context);

  private:
    IGMInstruction* _instruction;
    int _startAddress;
};

} // namespace Underanalyzer::Compiler::Bytecode

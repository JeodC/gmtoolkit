
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/VMData.h"

namespace Underanalyzer::Compiler::Bytecode {

class BytecodeContext;
class IMultiBranchPatch;

class IControlFlowContext {
  public:
    virtual ~IControlFlowContext() = default;
    virtual bool RequiresCleanup() const = 0;
    virtual bool IsLoop() const = 0;
    virtual bool CanContinueBeUsed() const = 0;
    virtual void SetCanContinueBeUsed(bool value) = 0;
    virtual void GenerateCleanupCode(BytecodeContext& context) = 0;
    virtual void UseBreak(BytecodeContext& context, IGMInstruction* instruction) = 0;
    virtual void UseContinue(BytecodeContext& context, IGMInstruction* instruction) = 0;
};

class LoopContext : public IControlFlowContext {
  public:
    LoopContext(IMultiBranchPatch* breakPatch, IMultiBranchPatch* continuePatch)
        : _breakPatch(breakPatch), _continuePatch(continuePatch) {
    }

    bool IsLoop() const override {
        return true;
    }
    bool CanContinueBeUsed() const override {
        return _canContinueBeUsed;
    }
    void SetCanContinueBeUsed(bool v) override {
        _canContinueBeUsed = v;
    }

    void UseBreak(BytecodeContext& context, IGMInstruction* instruction) override;
    void UseContinue(BytecodeContext& context, IGMInstruction* instruction) override;

  protected:
    IMultiBranchPatch* _breakPatch;
    IMultiBranchPatch* _continuePatch;
    bool _canContinueBeUsed = true;
};

class BasicLoopContext final : public LoopContext {
  public:
    using LoopContext::LoopContext;
    bool RequiresCleanup() const override {
        return false;
    }
    void GenerateCleanupCode(BytecodeContext&) override {
    }
};

class WithLoopContext final : public LoopContext {
  public:
    using LoopContext::LoopContext;
    bool RequiresCleanup() const override {
        return true;
    }
    void GenerateCleanupCode(BytecodeContext& context) override;
};

class RepeatLoopContext final : public LoopContext {
  public:
    using LoopContext::LoopContext;
    bool RequiresCleanup() const override {
        return true;
    }
    void GenerateCleanupCode(BytecodeContext& context) override;
};

class SwitchContext final : public IControlFlowContext {
  public:
    SwitchContext(IGMInstruction::DataType expressionType, IMultiBranchPatch* breakPatch,
                  IMultiBranchPatch* continuePatch)
        : _expressionType(expressionType), _breakPatch(breakPatch), _continuePatch(continuePatch) {
    }

    bool RequiresCleanup() const override {
        return true;
    }
    bool IsLoop() const override {
        return false;
    }
    bool CanContinueBeUsed() const override {
        return _canContinueBeUsed;
    }
    void SetCanContinueBeUsed(bool v) override {
        _canContinueBeUsed = v;
    }

    void GenerateCleanupCode(BytecodeContext& context) override;
    void UseBreak(BytecodeContext& context, IGMInstruction* instruction) override;
    void UseContinue(BytecodeContext& context, IGMInstruction* instruction) override;

  private:
    IGMInstruction::DataType _expressionType;
    IMultiBranchPatch* _breakPatch;
    IMultiBranchPatch* _continuePatch;
    bool _canContinueBeUsed = true;
};

} // namespace Underanalyzer::Compiler::Bytecode

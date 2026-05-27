
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/Compiler/Bytecode/InstructionPatch.h"
#include "Underanalyzer/Compiler/ISubCompileContext.h"
#include "Underanalyzer/Compiler/NodeArena.h"
#include "Underanalyzer/VMData.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace Underanalyzer {
class IGameContext;
}
namespace Underanalyzer::Compiler {
class CompileContext;
class FunctionScope;
class ICodeBuilder;
} // namespace Underanalyzer::Compiler
namespace Underanalyzer::Compiler::Nodes {
class IASTNode;
}

namespace Underanalyzer::Compiler::Bytecode {

class FunctionEntry;
class IControlFlowContext;

class BytecodeContext final : public ISubCompileContext {
  public:
    BytecodeContext(CompileContext& context, Nodes::IASTNode* rootNode, FunctionScope& rootScope,
                    const std::unordered_set<std::string>* localGlobalFunctions);

    CompileContext& CompileContextRef() override {
        return _compileContext;
    }
    FunctionScope& CurrentScope() override {
        return *_currentScope;
    }
    void SetCurrentScope(FunctionScope& scope) override {
        _currentScope = &scope;
    }
    FunctionScope& RootScope() override {
        return *_rootScope;
    }
    void SetRootScope(FunctionScope& scope) override {
        _rootScope = &scope;
    }

    NodeArena& Arena() {
        return _arena;
    }
    template <class T, class... Args> T* Make(Args&&... args) {
        return _arena.New<T>(std::forward<Args>(args)...);
    }

    Nodes::IASTNode* RootNode() const {
        return _rootNode;
    }
    std::vector<IGMInstruction*>& Instructions() {
        return _instructions;
    }
    std::vector<FunctionEntry*>& FunctionEntries() {
        return _functionEntries;
    }
    FunctionEntry* CurrentFunctionEntry() const {
        return _currentFunctionEntry;
    }
    void SetCurrentFunctionEntry(FunctionEntry* fe) {
        _currentFunctionEntry = fe;
    }
    InstructionPatches& Patches() {
        return _patches;
    }
    int Position() const {
        return _position;
    }

    const std::string* FunctionCallBeforeExit() const {
        return _functionCallBeforeExit.has_value() ? &*_functionCallBeforeExit : nullptr;
    }
    void SetFunctionCallBeforeExit(std::optional<std::string> v) {
        _functionCallBeforeExit = std::move(v);
    }

    int64_t LastFunctionID() const {
        return _lastFunctionID;
    }
    void SetLastFunctionID(int64_t v) {
        _lastFunctionID = v;
    }
    int64_t LastArrayOwnerID() const {
        return _lastArrayOwnerID;
    }
    void SetLastArrayOwnerID(int64_t v) {
        _lastArrayOwnerID = v;
    }
    bool CanGenerateArrayOwners() const {
        return _canGenerateArrayOwners;
    }
    void SetCanGenerateArrayOwners(bool v) {
        _canGenerateArrayOwners = v;
    }

    void GenerateCode(int initialPosition);
    static void PatchInstructions(CompileContext& context, InstructionPatches& patches);

    IGMInstruction* Emit(IGMInstruction::Opcode opcode);
    IGMInstruction* Emit(IGMInstruction::Opcode opcode, IGMInstruction::DataType dataType);
    IGMInstruction* Emit(IGMInstruction::Opcode opcode, IGMInstruction::DataType dataType1,
                         IGMInstruction::DataType dataType2);
    IGMInstruction* Emit(IGMInstruction::Opcode opcode, int16_t value, IGMInstruction::DataType dataType1,
                         IGMInstruction::DataType dataType2 = IGMInstruction::DataType::Double);
    IGMInstruction* Emit(IGMInstruction::Opcode opcode, int32_t value, IGMInstruction::DataType dataType1,
                         IGMInstruction::DataType dataType2 = IGMInstruction::DataType::Double);
    IGMInstruction* Emit(IGMInstruction::Opcode opcode, int64_t value, IGMInstruction::DataType dataType1,
                         IGMInstruction::DataType dataType2 = IGMInstruction::DataType::Double);
    IGMInstruction* Emit(IGMInstruction::Opcode opcode, double value, IGMInstruction::DataType dataType1,
                         IGMInstruction::DataType dataType2 = IGMInstruction::DataType::Double);
    IGMInstruction* Emit(IGMInstruction::Opcode opcode, IGMInstruction::ComparisonType comparisonType,
                         IGMInstruction::DataType dataType1, IGMInstruction::DataType dataType2);
    IGMInstruction* Emit(IGMInstruction::ExtendedOpcode extendedOpcode);
    IGMInstruction* Emit(IGMInstruction::ExtendedOpcode extendedOpcode, LocalFunctionPatch function);
    IGMInstruction* Emit(IGMInstruction::ExtendedOpcode extendedOpcode, FunctionPatch function);
    IGMInstruction* Emit(IGMInstruction::ExtendedOpcode extendedOpcode, int extendedValue);
    IGMInstruction* EmitDuplicate(IGMInstruction::DataType dataType, uint8_t duplicationSize);
    IGMInstruction* EmitDupSwap(IGMInstruction::DataType dataType, uint8_t duplicationSize, uint8_t duplicationSize2);
    IGMInstruction* EmitPopSwap(uint8_t swapSize);
    IGMInstruction* EmitPopWithExit();
    IGMInstruction* Emit(IGMInstruction::Opcode opcode, VariablePatch variable, IGMInstruction::DataType dataType1,
                         IGMInstruction::DataType dataType2 = IGMInstruction::DataType::Double);
    IGMInstruction* Emit(IGMInstruction::Opcode opcode, StructVariablePatch variable,
                         IGMInstruction::DataType dataType1,
                         IGMInstruction::DataType dataType2 = IGMInstruction::DataType::Double);
    IGMInstruction* EmitPushFunction(FunctionPatch function);
    IGMInstruction* EmitPushFunction(LocalFunctionPatch function);
    IGMInstruction* EmitCall(FunctionPatch function, int argumentCount);
    IGMInstruction* EmitCallVariable(int argumentCount);
    IGMInstruction* Emit(IGMInstruction::Opcode opcode, StringPatch stringPatch, IGMInstruction::DataType dataType1,
                         IGMInstruction::DataType dataType2 = IGMInstruction::DataType::Double);

    void PatchBranch(IGMInstruction* instruction, int branchOffset);
    void PatchPush(IGMInstruction* instruction, int value);

    void PushDataType(IGMInstruction::DataType dataType);
    IGMInstruction::DataType PeekDataType();
    IGMInstruction::DataType PopDataType();
    bool ConvertDataType(IGMInstruction::DataType destDataType);

    enum class InstanceConversionType { None, Int32, StacktopId };
    InstanceConversionType ConvertToInstanceId();

    bool DoAnyControlFlowRequireCleanup();
    void GenerateControlFlowCleanup();
    bool IsGlobalFunctionName(const std::string& name);
    void PushControlFlowContext(IControlFlowContext* context);
    void PopControlFlowContext();
    bool AnyControlFlowContexts();
    bool AnyLoopContexts();
    IControlFlowContext* GetTopControlFlowContext();
    bool IsFunctionDeclaredInCurrentScope(const std::string& name);
    int64_t GenerateArrayOwnerID(const std::string* variableName, int64_t functionId, bool isDot);

  private:
    CompileContext& _compileContext;
    NodeArena& _arena;
    Nodes::IASTNode* _rootNode;
    FunctionScope* _currentScope;
    FunctionScope* _rootScope;

    std::vector<IGMInstruction*> _instructions;
    std::vector<FunctionEntry*> _functionEntries;
    FunctionEntry* _currentFunctionEntry = nullptr;
    InstructionPatches _patches;
    int _position = 0;
    std::optional<std::string> _functionCallBeforeExit;
    int64_t _lastFunctionID = 1;
    int64_t _lastArrayOwnerID = -1;
    bool _canGenerateArrayOwners = false;

    std::vector<IGMInstruction::DataType> _dataTypeStack;
    ICodeBuilder* _codeBuilder;
    IGameContext* _gameContext;
};

} // namespace Underanalyzer::Compiler::Bytecode

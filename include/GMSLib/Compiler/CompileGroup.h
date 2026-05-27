
// Source: github.com/UnderminersTeam/UndertaleModTool @ c4e5c2c3
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "GMSLib/Compiler/CompileResult.h"
#include "Underanalyzer/Compiler/CompileContext.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Underanalyzer::Compiler::Bytecode {
class FunctionEntry;
}

namespace GMSLib {

class GMSData;
class GMSCode;
class GMSString;
class GMSFunction;
class GMSScript;
class GMSGameContext;
class GMSInstruction;

class CompileGroup {
  public:
    explicit CompileGroup(GMSGameContext& GameContextIn);

    void QueueCodeReplace(GMSCode* CodeToModify, std::string GmlCode);

    void QueueCodeReplace(const std::string& CodeEntryName, std::string GmlCode);

    CompileResult Compile();

    GMSGameContext& GlobalContext() const {
        return *_GameContext;
    }

    // Hook for hosts that want to fence GMSData mutations onto a specific
    // thread (e.g. UI work). Default runs inline. Deviation: upstream UTMT
    // dispatches to its WPF dispatcher; the C++ port leaves it pluggable.
    using MainThreadActionFn = std::function<void(std::function<void()>)>;
    MainThreadActionFn MainThreadAction = [](std::function<void()> F) { F(); };

    // Set true to keep the linking maps across Compile() calls when batching
    // several imports against a known-stable GMSData snapshot.
    bool PersistLinkingLookups = false;

    GMSString* MakeString(const std::string& Content, int* OutId);
    void RegisterLocalVariable(GMSInstruction* Reference, const std::string& Name);
    void RegisterNonLocalVariable(const std::string& Name);
    int RegisterName(const std::string& Name);
    GMSFunction* EnsureFunctionDefined(const std::string& FunctionName);
    int CurrentCodeEntryNameHash() const {
        return _CurrentCodeEntryNameHash;
    }
    int NextTryVariableID();

  private:
    struct QueuedOperation {
        GMSCode* CodeEntry;
        std::string GmlCode;
    };

    struct CodeEntryNameGroup {
        std::deque<GMSCode*> RemainingOriginalEntries;
        int EntriesUsed = 0;
    };

    struct ChildCodeEntryData {
        std::string Name;
        Underanalyzer::Compiler::Bytecode::FunctionEntry* FunctionEntry;
        GMSCode* Code;
        GMSScript* Script;
        GMSFunction* Function;
        bool ExistingCode;
        bool ExistingScript;
        bool ExistingFunction;
    };

    void InitializeLinkingLookups();

    std::vector<ChildCodeEntryData>
    ResolveFunctionEntries(GMSCode* RootCode, const std::string& RootCodeName,
                           Underanalyzer::Compiler::CompileScriptKind ScriptKindIn,
                           const std::vector<Underanalyzer::Compiler::Bytecode::FunctionEntry*>& Entries,
                           std::unordered_map<std::string, CodeEntryNameGroup>& OutRemainingChildren);

    void CommitChildEntries(GMSCode* RootCode, std::unordered_map<std::string, CodeEntryNameGroup>& RemainingChildren,
                            std::vector<ChildCodeEntryData>& ChildData, int OutputLength);

    static bool SimilarCodeEntryNames(const std::string& OriginalName, const std::string& NewNameNoNumbers);
    static std::string FindOriginalShortName(const std::string& OriginalName, const std::string& NewShortNameNoNumbers);

    int SeedTryVariableIndexFromScript(GMSCode* Script) const;

    GMSGameContext* _GameContext;
    GMSData* _Data;
    std::vector<QueuedOperation> _QueuedCodeReplacements;

    int _CurrentCodeEntryNameHash = 0;
    int _NextTryVariableIndex = 0;
    int _NextNameId = 100000;
    std::unordered_map<std::string, int> _ParsedNameIds;
    std::unordered_map<std::string, int> _LinkingStringIdLookup;
    std::unordered_map<std::string, GMSFunction*> _LinkingFunctionLookup;
    std::unordered_map<std::string, std::vector<GMSScript*>> _LinkingScriptLookup;
    int _LinkingStructCounter = -1;
    std::vector<std::pair<std::string, bool>> _LinkingVariableOrder;
    std::unordered_map<std::string, int> _LinkingVariableOrderLookup;
    std::unordered_map<std::string, std::vector<GMSInstruction*>> _LinkingLocalReferences;
    std::vector<std::unique_ptr<GMSCode>> _PendingChildCodeOwnership;
    std::vector<std::unique_ptr<GMSScript>> _PendingChildScriptOwnership;
};

} // namespace GMSLib

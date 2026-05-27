
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <string_view>
#include <unordered_set>

namespace Underanalyzer {

// Names of compiler-internal runtime helpers. The doubled '@' sigil is reserved by
// GameMaker for these; userland code cannot define an identifier in that form.
class VMConstants {
  public:
    static constexpr std::string_view TryHookFunction = "@@try_hook@@";
    static constexpr std::string_view TryUnhookFunction = "@@try_unhook@@";
    static constexpr std::string_view FinishCatchFunction = "@@finish_catch@@";
    static constexpr std::string_view FinishFinallyFunction = "@@finish_finally@@";

    static constexpr std::string_view MethodFunction = "method";
    static constexpr std::string_view NullObjectFunction = "@@NullObject@@";
    static constexpr std::string_view NewObjectFunction = "@@NewGMLObject@@";

    static constexpr std::string_view CopyStaticFunction = "@@CopyStatic@@";
    static constexpr std::string_view SetStaticFunction = "@@SetStatic@@";

    static constexpr std::string_view SelfFunction = "@@This@@";
    static constexpr std::string_view OtherFunction = "@@Other@@";
    static constexpr std::string_view GlobalFunction = "@@Global@@";
    static constexpr std::string_view GetInstanceFunction = "@@GetInstance@@";

    static constexpr int OldArrayLimit = 32000;

    static constexpr std::string_view NewArrayFunction = "@@NewGMLArray@@";
    static constexpr std::string_view TempReturnVariable = "$$$$temp$$$$";
    static constexpr std::string_view ThrowFunction = "@@throw@@";

    static constexpr std::string_view TryBreakVariable = "__yy_breakEx";
    static constexpr std::string_view TryContinueVariable = "__yy_continueEx";
    static constexpr std::string_view TryCopyVariable = "copyVar";

    static constexpr std::string_view StructGetFromHashFunction = "struct_get_from_hash";

    static constexpr std::string_view ChooseFunction = "choose";
    static constexpr std::string_view ScriptExecuteFunction = "script_execute";

    static constexpr std::string_view StaticGetFunction = "static_get";

    static const std::unordered_set<std::string_view>& BuiltinArrayVariables();
};

} // namespace Underanalyzer

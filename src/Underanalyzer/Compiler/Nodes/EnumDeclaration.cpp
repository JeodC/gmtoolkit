
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/EnumDeclaration.h"

#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/Lexer/Token.h"
#include "Underanalyzer/Compiler/Nodes/IASTNode.h"
#include "Underanalyzer/Compiler/Nodes/Int64Node.h"
#include "Underanalyzer/Compiler/Nodes/NumberNode.h"
#include "Underanalyzer/Compiler/Parser/Expressions.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"
#include "Underanalyzer/GMEnum.h"

namespace Underanalyzer::Compiler::Nodes {

using namespace Lexer;
using namespace Parser;

EnumDeclaration::EnumDeclaration(std::string name, std::vector<std::string> valueNames,
                                 std::unordered_map<std::string, IASTNode*> values)
    : Name(std::move(name)), ValueNames(std::move(valueNames)), Values(std::move(values)) {
    IntegerValues.reserve(Values.size());
}

void EnumDeclaration::Parse(ParseContext& context) {
    if (context.EnsureToken(KeywordKind::Enum) == nullptr)
        return;

    if (context.EndOfCode()) {
        context.CompileContextRef().PushError("Unexpected end of code (expected name for enum)");
        return;
    }
    IToken* tokenEnumName = context.Tokens()[context.Position()];
    context.SetPosition(context.Position() + 1);
    auto* tokenEnumNameVar = As<TokenVariable>(tokenEnumName);
    if (tokenEnumNameVar == nullptr) {
        context.CompileContextRef().PushError("Expected name for enum", tokenEnumName);
        return;
    }

    const std::string& enumNameStr = tokenEnumNameVar->Text;
    if (tokenEnumNameVar->BuiltinVariable != nullptr) {
        context.CompileContextRef().PushError("Declaring enum name over builtin variable '" + enumNameStr + "'",
                                              tokenEnumNameVar);
        return;
    }

    bool shouldAdd = true;
    if (context.CompileContextRef().Enums().find(enumNameStr) != context.CompileContextRef().Enums().end() ||
        context.ParseEnums().find(enumNameStr) != context.ParseEnums().end()) {
        context.CompileContextRef().PushError("Enum name '" + enumNameStr + "' declared more than once",
                                              tokenEnumNameVar);
        shouldAdd = false;
    }

    if (context.EnsureToken(SeparatorKind::BlockOpen, KeywordKind::Begin) == nullptr)
        return;

    std::vector<std::string> valueNames;
    valueNames.reserve(16);
    std::unordered_map<std::string, IASTNode*> values;
    values.reserve(16);

    while (!context.EndOfCode() && !context.IsCurrentToken(SeparatorKind::BlockClose)) {
        IToken* valueName = context.Tokens()[context.Position()];
        context.SetPosition(context.Position() + 1);
        auto* valueNameVar = As<TokenVariable>(valueName);
        if (valueNameVar == nullptr) {
            context.CompileContextRef().PushError("Expected name for enum value", valueName);
            return;
        }

        bool shouldAddValue = true;
        const std::string& valueNameStr = valueNameVar->Text;
        if (values.find(valueNameStr) != values.end()) {
            context.CompileContextRef().PushError("Duplicate enum value name '" + valueNameStr + "'", valueNameVar);
            shouldAddValue = false;
        }

        IASTNode* value = nullptr;
        if (context.IsCurrentToken(OperatorKind::Assign) || context.IsCurrentToken(OperatorKind::Assign2)) {
            context.SetPosition(context.Position() + 1);
            value = Expressions::ParseExpression(context);
        }

        if (shouldAddValue) {
            valueNames.push_back(valueNameStr);
            values[valueNameStr] = value;
        }

        if (context.IsCurrentToken(SeparatorKind::Comma)) {
            context.SetPosition(context.Position() + 1);
        } else if (!context.IsCurrentToken(SeparatorKind::BlockClose, KeywordKind::End)) {
            break;
        }
    }

    if (context.EnsureToken(SeparatorKind::BlockClose) == nullptr)
        return;

    if (shouldAdd) {
        auto* decl = context.Make<EnumDeclaration>(enumNameStr, std::move(valueNames), std::move(values));
        context.ParseEnums()[enumNameStr] = decl;
        context.ParseEnumOrder().push_back(decl);
    }
}

// Two-pass resolution: first pass folds the obvious cases (literal/auto-increment);
// second pass retries the unresolved entries now that earlier members are known,
// so a value can reference any sibling regardless of source order.
void EnumDeclaration::ResolveValues(ParseContext& context) {
    for (auto* decl : context.ParseEnumOrder()) {
        int64_t currentValue = 0;
        bool currentValueValid = true;
        for (const std::string& vname : decl->ValueNames) {
            IASTNode* expr = decl->Values[vname];
            if (expr != nullptr) {
                IASTNode* processed = expr->PostProcess(context);
                decl->Values[vname] = processed;
                if (auto* n = dynamic_cast<NumberNode*>(processed)) {
                    currentValue = static_cast<int64_t>(n->Value);
                    currentValueValid = true;
                } else if (auto* n = dynamic_cast<Int64Node*>(processed)) {
                    currentValue = n->Value;
                    currentValueValid = true;
                } else {
                    currentValue = 0;
                    currentValueValid = false;
                }
            }
            if (currentValueValid) {
                decl->IntegerValues[vname] = currentValue;
                currentValue++;
            }
        }
    }

    for (auto* decl : context.ParseEnumOrder()) {
        int64_t currentValue = 0;
        bool currentValueValid = true;
        for (const std::string& vname : decl->ValueNames) {
            auto resolvedIt = decl->IntegerValues.find(vname);
            if (resolvedIt != decl->IntegerValues.end()) {
                currentValue = resolvedIt->second + 1;
                currentValueValid = true;
                continue;
            }
            IASTNode* expr = decl->Values[vname];
            if (expr != nullptr) {
                IASTNode* processed = expr->PostProcess(context);
                decl->Values[vname] = processed;
                if (auto* n = dynamic_cast<NumberNode*>(processed)) {
                    currentValue = static_cast<int64_t>(n->Value);
                    currentValueValid = true;
                } else if (auto* n = dynamic_cast<Int64Node*>(processed)) {
                    currentValue = n->Value;
                    currentValueValid = true;
                } else {
                    currentValue = 0;
                    currentValueValid = false;
                }
            }
            if (currentValueValid) {
                decl->IntegerValues[vname] = currentValue;
                currentValue++;
            } else {
                context.CompileContextRef().PushError("Failed to resolve enum value '" + decl->Name + "." + vname +
                                                          "' to a constant integer value",
                                                      nullptr);
                currentValue = 0;
                currentValueValid = true;
            }
        }
    }

    for (auto* decl : context.ParseEnumOrder()) {
        std::vector<std::shared_ptr<GMEnumValue>> values;
        values.reserve(decl->ValueNames.size());
        for (const std::string& vname : decl->ValueNames) {
            int64_t v = 0;
            auto it = decl->IntegerValues.find(vname);
            if (it != decl->IntegerValues.end())
                v = it->second;
            values.push_back(std::make_shared<GMEnumValue>(vname, v));
        }
        context.CompileContextRef().Enums()[decl->Name] = std::make_shared<GMEnum>(decl->Name, std::move(values));
    }
    context.ParseEnums().clear();
    context.ParseEnumOrder().clear();
}

} // namespace Underanalyzer::Compiler::Nodes

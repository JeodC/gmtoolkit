
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Lexer/Numbers.h"

#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/Lexer/ContiguousTextReader.h"
#include "Underanalyzer/Compiler/Lexer/LexContext.h"

#include <charconv>
#include <cstdlib>
#include <string>

namespace Underanalyzer::Compiler::Lexer {

int Numbers::ParseDecimal(LexContext& context, int startPosition) {
    std::string_view number;
    int pos = ContiguousTextReader::ReadWhileNumber(context.Text(), startPosition, number);

    int64_t intValue = 0;
    auto intResult = std::from_chars(number.data(), number.data() + number.size(), intValue);
    if (intResult.ec == std::errc{} && intResult.ptr == number.data() + number.size()) {
        // Roundtrip via double tells us whether the value still fits in a 53-bit mantissa;
        // anything that loses precision must be kept as a 64-bit integer token.
        // INT64_MAX rounds up to 2^63 as a double, making the back-conversion UB
        // in C++ (x64 traps to INT64_MIN, ARM saturates); .NET saturates, so
        // treat that one literal as the saturated comparison upstream performs.
        double dValue = static_cast<double>(intValue);
        bool lossless = dValue < 9223372036854775808.0 ? (static_cast<int64_t>(dValue) == intValue)
                                                       : (intValue == INT64_MAX);
        if (lossless) {
            context.Tokens().push_back(context.Arena().New<TokenNumber>(context, startPosition, std::string(number),
                                                                        static_cast<double>(intValue)));
        } else {
            context.Tokens().push_back(
                context.Arena().New<TokenInt64>(context, startPosition, std::string(number), intValue));
        }
        return pos;
    }

    std::string numberStr(number);
    char* endPtr = nullptr;
    double floatValue = std::strtod(numberStr.c_str(), &endPtr);
    if (endPtr == numberStr.c_str() + numberStr.size()) {
        context.Tokens().push_back(context.Arena().New<TokenNumber>(context, startPosition, numberStr, floatValue));
    } else {
        context.CompileContextRef().PushError("Invalid number \"" + numberStr + "\"", context, startPosition);
    }
    return pos;
}

int Numbers::ParseHex(LexContext& context, int startPosition, bool dollarSignSyntax) {
    std::string_view hex;
    int prefixLen = dollarSignSyntax ? 1 : 2;
    int pos = ContiguousTextReader::ReadWhileHex(context.Text(), startPosition + prefixLen, hex);

    // Parse as unsigned and bit-cast: .NET's NumberStyles.HexNumber treats
    // 16-digit hex as a two's-complement bit pattern ($ffffffffffffffff == -1),
    // while a signed from_chars would reject it as overflow.
    uint64_t uvalue = 0;
    auto result = std::from_chars(hex.data(), hex.data() + hex.size(), uvalue, 16);
    if (result.ec != std::errc{} || result.ptr != hex.data() + hex.size()) {
        context.CompileContextRef().PushError("Invalid hex literal", context, startPosition);
        return pos;
    }
    int64_t value = static_cast<int64_t>(uvalue);

    std::string display = (dollarSignSyntax ? "$" : "0x") + std::string(hex);
    // Hex literals stay 32-bit (numeric) unless they overflow into 64-bit territory.
    if (value >= INT32_MIN && value <= INT32_MAX) {
        context.Tokens().push_back(
            context.Arena().New<TokenNumber>(context, startPosition, std::move(display), static_cast<double>(value)));
    } else {
        context.Tokens().push_back(context.Arena().New<TokenInt64>(context, startPosition, std::move(display), value));
    }
    return pos;
}

} // namespace Underanalyzer::Compiler::Lexer

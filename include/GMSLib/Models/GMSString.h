
// Source: github.com/UnderminersTeam/UndertaleModTool @ c4e5c2c3
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Underanalyzer/VMData.h"

#include <cstdint>
#include <string>

namespace GMSLib {

class GMSString final : public Underanalyzer::IGMString {
  public:
    std::string Content;
    std::int64_t SourcePayloadOffset = -1;
    std::int32_t Id = -1;

    GMSString() = default;
    explicit GMSString(std::string ContentIn) : Content(std::move(ContentIn)) {
    }

    const std::string& Text() const override {
        return Content;
    }
};

} // namespace GMSLib

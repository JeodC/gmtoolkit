
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/VMConstants.h"

namespace Underanalyzer {

const std::unordered_set<std::string_view>& VMConstants::BuiltinArrayVariables() {
    static const std::unordered_set<std::string_view> set = {
        "view_xview",   "view_yview",      "view_wview",      "view_hview",       "view_angle",
        "view_hborder", "view_vborder",    "view_hspeed",     "view_vspeed",      "view_object",
        "view_xport",   "view_yport",      "view_wport",      "view_hport",       "view_surface_id",
        "view_camera",  "phy_collision_x", "phy_collision_y", "phy_col_normal_x", "phy_col_normal_y"
    };
    return set;
}

} // namespace Underanalyzer

// SPDX-License-Identifier: GPL-3.0-or-later

#include "GMSLib/GMSData.h"

namespace GMSLib {

// Lexicographic compare against the (major, minor, release, build) tuple in
// GEN8. Only the last component uses >= so that omitted args (defaulting to 0)
// still match the lowest version in the requested major/minor/release.
bool GMSData::IsVersionAtLeast(int Major, int Minor, int Release, int Build) const {
    const auto& V = GeneralInfo.Version;
    if (static_cast<int>(V.major) != Major)
        return static_cast<int>(V.major) > Major;
    if (static_cast<int>(V.minor) != Minor)
        return static_cast<int>(V.minor) > Minor;
    if (static_cast<int>(V.release) != Release)
        return static_cast<int>(V.release) > Release;
    return static_cast<int>(V.build) >= Build;
}

} // namespace GMSLib

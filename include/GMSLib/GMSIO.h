// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Toolkit/Version.h"

#include <string>

namespace GMSLib {

class GMSData;

int DetectVersion(const std::string& Path, Gmtoolkit::Version& OutVersion);
int LoadFromFile(const std::string& Path, GMSData& OutData);
int SaveToFile(const std::string& Path, GMSData& Data);

} // namespace GMSLib

// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

namespace GMSLib {

class GMSData;

int LoadFromFile(const std::string& Path, GMSData& OutData);
int SaveToFile(const std::string& Path, GMSData& Data);

} // namespace GMSLib

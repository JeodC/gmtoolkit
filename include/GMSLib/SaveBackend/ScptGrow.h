// SPDX-License-Identifier: MIT

#pragma once

#include "GMSLib/SaveBackend/Pools.h"

#include <string>
#include <vector>

namespace GMSLib::SaveBackend {

int grow_scpt_in_place(Pools& P, const std::vector<Pools::ScptInsert>& inserts);

}

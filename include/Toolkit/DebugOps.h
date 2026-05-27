// SPDX-License-Identifier: MIT

#pragma once

namespace Cli {

int dispatch_info(const char* data_path);
int dispatch_pool_test(const char* data_path, const char* sample, const char* out_path);

} // namespace Cli

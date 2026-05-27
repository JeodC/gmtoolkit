// SPDX-License-Identifier: MIT

#include "Toolkit/Log.h"

#include <stdio.h>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace Gmtoolkit {

namespace {
bool g_drag_drop = false;
}

void start_output_tee(const std::string& log_path) {
    FILE* f = fopen(log_path.c_str(), "w");
    if (!f)
        return;
    log_sink() = f;
}

void stop_output_tee() {
    FILE*& sink = log_sink();
    if (sink) {
        fclose(sink);
        sink = nullptr;
    }
}

void set_drag_drop_mode() {
    g_drag_drop = true;
}

// A drag-and-drop launch closes the console as soon as main returns, so block on input.
void pause_if_drag_drop() {
#if defined(_WIN32)
    if (!g_drag_drop)
        return;
    fprintf(stderr, "\nPress Enter to close...");
    fflush(stderr);
    char c;
    fread(&c, 1, 1, stdin);
#endif
}

} // namespace Gmtoolkit

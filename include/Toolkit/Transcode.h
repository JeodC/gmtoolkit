// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

namespace Gmtoolkit {

// Video re-encode parameters. Defaults: 24 fps MPEG-4, cheap to decode on weak SoCs.
struct TranscodeOpts {
    int fps = 24;
    int width = 0;  // 0 = keep source
    int height = 0;
    int64_t video_bitrate = 1500000;
    int64_t rc_max_rate = 2000000;
    int64_t rc_buffer_size = 3000000;
};

// Re-encode in_path -> out_path (MPEG-4 video, audio stream-copied, MP4). Returns 0 on success.
int transcode_video(const char* in_path, const char* out_path, const TranscodeOpts& opt);

} // namespace Gmtoolkit

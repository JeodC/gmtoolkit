// SPDX-License-Identifier: MIT

#include "Toolkit/Platform.h"
#include "Toolkit/Log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_STDIO
#define STBI_ASSERT(x)
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

static const uint8_t PNG_SIG[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };

int is_png_sig(const uint8_t* buf, size_t len) {
    return len >= 8 && memcmp(buf, PNG_SIG, 8) == 0;
}

// Walk PNG chunks until IEND to discover the encoded length; needed because TXTR doesn't store it.
size_t scan_png_blob_len(const uint8_t* src, size_t src_left) {
    if (!is_png_sig(src, src_left))
        return 0;
    size_t i = 8;
    while (i + 12 <= src_left) {
        uint32_t clen = ((uint32_t)src[i + 0] << 24) | ((uint32_t)src[i + 1] << 16) | ((uint32_t)src[i + 2] << 8) |
                        ((uint32_t)src[i + 3]);
        size_t chunk_total = 4 + 4 + (size_t)clen + 4;
        if (chunk_total < 12 || i + chunk_total > src_left)
            return 0;
        bool is_iend = (memcmp(src + i + 4, "IEND", 4) == 0);
        i += chunk_total;
        if (is_iend)
            return i;
    }
    return 0;
}

int decode_png(const uint8_t* blob, size_t blob_len, uint8_t** out_rgba, int* out_w, int* out_h) {
    int w = 0, h = 0, channels = 0;
    stbi_uc* px = stbi_load_from_memory(blob, (int)blob_len, &w, &h, &channels, 4);
    if (!px) {
        Gmtoolkit::err("decode_png: %s", stbi_failure_reason());
        return -1;
    }
    *out_rgba = (uint8_t*)px;
    *out_w = w;
    *out_h = h;
    return 0;
}

struct png_capture {
    uint8_t* buf;
    size_t len;
    size_t cap;
    bool oom;
};

static void png_write_capture(void* ctx, void* data, int size) {
    png_capture* c = (png_capture*)ctx;
    if (c->oom || size <= 0)
        return;
    size_t need = c->len + (size_t)size;
    if (need > c->cap) {
        size_t newcap = c->cap ? c->cap : 1024;
        while (newcap < need)
            newcap *= 2;
        uint8_t* grown = (uint8_t*)realloc(c->buf, newcap);
        if (!grown) {
            c->oom = true;
            return;
        }
        c->buf = grown;
        c->cap = newcap;
    }
    memcpy(c->buf + c->len, data, (size_t)size);
    c->len = need;
}

int encode_png(const uint8_t* rgba, int w, int h, uint8_t** out, size_t* out_len) {
    png_capture cap{};

    int ok = stbi_write_png_to_func(png_write_capture, &cap, w, h, 4, rgba, w * 4);
    if (!ok || cap.oom || cap.len == 0) {
        free(cap.buf);
        Gmtoolkit::err("encode_png: stb_image_write failed (oom=%d)", cap.oom ? 1 : 0);
        return -1;
    }
    *out = cap.buf;
    *out_len = cap.len;
    return 0;
}

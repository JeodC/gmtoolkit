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

static uint32_t crc32_table[256];
static bool crc32_table_built = false;

static void crc32_init_table(void) {
    if (crc32_table_built)
        return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc32_table[i] = c;
    }
    crc32_table_built = true;
}

static uint32_t crc32_compute(const uint8_t* data, size_t len) {
    crc32_init_table();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        c = crc32_table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

static uint32_t adler32_compute(const uint8_t* data, size_t len) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; i++) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

// Hand-crafted 1x1 PNG: signature + minimal IHDR/IDAT/IEND with the index baked into the pixel.
// Lets external pages keep a valid PNG inline so anything walking TXTR by-format still sees PNG.
int build_png_stub(unsigned int idx, uint8_t** out, size_t* out_len) {
    uint8_t pixels[9];
    pixels[0] = 0;
    pixels[1] = 0xDE;
    pixels[2] = 0xAD;
    pixels[3] = 0xBE;
    pixels[4] = 0xFF;
    pixels[5] = (uint8_t)(idx & 0xFF);
    pixels[6] = (uint8_t)((idx >> 8) & 0xFF);
    pixels[7] = (uint8_t)((idx >> 16) & 0xFF);
    pixels[8] = 0xFF;

    uint8_t zlib[20];
    zlib[0] = 0x78;
    zlib[1] = 0x01;
    zlib[2] = 0x01;
    zlib[3] = 9;
    zlib[4] = 0;
    zlib[5] = (uint8_t)(~9 & 0xFF);
    zlib[6] = 0xFF;
    memcpy(zlib + 7, pixels, 9);
    uint32_t adler = adler32_compute(pixels, 9);
    zlib[16] = (uint8_t)((adler >> 24) & 0xFF);
    zlib[17] = (uint8_t)((adler >> 16) & 0xFF);
    zlib[18] = (uint8_t)((adler >> 8) & 0xFF);
    zlib[19] = (uint8_t)(adler & 0xFF);

    const size_t total = 8 + 25 + 32 + 12;
    uint8_t* png = (uint8_t*)malloc(total);
    if (!png)
        return -1;
    size_t p = 0;

    memcpy(png + p, PNG_SIG, 8);
    p += 8;

    png[p++] = 0;
    png[p++] = 0;
    png[p++] = 0;
    png[p++] = 13;
    size_t ihdr_crc_start = p;
    memcpy(png + p, "IHDR", 4);
    p += 4;
    png[p++] = 0;
    png[p++] = 0;
    png[p++] = 0;
    png[p++] = 2;
    png[p++] = 0;
    png[p++] = 0;
    png[p++] = 0;
    png[p++] = 1;
    png[p++] = 8;
    png[p++] = 6;
    png[p++] = 0;
    png[p++] = 0;
    png[p++] = 0;
    {
        uint32_t crc = crc32_compute(png + ihdr_crc_start, 4 + 13);
        png[p++] = (uint8_t)((crc >> 24) & 0xFF);
        png[p++] = (uint8_t)((crc >> 16) & 0xFF);
        png[p++] = (uint8_t)((crc >> 8) & 0xFF);
        png[p++] = (uint8_t)(crc & 0xFF);
    }

    png[p++] = 0;
    png[p++] = 0;
    png[p++] = 0;
    png[p++] = 20;
    size_t idat_crc_start = p;
    memcpy(png + p, "IDAT", 4);
    p += 4;
    memcpy(png + p, zlib, 20);
    p += 20;
    {
        uint32_t crc = crc32_compute(png + idat_crc_start, 4 + 20);
        png[p++] = (uint8_t)((crc >> 24) & 0xFF);
        png[p++] = (uint8_t)((crc >> 16) & 0xFF);
        png[p++] = (uint8_t)((crc >> 8) & 0xFF);
        png[p++] = (uint8_t)(crc & 0xFF);
    }

    png[p++] = 0;
    png[p++] = 0;
    png[p++] = 0;
    png[p++] = 0;
    memcpy(png + p, "IEND", 4);
    p += 4;
    png[p++] = 0xAE;
    png[p++] = 0x42;
    png[p++] = 0x60;
    png[p++] = 0x82;

    *out = png;
    *out_len = total;
    return 0;
}

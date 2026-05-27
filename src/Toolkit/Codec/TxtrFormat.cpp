// SPDX-License-Identifier: MIT

#include "GMSLib/GMSChunks.h"
#include "Toolkit/Codec/Txtr.h"
#include "Toolkit/IO.h"
#include "Toolkit/Platform.h"
#include "Toolkit/Log.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern size_t scan_2zoq_blob_len(const uint8_t* src, size_t src_left);
extern size_t scan_yyg_qoif_blob_len(const uint8_t* qoif, size_t qoif_len);
extern int parse_2zoq(const uint8_t* blob, size_t blob_len, uint8_t** qoif_out, size_t* qoif_len_out);
extern int decode_yyg_qoif(const uint8_t* qoif, size_t qoif_len, uint8_t** out_rgba, int* out_w, int* out_h);
extern int build_2zoq_stub(unsigned int idx, uint8_t** out, size_t* out_len);
extern int build_fioq_stub(unsigned int idx, uint8_t** out, size_t* out_len);
extern int encode_yyg_qoif(const uint8_t* rgba, int w, int h, uint8_t** out, size_t* out_len);
extern int wrap_2zoq(const uint8_t* qoif, size_t qoif_len, int w, int h, uint8_t** out, size_t* out_len);

extern int is_png_sig(const uint8_t* buf, size_t len);
extern size_t scan_png_blob_len(const uint8_t* src, size_t src_left);
extern int decode_png(const uint8_t* blob, size_t blob_len, uint8_t** out_rgba, int* out_w, int* out_h);
extern int build_png_stub(unsigned int idx, uint8_t** out, size_t* out_len);
extern int encode_png(const uint8_t* rgba, int w, int h, uint8_t** out, size_t* out_len);

TxtrFormat detect_blob_format(const uint8_t* p, size_t len) {
    if (len < 4)
        return TxtrFormat::UNKNOWN;
    if (memcmp(p, "2zoq", 4) == 0)
        return TxtrFormat::ZOQ;
    if (memcmp(p, "fioq", 4) == 0)
        return TxtrFormat::QOIF;
    if (memcmp(p, "DDS ", 4) == 0)
        return TxtrFormat::DDS;
    if (is_png_sig(p, len))
        return TxtrFormat::PNG;
    return TxtrFormat::UNKNOWN;
}

static size_t scan_dds_blob_len(const uint8_t*, size_t) {
    return 0;
}

size_t scan_blob_len(TxtrFormat fmt, const uint8_t* src, size_t src_left) {
    if (fmt == TxtrFormat::ZOQ)
        return scan_2zoq_blob_len(src, src_left);
    if (fmt == TxtrFormat::QOIF)
        return scan_yyg_qoif_blob_len(src, src_left);
    if (fmt == TxtrFormat::PNG)
        return scan_png_blob_len(src, src_left);
    if (fmt == TxtrFormat::DDS)
        return scan_dds_blob_len(src, src_left);
    return 0;
}

int decode_blob_to_rgba(TxtrFormat fmt, const uint8_t* blob, size_t blob_len, uint8_t** out_rgba, int* out_w,
                        int* out_h) {
    if (fmt == TxtrFormat::ZOQ) {
        uint8_t* qoif = NULL;
        size_t qoif_len = 0;
        int r = parse_2zoq(blob, blob_len, &qoif, &qoif_len);
        if (r != 0) {
            Gmtoolkit::err("parse_2zoq failed: %d", r);
            return r;
        }
        r = decode_yyg_qoif(qoif, qoif_len, out_rgba, out_w, out_h);
        free(qoif);
        if (r != 0)
            Gmtoolkit::err("decode_yyg_qoif failed: %d", r);
        return r;
    }
    if (fmt == TxtrFormat::QOIF) {
        return decode_yyg_qoif(blob, blob_len, out_rgba, out_w, out_h);
    }
    if (fmt == TxtrFormat::PNG) {
        return decode_png(blob, blob_len, out_rgba, out_w, out_h);
    }
    if (fmt == TxtrFormat::DDS) {
        Gmtoolkit::tprint("DDS texture decode not yet implemented in gmtoolkit. "
                          "Use upstream UTMT to patch DDS-source data files.\n");
        return -1;
    }
    Gmtoolkit::err("decode_blob_to_rgba: unknown format");
    return -1;
}

int build_stub_for_format(TxtrFormat fmt, unsigned int idx, uint8_t** out, size_t* out_len) {
    if (fmt == TxtrFormat::ZOQ)
        return build_2zoq_stub(idx, out, out_len);
    if (fmt == TxtrFormat::QOIF)
        return build_fioq_stub(idx, out, out_len);
    if (fmt == TxtrFormat::PNG)
        return build_png_stub(idx, out, out_len);
    return -1;
}

int encode_atlas_in_format(TxtrFormat fmt, const uint8_t* rgba, int w, int h, uint8_t** out, size_t* out_len) {
    if (fmt == TxtrFormat::PNG)
        return encode_png(rgba, w, h, out, out_len);
    if (fmt == TxtrFormat::QOIF)
        return encode_yyg_qoif(rgba, w, h, out, out_len);
    if (fmt == TxtrFormat::ZOQ) {
        uint8_t* qoif = NULL;
        size_t qoif_len = 0;
        int r = encode_yyg_qoif(rgba, w, h, &qoif, &qoif_len);
        if (r != 0)
            return r;
        r = wrap_2zoq(qoif, qoif_len, w, h, out, out_len);
        free(qoif);
        return r;
    }
    Gmtoolkit::err("encode_atlas_in_format: format not supported for inline output");
    return -1;
}

// Rewrite TXTR in place: keep entry headers, drop the original blob payloads, repoint each entry to
// its 0x80-aligned slot of `stubs[i]`, then slide AUDO and trim the file to match.
long compact_txtr(FILE* f, size_t txtr_start, size_t txtr_size, uint8_t** stubs, size_t* stub_lens, unsigned int count,
                  size_t entry_size) {
    fseek(f, (long)txtr_start, SEEK_SET);
    uint32_t got;
    if (fread(&got, 4, 1, f) != 1)
        return 0;
    if (got != count) {
        Gmtoolkit::err("TXTR count mismatch: file=%u, expected=%u", got, count);
        return 0;
    }

    uint32_t* ptrs = (uint32_t*)malloc(count * sizeof(uint32_t));
    if (!ptrs)
        return 0;
    if (fread(ptrs, 4, count, f) != count) {
        free(ptrs);
        return 0;
    }

    bool has_size_field = (entry_size >= 16);
    size_t ptr_off_in_entry = entry_size - 4;

    uint8_t* entries = (uint8_t*)malloc((size_t)count * entry_size);
    if (!entries) {
        free(ptrs);
        return 0;
    }
    for (unsigned int i = 0; i < count; i++) {
        fseek(f, ptrs[i], SEEK_SET);
        if (fread(entries + (size_t)i * entry_size, 1, entry_size, f) != entry_size) {
            free(entries);
            free(ptrs);
            return 0;
        }
    }

    uint32_t entries_end = 0;
    for (unsigned int i = 0; i < count; i++) {
        uint32_t e = ptrs[i] + (uint32_t)entry_size;
        if (e > entries_end)
            entries_end = e;
    }

    // Each blob pointer must be absolute 0x80-aligned; UTMT validates this on load.
    uint64_t blob_cursor = entries_end;
    for (unsigned int i = 0; i < count; i++) {
        if (stub_lens[i] == 0)
            continue;
        while (blob_cursor & 0x7F)
            blob_cursor++;
        uint8_t* rec = entries + (size_t)i * entry_size;
        if (has_size_field) {
            rec[8] = (uint8_t)(stub_lens[i] & 0xFF);
            rec[9] = (uint8_t)((stub_lens[i] >> 8) & 0xFF);
            rec[10] = (uint8_t)((stub_lens[i] >> 16) & 0xFF);
            rec[11] = (uint8_t)((stub_lens[i] >> 24) & 0xFF);
        }
        uint32_t new_ptr = (uint32_t)blob_cursor;
        rec[ptr_off_in_entry + 0] = (uint8_t)(new_ptr & 0xFF);
        rec[ptr_off_in_entry + 1] = (uint8_t)((new_ptr >> 8) & 0xFF);
        rec[ptr_off_in_entry + 2] = (uint8_t)((new_ptr >> 16) & 0xFF);
        rec[ptr_off_in_entry + 3] = (uint8_t)((new_ptr >> 24) & 0xFF);
        blob_cursor += stub_lens[i];
    }

    while ((blob_cursor - txtr_start) & 3u)
        blob_cursor++;
    uint64_t new_payload_size = blob_cursor - txtr_start;
    if (new_payload_size > txtr_size) {
        Gmtoolkit::err("Compact would grow TXTR; not safe (%" PRIu64 " > %zu)", new_payload_size, txtr_size);
        free(entries);
        free(ptrs);
        return 0;
    }

    fseek(f, 0, SEEK_END);
    long total = ftell(f);
    long tail_start = (long)(txtr_start + txtr_size);
    long tail_len = total - tail_start;
    uint8_t* tail = NULL;
    if (tail_len > 0) {
        tail = (uint8_t*)malloc((size_t)tail_len);
        if (!tail) {
            free(entries);
            free(ptrs);
            return 0;
        }
        fseek(f, tail_start, SEEK_SET);
        if (fread(tail, 1, (size_t)tail_len, f) != (size_t)tail_len) {
            free(tail);
            free(entries);
            free(ptrs);
            return 0;
        }
    }

    uint32_t new_pls32 = (uint32_t)new_payload_size;
    fseek(f, (long)txtr_start - 4, SEEK_SET);
    fwrite(&new_pls32, 4, 1, f);

    for (unsigned int i = 0; i < count; i++) {
        fseek(f, ptrs[i], SEEK_SET);
        fwrite(entries + (size_t)i * entry_size, 1, entry_size, f);
    }

    fseek(f, entries_end, SEEK_SET);
    uint64_t pos = entries_end;
    uint8_t pad_zeros[128] = { 0 };
    for (unsigned int i = 0; i < count; i++) {
        if (stub_lens[i] == 0)
            continue;
        uint8_t* rec = entries + (size_t)i * entry_size;
        uint64_t target = Gmtoolkit::r_u32(rec + ptr_off_in_entry);
        while (pos < target) {
            size_t gap = (size_t)(target - pos);
            if (gap > sizeof(pad_zeros))
                gap = sizeof(pad_zeros);
            fwrite(pad_zeros, 1, gap, f);
            pos += gap;
        }
        fwrite(stubs[i], 1, stub_lens[i], f);
        pos += stub_lens[i];
    }

    uint64_t pad_end = txtr_start + new_payload_size;
    while (pos < pad_end) {
        size_t gap = (size_t)(pad_end - pos);
        if (gap > sizeof(pad_zeros))
            gap = sizeof(pad_zeros);
        fwrite(pad_zeros, 1, gap, f);
        pos += gap;
    }

    long new_tail_start = (long)(txtr_start + new_payload_size);
    if (tail_len > 0) {
        fseek(f, new_tail_start, SEEK_SET);
        fwrite(tail, 1, (size_t)tail_len, f);
        free(tail);
    }
    long new_total = new_tail_start + tail_len;

    fflush(f);
    if (portable_truncate(f, (long long)new_total) != 0) {
        perror("ftruncate");
        free(entries);
        free(ptrs);
        return 0;
    }

    uint32_t form_size = (uint32_t)(new_total - 8);
    fseek(f, 4, SEEK_SET);
    fwrite(&form_size, 4, 1, f);

    GMSLib::SlideAudoPointers(f, txtr_start + new_payload_size, -(long)(txtr_size - (size_t)new_payload_size));
    fflush(f);

    free(entries);
    free(ptrs);
    return new_total;
}

size_t find_next_blob(const uint8_t* buf, size_t start, size_t end, TxtrFormat* out_fmt) {
    for (size_t i = start; i + 8 <= end; i++) {
        if (buf[i] == '2' && buf[i + 1] == 'z' && buf[i + 2] == 'o' && buf[i + 3] == 'q') {
            *out_fmt = TxtrFormat::ZOQ;
            return i;
        }
        if (buf[i] == 'f' && buf[i + 1] == 'i' && buf[i + 2] == 'o' && buf[i + 3] == 'q') {
            *out_fmt = TxtrFormat::QOIF;
            return i;
        }
        if (is_png_sig(buf + i, end - i)) {
            *out_fmt = TxtrFormat::PNG;
            return i;
        }
    }
    *out_fmt = TxtrFormat::UNKNOWN;
    return (size_t)-1;
}

// SPDX-License-Identifier: MIT

#include "Toolkit/IO.h"
#include "Toolkit/Log.h"
#include "Toolkit/Platform.h"

#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>
#include <vorbis/vorbisfile.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using Gmtoolkit::find_chunk;
using Gmtoolkit::is_oggs;
using Gmtoolkit::is_riff;
using Gmtoolkit::r_u32;
using Gmtoolkit::read_strg_at;
using Gmtoolkit::slurp;
using Gmtoolkit::spew;
using Gmtoolkit::w_u32;

static const uint32_t SOND_FLAG_EMBEDDED = 0x01;
static const uint32_t SOND_FLAG_COMPRESSED = 0x02;
static const int OGG_STREAM_SERIALNO = 0x4B544D47;

struct AudioOpts {
    int bitrate;
    bool downmix;
    int resample;
};

struct PcmBuf {
    std::vector<int16_t> samples;
    int channels;
    int sample_rate;
};

struct OvMemSrc {
    const uint8_t* data;
    size_t size;
    size_t pos;
};

struct AgrpState {
    std::string path;
    MappedFile mmap_in;
    bool loaded;
    bool dirty;

    std::vector<uint8_t> sond_blob;
    size_t sond_chunk_off;
    bool sond_modified;

    size_t audo_chunk_off;
    size_t audo_chunk_size;

    struct Entry {
        size_t src_off;
        uint32_t orig_size;
        int action;
        std::vector<uint8_t> new_bytes;
    };
    std::vector<Entry> entries;
};

struct SondCtx {
    size_t chunk_off;
    size_t chunk_size;
    uint32_t count;
    int entry_size;
};

enum TaskKind : uint8_t { TK_WAV, TK_OGG_RECOMPRESS };
struct EncodeTask {
    TaskKind kind;
    uint32_t sond_idx;
    int32_t agrp_id;
    int32_t aid;
    const uint8_t* payload;
    size_t payload_size;
};

struct EncodeResult {
    std::vector<uint8_t> bytes;
    size_t orig_size = 0;
    bool ok = false;
};

// Walk RIFF chunks for 'fmt ' + 'data' and normalise samples to 16-bit signed regardless of source depth.
static int parse_wav(const uint8_t* wav, size_t wav_size, PcmBuf& out) {
    if (wav_size < 44 || !is_riff(wav, wav_size) || std::memcmp(wav + 8, "WAVE", 4) != 0) {
        Gmtoolkit::err("audio: input is not a RIFF/WAVE file");
        return -1;
    }

    size_t p = 12;
    bool have_fmt = false;
    uint16_t fmt_code = 0, n_chan = 0, bits = 0;
    uint32_t srate = 0;
    const uint8_t* pcm_ptr = nullptr;
    uint32_t pcm_bytes = 0;
    while (p + 8 <= wav_size) {
        const uint8_t* id = wav + p;
        uint32_t csz = r_u32(wav + p + 4);
        if (p + 8 + csz > wav_size)
            break;
        if (std::memcmp(id, "fmt ", 4) == 0 && csz >= 16) {
            fmt_code = (uint16_t)(wav[p + 8] | (wav[p + 9] << 8));
            n_chan = (uint16_t)(wav[p + 10] | (wav[p + 11] << 8));
            srate = r_u32(wav + p + 12);
            bits = (uint16_t)(wav[p + 22] | (wav[p + 23] << 8));
            have_fmt = true;
        } else if (std::memcmp(id, "data", 4) == 0) {
            pcm_ptr = wav + p + 8;
            pcm_bytes = csz;
            break;
        }

        p += 8 + csz + (csz & 1);
    }
    if (!have_fmt || !pcm_ptr) {
        Gmtoolkit::err("audio: WAV missing 'fmt '/'data' chunks");
        return -1;
    }
    if (fmt_code != 1) {
        Gmtoolkit::err("audio: only PCM WAV supported (got fmt=%u)", fmt_code);
        return -1;
    }
    out.channels = n_chan;
    out.sample_rate = (int)srate;
    if (bits == 16) {
        size_t n_samples = pcm_bytes / 2;
        out.samples.assign((const int16_t*)pcm_ptr, (const int16_t*)pcm_ptr + n_samples);
    } else if (bits == 8) {

        size_t n = pcm_bytes;
        out.samples.resize(n);
        for (size_t i = 0; i < n; i++) {
            out.samples[i] = (int16_t)(((int)pcm_ptr[i] - 128) << 8);
        }
    } else if (bits == 24) {

        size_t n = pcm_bytes / 3;
        out.samples.resize(n);
        for (size_t i = 0; i < n; i++) {
            int32_t s24 = (int32_t)pcm_ptr[i * 3 + 1] | ((int32_t)pcm_ptr[i * 3 + 2] << 8);
            if (s24 & 0x8000)
                s24 |= ~0xFFFF;
            out.samples[i] = (int16_t)s24;
        }
    } else if (bits == 32) {
        size_t n = pcm_bytes / 4;
        out.samples.resize(n);
        for (size_t i = 0; i < n; i++) {
            int32_t s32 = (int32_t)pcm_ptr[i * 4] | ((int32_t)pcm_ptr[i * 4 + 1] << 8) |
                          ((int32_t)pcm_ptr[i * 4 + 2] << 16) | ((int32_t)pcm_ptr[i * 4 + 3] << 24);
            out.samples[i] = (int16_t)(s32 >> 16);
        }
    } else {
        Gmtoolkit::err("audio: unsupported PCM bit depth %u (want 8/16/24/32)", bits);
        return -1;
    }
    return 0;
}

// Optional stereo->mono downmix and linear-interp resample, then libvorbis-encode into an OGG stream.
static int encode_pcm_to_ogg(const PcmBuf& pcm, const AudioOpts& opt, std::vector<uint8_t>& out) {
    out.clear();
    int channels = pcm.channels;
    int rate = pcm.sample_rate;
    const int16_t* src = pcm.samples.data();
    long src_frames = (long)(pcm.samples.size() / channels);
    std::vector<int16_t> resampled;
    std::vector<int16_t> downmixed;

    if (opt.downmix && channels == 2) {
        downmixed.resize((size_t)src_frames);
        for (long i = 0; i < src_frames; i++) {
            int32_t l = src[i * 2];
            int32_t r = src[i * 2 + 1];
            downmixed[i] = (int16_t)((l + r) / 2);
        }
        src = downmixed.data();
        channels = 1;
    }
    if (opt.resample > 0 && opt.resample != rate) {
        double ratio = (double)opt.resample / (double)rate;
        long out_frames = (long)((double)src_frames * ratio);
        resampled.resize((size_t)out_frames * channels);
        for (long i = 0; i < out_frames; i++) {
            double src_pos = (double)i / ratio;
            long s0 = (long)src_pos;
            long s1 = s0 + 1 < src_frames ? s0 + 1 : s0;
            double frac = src_pos - (double)s0;
            for (int c = 0; c < channels; c++) {
                double a = src[s0 * channels + c];
                double b = src[s1 * channels + c];
                resampled[i * channels + c] = (int16_t)(a + (b - a) * frac);
            }
        }
        src = resampled.data();
        src_frames = out_frames;
        rate = opt.resample;
    }

    vorbis_info vi;
    vorbis_info_init(&vi);

    int rc;
    if (opt.bitrate > 0) {
        long target = opt.bitrate * 1000;
        rc = vorbis_encode_init(&vi, channels, rate, -1, target, -1);
    } else {
        rc = vorbis_encode_init_vbr(&vi, channels, rate, 0.4f);
    }
    if (rc != 0) {
        Gmtoolkit::err("audio: vorbis_encode_init failed (channels=%d rate=%d rc=%d)", channels, rate, rc);
        vorbis_info_clear(&vi);
        return -1;
    }

    vorbis_comment vc;
    vorbis_comment_init(&vc);
    vorbis_comment_add_tag(&vc, "ENCODER", "gmtoolkit/libvorbis");

    vorbis_dsp_state vd;
    vorbis_block vb;
    vorbis_analysis_init(&vd, &vi);
    vorbis_block_init(&vd, &vb);

    ogg_stream_state os;
    ogg_stream_init(&os, OGG_STREAM_SERIALNO);

    ogg_packet h, hc, hb;
    vorbis_analysis_headerout(&vd, &vc, &h, &hc, &hb);
    ogg_stream_packetin(&os, &h);
    ogg_stream_packetin(&os, &hc);
    ogg_stream_packetin(&os, &hb);

    ogg_page og;
    while (ogg_stream_flush(&os, &og) != 0) {
        out.insert(out.end(), og.header, og.header + og.header_len);
        out.insert(out.end(), og.body, og.body + og.body_len);
    }

    const long CHUNK = 4096;
    long pos = 0;
    while (pos < src_frames) {
        long n = std::min(CHUNK, src_frames - pos);
        float** buf = vorbis_analysis_buffer(&vd, (int)n);
        for (long i = 0; i < n; i++) {
            for (int c = 0; c < channels; c++) {
                buf[c][i] = (float)src[(pos + i) * channels + c] / 32768.0f;
            }
        }
        vorbis_analysis_wrote(&vd, (int)n);
        pos += n;

        while (vorbis_analysis_blockout(&vd, &vb) == 1) {
            vorbis_analysis(&vb, nullptr);
            vorbis_bitrate_addblock(&vb);
            ogg_packet op;
            while (vorbis_bitrate_flushpacket(&vd, &op)) {
                ogg_stream_packetin(&os, &op);
                while (ogg_stream_pageout(&os, &og) != 0) {
                    out.insert(out.end(), og.header, og.header + og.header_len);
                    out.insert(out.end(), og.body, og.body + og.body_len);
                }
            }
        }
    }

    vorbis_analysis_wrote(&vd, 0);
    while (vorbis_analysis_blockout(&vd, &vb) == 1) {
        vorbis_analysis(&vb, nullptr);
        vorbis_bitrate_addblock(&vb);
        ogg_packet op;
        while (vorbis_bitrate_flushpacket(&vd, &op)) {
            ogg_stream_packetin(&os, &op);
            while (ogg_stream_flush(&os, &og) != 0) {
                out.insert(out.end(), og.header, og.header + og.header_len);
                out.insert(out.end(), og.body, og.body + og.body_len);
            }
        }
    }

    ogg_stream_clear(&os);
    vorbis_block_clear(&vb);
    vorbis_dsp_clear(&vd);
    vorbis_comment_clear(&vc);
    vorbis_info_clear(&vi);
    return 0;
}

static size_t ov_mem_read(void* ptr, size_t sz, size_t nm, void* src_) {
    auto* src = (OvMemSrc*)src_;
    size_t remain = src->size - src->pos;
    size_t want = sz * nm;
    if (want > remain)
        want = remain;
    std::memcpy(ptr, src->data + src->pos, want);
    src->pos += want;
    return want / sz;
}

static int ov_mem_seek(void* src_, ogg_int64_t off, int whence) {
    auto* src = (OvMemSrc*)src_;
    size_t base = (whence == SEEK_SET) ? 0 : (whence == SEEK_CUR) ? src->pos : src->size;
    if ((ogg_int64_t)base + off < 0)
        return -1;
    if ((size_t)((ogg_int64_t)base + off) > src->size)
        return -1;
    src->pos = (size_t)((ogg_int64_t)base + off);
    return 0;
}

static long ov_mem_tell(void* src_) {
    return (long)((OvMemSrc*)src_)->pos;
}

static int decode_ogg_to_pcm(const uint8_t* ogg, size_t ogg_size, PcmBuf& out) {
    OvMemSrc src{ ogg, ogg_size, 0 };
    ov_callbacks cb;
    cb.read_func = ov_mem_read;
    cb.seek_func = ov_mem_seek;
    cb.tell_func = ov_mem_tell;
    cb.close_func = nullptr;

    OggVorbis_File vf;
    if (ov_open_callbacks(&src, &vf, nullptr, 0, cb) != 0) {
        Gmtoolkit::err("audio: ov_open_callbacks failed");
        return -1;
    }
    vorbis_info* vi = ov_info(&vf, -1);
    out.channels = vi->channels;
    out.sample_rate = (int)vi->rate;
    out.samples.clear();

    char read_buf[4096];
    int bitstream = 0;
    for (;;) {
        long r = ov_read(&vf, read_buf, sizeof(read_buf), 0, 2, 1, &bitstream);
        if (r == 0)
            break;
        if (r < 0) {
            ov_clear(&vf);
            Gmtoolkit::err("audio: ov_read decode error (rc=%ld)", r);
            return -1;
        }
        size_t old = out.samples.size();
        out.samples.resize(old + (size_t)r / 2);
        std::memcpy(out.samples.data() + old, read_buf, (size_t)r);
    }
    ov_clear(&vf);
    return 0;
}

static int wav_to_ogg(const uint8_t* wav, size_t wav_size, const AudioOpts& opt, std::vector<uint8_t>& out) {
    PcmBuf pcm;
    if (parse_wav(wav, wav_size, pcm) != 0)
        return -1;
    return encode_pcm_to_ogg(pcm, opt, out);
}

// Streaming re-encode: pulls PCM frames from ov_read and feeds vorbis_analysis without staging the
// whole decoded buffer. Falls back to the buffered path when resampling is requested.
static int ogg_stream_recompress(const uint8_t* ogg_in, size_t ogg_size, const AudioOpts& opt,
                                 std::vector<uint8_t>& out) {
    out.clear();

    OvMemSrc src{ ogg_in, ogg_size, 0 };
    ov_callbacks cb;
    cb.read_func = ov_mem_read;
    cb.seek_func = ov_mem_seek;
    cb.tell_func = ov_mem_tell;
    cb.close_func = nullptr;

    OggVorbis_File vf;
    if (ov_open_callbacks(&src, &vf, nullptr, 0, cb) != 0) {
        Gmtoolkit::err("audio: ov_open_callbacks failed");
        return -1;
    }
    vorbis_info* vi_in = ov_info(&vf, -1);
    int src_channels = vi_in->channels;
    int src_rate = (int)vi_in->rate;
    bool do_downmix = (opt.downmix && src_channels == 2);
    int out_channels = do_downmix ? 1 : src_channels;
    int out_rate = src_rate;
    vorbis_info vi;
    vorbis_info_init(&vi);
    int rc = (opt.bitrate > 0) ? vorbis_encode_init(&vi, out_channels, out_rate, -1, opt.bitrate * 1000L, -1)
                               : vorbis_encode_init_vbr(&vi, out_channels, out_rate, 0.4f);
    if (rc != 0) {
        Gmtoolkit::err("audio: vorbis_encode_init failed (channels=%d rate=%d rc=%d)", out_channels, out_rate, rc);
        ov_clear(&vf);
        vorbis_info_clear(&vi);
        return -1;
    }

    vorbis_comment vc;
    vorbis_comment_init(&vc);
    vorbis_comment_add_tag(&vc, "ENCODER", "gmtoolkit/libvorbis");

    vorbis_dsp_state vd;
    vorbis_block vb;
    vorbis_analysis_init(&vd, &vi);
    vorbis_block_init(&vd, &vb);

    ogg_stream_state os;
    ogg_stream_init(&os, OGG_STREAM_SERIALNO);

    ogg_packet hh, hc, hb;
    vorbis_analysis_headerout(&vd, &vc, &hh, &hc, &hb);
    ogg_stream_packetin(&os, &hh);
    ogg_stream_packetin(&os, &hc);
    ogg_stream_packetin(&os, &hb);

    ogg_page og;
    while (ogg_stream_flush(&os, &og) != 0) {
        out.insert(out.end(), og.header, og.header + og.header_len);
        out.insert(out.end(), og.body, og.body + og.body_len);
    }

    auto pump_packets = [&]() {
        while (vorbis_analysis_blockout(&vd, &vb) == 1) {
            vorbis_analysis(&vb, nullptr);
            vorbis_bitrate_addblock(&vb);
            ogg_packet op;
            while (vorbis_bitrate_flushpacket(&vd, &op)) {
                ogg_stream_packetin(&os, &op);
                while (ogg_stream_pageout(&os, &og) != 0) {
                    out.insert(out.end(), og.header, og.header + og.header_len);
                    out.insert(out.end(), og.body, og.body + og.body_len);
                }
            }
        }
    };

    constexpr int READ_BUF_BYTES = 4096;
    int16_t read_buf[READ_BUF_BYTES / 2];
    int bitstream = 0;
    bool decode_failed = false;
    for (;;) {
        long bytes = ov_read(&vf, (char*)read_buf, READ_BUF_BYTES, 0, 2, 1, &bitstream);
        if (bytes == 0)
            break;
        if (bytes < 0) {
            Gmtoolkit::err("audio: ov_read decode error (rc=%ld)", bytes);
            decode_failed = true;
            break;
        }
        long n_frames = bytes / 2 / src_channels;
        if (n_frames <= 0)
            continue;

        float** ana = vorbis_analysis_buffer(&vd, (int)n_frames);
        if (do_downmix) {
            for (long i = 0; i < n_frames; i++) {
                int32_t l = read_buf[i * 2];
                int32_t r = read_buf[i * 2 + 1];
                ana[0][i] = (float)((l + r) / 2) / 32768.0f;
            }
        } else {
            for (long i = 0; i < n_frames; i++) {
                for (int c = 0; c < src_channels; c++) {
                    ana[c][i] = (float)read_buf[i * src_channels + c] / 32768.0f;
                }
            }
        }
        vorbis_analysis_wrote(&vd, (int)n_frames);
        pump_packets();
    }

    if (!decode_failed) {
        vorbis_analysis_wrote(&vd, 0);
        while (vorbis_analysis_blockout(&vd, &vb) == 1) {
            vorbis_analysis(&vb, nullptr);
            vorbis_bitrate_addblock(&vb);
            ogg_packet op;
            while (vorbis_bitrate_flushpacket(&vd, &op)) {
                ogg_stream_packetin(&os, &op);
                while (ogg_stream_flush(&os, &og) != 0) {
                    out.insert(out.end(), og.header, og.header + og.header_len);
                    out.insert(out.end(), og.body, og.body + og.body_len);
                }
            }
        }
    }

    ogg_stream_clear(&os);
    vorbis_block_clear(&vb);
    vorbis_dsp_clear(&vd);
    vorbis_comment_clear(&vc);
    vorbis_info_clear(&vi);
    ov_clear(&vf);
    return decode_failed ? -1 : 0;
}

static int ogg_recompress(const uint8_t* ogg_in, size_t ogg_size, const AudioOpts& opt, std::vector<uint8_t>& out) {
    if (opt.resample <= 0)
        return ogg_stream_recompress(ogg_in, ogg_size, opt, out);

    PcmBuf pcm;
    if (decode_ogg_to_pcm(ogg_in, ogg_size, pcm) != 0)
        return -1;
    return encode_pcm_to_ogg(pcm, opt, out);
}

static int parse_audo_entries(AgrpState& st) {
    const uint8_t* buf = st.mmap_in.data;
    size_t buf_size = st.mmap_in.size;
    if (find_chunk(buf, buf_size, "AUDO", &st.audo_chunk_off, &st.audo_chunk_size) != 0) {
        return -1;
    }
    if (st.audo_chunk_size < 4)
        return 0;
    uint32_t count = r_u32(buf + st.audo_chunk_off);
    if (count == 0)
        return 0;
    if (4 + (size_t)count * 4 > st.audo_chunk_size)
        return -1;
    st.entries.resize(count);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t ptr = r_u32(buf + st.audo_chunk_off + 4 + (size_t)i * 4);
        if (ptr + 4 > buf_size)
            return -1;
        uint32_t esz = r_u32(buf + ptr);
        if (ptr + 4 + esz > buf_size)
            return -1;
        st.entries[i].src_off = ptr;
        st.entries[i].orig_size = esz;
        st.entries[i].action = 0;
    }
    return 0;
}

// AUDO entries are 4-byte aligned and the ptr table holds absolute file offsets, so layout has to
// account for both per-entry padding and the chunk's own base offset.
static size_t compute_new_audo_layout(const AgrpState& st, std::vector<uint8_t>& out_header) {
    uint32_t count = (uint32_t)st.entries.size();
    out_header.assign(4 + (size_t)count * 4, 0);
    w_u32(out_header.data(), count);

    size_t pos = out_header.size();
    for (uint32_t i = 0; i < count; i++) {
        uint32_t new_ptr_in_file = (uint32_t)(st.audo_chunk_off + pos);
        w_u32(out_header.data() + 4 + (size_t)i * 4, new_ptr_in_file);

        size_t esz = (st.entries[i].action == 1) ? st.entries[i].new_bytes.size() : st.entries[i].orig_size;
        pos += 4 + esz;
        if (i + 1 < count) {
            size_t file_pos = st.audo_chunk_off + pos;
            pos += (4 - (file_pos & 3)) & 3;
        }
    }
    return pos;
}

static bool stream_range(FILE* src, FILE* dst, size_t off, size_t len, uint8_t* buf, size_t bufsize) {
    if (fseek(src, (long)off, SEEK_SET) != 0)
        return false;
    while (len > 0) {
        size_t n = len < bufsize ? len : bufsize;
        if (fread(buf, 1, n, src) != n)
            return false;
        if (fwrite(buf, 1, n, dst) != n)
            return false;
        len -= n;
    }
    return true;
}

// Write a fresh file by streaming the original through, splicing in the new SOND blob and the
// rebuilt AUDO entries at their original chunk offsets, then atomic-rename onto the source path.
static int stream_flush(AgrpState& st, const char* path, size_t src_total_size) {
    std::vector<uint8_t> audo_header;
    size_t new_audo_payload_size = 0;
    if (st.dirty)
        new_audo_payload_size = compute_new_audo_layout(st, audo_header);

    bool has_sond = !st.sond_blob.empty();
    size_t sond_off = has_sond ? st.sond_chunk_off : 0;
    size_t sond_size = has_sond ? st.sond_blob.size() : 0;

    mapped_file_close(&st.mmap_in);

    FILE* src = fopen(path, "rb");
    if (!src) {
        perror(path);
        return -1;
    }
    std::string tmppath = std::string(path) + ".tmp";
    FILE* dst = fopen(tmppath.c_str(), "wb");
    if (!dst) {
        perror(tmppath.c_str());
        fclose(src);
        return -1;
    }

    constexpr size_t BUFSZ = 64 * 1024;
    std::vector<uint8_t> buf(BUFSZ);

    long long delta = 0;
    if (st.dirty)
        delta += (long long)new_audo_payload_size - (long long)st.audo_chunk_size;
    size_t new_total = (size_t)((long long)src_total_size + delta);

    bool ok = true;

    uint8_t hdr[8];
    if (fread(hdr, 1, 8, src) != 8)
        ok = false;
    else {
        w_u32(hdr + 4, (uint32_t)(new_total - 8));
        if (fwrite(hdr, 1, 8, dst) != 8)
            ok = false;
    }

    size_t cursor = 8;
    if (ok && has_sond) {
        if (sond_off > cursor) {
            if (!stream_range(src, dst, cursor, sond_off - cursor, buf.data(), BUFSZ))
                ok = false;
            cursor = sond_off;
        }
        if (ok && fwrite(st.sond_blob.data(), 1, st.sond_blob.size(), dst) != st.sond_blob.size())
            ok = false;
        cursor = sond_off + sond_size;
        if (ok && fseek(src, (long)cursor, SEEK_SET) != 0)
            ok = false;
    }

    if (ok && st.dirty) {
        size_t audo_hdr_size_off = st.audo_chunk_off - 4;
        if (audo_hdr_size_off > cursor) {
            if (!stream_range(src, dst, cursor, audo_hdr_size_off - cursor, buf.data(), BUFSZ))
                ok = false;
            cursor = audo_hdr_size_off;
        }
        if (ok) {
            uint8_t szhdr[4];
            w_u32(szhdr, (uint32_t)new_audo_payload_size);
            if (fwrite(szhdr, 1, 4, dst) != 4)
                ok = false;
        }
        if (ok && fwrite(audo_header.data(), 1, audo_header.size(), dst) != audo_header.size())
            ok = false;

        size_t local_pos = audo_header.size();
        uint32_t count = (uint32_t)st.entries.size();
        for (uint32_t i = 0; ok && i < count; i++) {
            auto& ent = st.entries[i];
            if (ent.action == 1) {
                uint8_t szhdr[4];
                w_u32(szhdr, (uint32_t)ent.new_bytes.size());
                if (fwrite(szhdr, 1, 4, dst) != 4)
                    ok = false;
                if (ok && fwrite(ent.new_bytes.data(), 1, ent.new_bytes.size(), dst) != ent.new_bytes.size())
                    ok = false;
                local_pos += 4 + ent.new_bytes.size();
            } else {
                if (!stream_range(src, dst, ent.src_off, 4 + (size_t)ent.orig_size, buf.data(), BUFSZ))
                    ok = false;
                local_pos += 4 + ent.orig_size;
            }
            if (ok && i + 1 < count) {
                size_t file_pos = st.audo_chunk_off + local_pos;
                size_t pad = (4 - (file_pos & 3)) & 3;
                if (pad) {
                    uint8_t zeros[4] = { 0, 0, 0, 0 };
                    if (fwrite(zeros, 1, pad, dst) != pad)
                        ok = false;
                    local_pos += pad;
                }
            }
        }
        cursor = st.audo_chunk_off + st.audo_chunk_size;
        if (ok && fseek(src, (long)cursor, SEEK_SET) != 0)
            ok = false;
    }

    if (ok && cursor < src_total_size) {
        if (!stream_range(src, dst, cursor, src_total_size - cursor, buf.data(), BUFSZ))
            ok = false;
    }

    fclose(src);
    fclose(dst);

    if (!ok) {
        std::remove(tmppath.c_str());
        return -1;
    }

    if (portable_rename(tmppath.c_str(), path) != 0) {
        perror(path);
        std::remove(tmppath.c_str());
        return -1;
    }
    return 0;
}

// SOND entries grew from 36 to 40 bytes in 2024.6. Detect from the inter-entry stride, or peek at the
// new field for the single-entry case.
static int parse_sond(const uint8_t* sond_blob, size_t sond_size, size_t sond_file_off, SondCtx& out) {
    out.chunk_off = sond_file_off;
    out.chunk_size = sond_size;
    if (sond_size < 4)
        return -1;
    out.count = r_u32(sond_blob);
    if (out.count == 0)
        return 0;
    if (4 + (size_t)out.count * 4 > sond_size)
        return -1;

    out.entry_size = 36;
    if (out.count >= 2) {
        uint32_t e0 = r_u32(sond_blob + 4);
        uint32_t e1 = r_u32(sond_blob + 8);
        if (e1 > e0 && (e1 - e0) == 40)
            out.entry_size = 40;
    } else {
        uint32_t e0 = r_u32(sond_blob + 4);
        size_t e0_local = (size_t)e0 - sond_file_off;
        if (e0_local + 36 <= sond_size && r_u32(sond_blob + e0_local + 32) != 0)
            out.entry_size = 40;
    }
    return 0;
}

static size_t sond_entry_off(const uint8_t* sond_blob, const SondCtx& ctx, uint32_t i) {
    size_t pp_local = 4 + (size_t)i * 4;
    uint32_t abs_off = r_u32(sond_blob + pp_local);
    return (size_t)abs_off - ctx.chunk_off;
}

// Compression flips Embedded off and Compressed on; the runtime ignores AUDO flags but trusts SOND's.
static void sond_mark_compressed(uint8_t* sond_blob, size_t sond_blob_size, const SondCtx& ctx, uint32_t sond_idx) {
    size_t eo = sond_entry_off(sond_blob, ctx, sond_idx);
    if (eo + 8 > sond_blob_size)
        return;
    uint32_t f = r_u32(sond_blob + eo + 4);
    f = (f & ~SOND_FLAG_EMBEDDED) | SOND_FLAG_COMPRESSED;
    w_u32(sond_blob + eo + 4, f);
}

static std::string resolve_audiogroup_path(const uint8_t* data_win, size_t data_size, const std::string& base_dir,
                                           uint32_t agrp_id) {
    size_t agrp_off, agrp_size;
    if (find_chunk(data_win, data_size, "AGRP", &agrp_off, &agrp_size) != 0)
        return "";
    if (agrp_size < 4 + (size_t)(agrp_id + 1) * 4)
        return "";

    std::string friendly;
    {
        size_t pp = agrp_off + 4 + (size_t)agrp_id * 4;
        uint32_t ent_off = r_u32(data_win + pp);
        if ((size_t)ent_off + 4 <= data_size) {
            uint32_t name_ptr = r_u32(data_win + ent_off);
            friendly = read_strg_at(data_win, data_size, name_ptr);
        }
    }

    std::string candidates[3];
    if (!friendly.empty()) {
        candidates[0] = base_dir + "/" + friendly;
        candidates[1] = base_dir + "/" + friendly + ".dat";
    }
    char numbered[64];
    snprintf(numbered, sizeof(numbered), "%s/audiogroup%u.dat", base_dir.c_str(), (unsigned)agrp_id);
    candidates[2] = numbered;

    for (const auto& c : candidates) {
        if (c.empty())
            continue;
        FILE* t = fopen(c.c_str(), "rb");
        if (t) {
            fclose(t);
            return c;
        }
    }
    return "";
}

extern "C" int compress_audio(const char* data_win_path, size_t min_size, int bitrate, bool downmix, int resample,
                              bool recompress, bool process_audiogroups, bool verbose, unsigned threads) {
    if (threads == 0)
        threads = (unsigned)std::thread::hardware_concurrency();
    if (threads == 0)
        threads = 1;
    AudioOpts opt = { bitrate, downmix, resample };

    std::unordered_map<uint32_t, AgrpState> groups;
    struct GroupsCleanup {
        std::unordered_map<uint32_t, AgrpState>* g;
        ~GroupsCleanup() {
            for (auto& kv : *g)
                mapped_file_close(&kv.second.mmap_in);
        }
    } guard{ &groups };

    AgrpState g0;
    g0.path = "";
    g0.loaded = false;
    g0.dirty = false;
    g0.sond_modified = false;
    if (mapped_file_open(data_win_path, &g0.mmap_in) != 0) {
        perror(data_win_path);
        return -1;
    }
    size_t src_total_size = g0.mmap_in.size;
    if (g0.mmap_in.size < 8 || memcmp(g0.mmap_in.data, "FORM", 4) != 0) {
        Gmtoolkit::tprint("[AUDIO] %s is not a GameMaker file\n", data_win_path);
        groups[0] = std::move(g0);
        return -1;
    }

    size_t sond_off = 0, sond_size = 0;
    if (find_chunk(g0.mmap_in.data, g0.mmap_in.size, "SOND", &sond_off, &sond_size) != 0 || sond_size < 4) {
        if (verbose)
            Gmtoolkit::tprint("[AUDIO] no SOND chunk; nothing to do.\n");
        groups[0] = std::move(g0);
        return 0;
    }
    g0.sond_blob.assign(g0.mmap_in.data + sond_off, g0.mmap_in.data + sond_off + sond_size);
    g0.sond_chunk_off = sond_off;

    SondCtx sond;
    if (parse_sond(g0.sond_blob.data(), g0.sond_blob.size(), sond_off, sond) != 0 || sond.count == 0) {
        if (verbose)
            Gmtoolkit::tprint("[AUDIO] no SOND entries; nothing to do.\n");
        groups[0] = std::move(g0);
        return 0;
    }

    std::string base_dir;
    {
        std::string p(data_win_path);
        size_t s = p.find_last_of("/\\");
        base_dir = (s == std::string::npos) ? std::string(".") : p.substr(0, s);
    }

    if (parse_audo_entries(g0) != 0) {
        Gmtoolkit::tprint("[AUDIO] cannot parse data file AUDO\n");
        groups[0] = std::move(g0);
        return -1;
    }
    g0.loaded = true;
    groups[0] = std::move(g0);
    AgrpState& dw = groups[0];

    auto load_group = [&](uint32_t agrp_id) -> AgrpState* {
        auto it = groups.find(agrp_id);
        if (it != groups.end())
            return it->second.loaded ? &it->second : nullptr;
        auto ins = groups.emplace(std::piecewise_construct, std::forward_as_tuple(agrp_id), std::forward_as_tuple());
        AgrpState& g = ins.first->second;
        g.path = resolve_audiogroup_path(dw.mmap_in.data, dw.mmap_in.size, base_dir, agrp_id);
        g.loaded = false;
        g.dirty = false;
        g.sond_modified = false;
        if (!g.path.empty()) {
            if (mapped_file_open(g.path.c_str(), &g.mmap_in) == 0) {
                if (parse_audo_entries(g) == 0) {
                    g.loaded = true;
                } else {
                    Gmtoolkit::tprint("[AUDIO] cannot parse %s AUDO\n", g.path.c_str());
                    mapped_file_close(&g.mmap_in);
                }
            }
        }
        return g.loaded ? &g : nullptr;
    };

    unsigned int n_wav = 0, n_ogg = 0, n_skipped = 0;
    size_t bytes_in = 0, bytes_out = 0;

    unsigned int total_groups = 0;
    std::unordered_map<uint32_t, bool> seen;
    for (uint32_t si = 0; si < sond.count; si++) {
        size_t eo = sond_entry_off(dw.sond_blob.data(), sond, si);
        if (eo + sond.entry_size > dw.sond_blob.size())
            continue;
        int32_t agrp_id = (int32_t)r_u32(dw.sond_blob.data() + eo + 28);
        int32_t aid = (int32_t)r_u32(dw.sond_blob.data() + eo + 32);
        if (agrp_id < 0 || aid < 0 || aid == (int32_t)0xFFFFFFFFu)
            continue;
        if (!process_audiogroups && agrp_id != 0)
            continue;
        seen[(uint32_t)agrp_id] = true;
    }
    total_groups = (unsigned int)seen.size();

    unsigned int processed_groups = 0;
    std::unordered_map<uint32_t, bool> group_announced;

    auto announce_group = [&](int32_t agrp_id) {
        if (group_announced.count((uint32_t)agrp_id))
            return;
        group_announced[(uint32_t)agrp_id] = true;
        processed_groups++;
        const char* label = "data file";
        std::string path;
        if (agrp_id != 0) {
            auto it = groups.find((uint32_t)agrp_id);
            if (it != groups.end() && !it->second.path.empty()) {
                path = it->second.path;
                size_t s = path.find_last_of("/\\");
                if (s != std::string::npos)
                    path = path.substr(s + 1);
                label = path.c_str();
            }
        }
        Gmtoolkit::tprint("[AUDIO] Compressing audiogroup %u/%u: %s\n", processed_groups, total_groups, label);
        std::fflush(stdout);
    };

    std::vector<EncodeTask> tasks;
    tasks.reserve(sond.count);

    for (uint32_t si = 0; si < sond.count; si++) {
        size_t eo = sond_entry_off(dw.sond_blob.data(), sond, si);
        if (eo + sond.entry_size > dw.sond_blob.size())
            continue;

        uint32_t flags = r_u32(dw.sond_blob.data() + eo + 4);
        int32_t agrp_id = (int32_t)r_u32(dw.sond_blob.data() + eo + 28);
        int32_t aid = (int32_t)r_u32(dw.sond_blob.data() + eo + 32);
        if (agrp_id < 0 || aid < 0 || aid == (int32_t)0xFFFFFFFFu)
            continue;
        if (!process_audiogroups && agrp_id != 0) {
            sond_mark_compressed(dw.sond_blob.data(), dw.sond_blob.size(), sond, si);
            dw.sond_modified = true;
            n_skipped++;
            continue;
        }

        AgrpState* g = load_group((uint32_t)agrp_id);
        if (!g)
            continue;
        if ((size_t)aid >= g->entries.size())
            continue;

        auto& ent = g->entries[aid];
        const uint8_t* payload = g->mmap_in.data + ent.src_off + 4;
        bool below_min = ent.orig_size < min_size;
        bool wav = is_riff(payload, ent.orig_size);
        bool ogg = is_oggs(payload, ent.orig_size);

        if (below_min) {
            if (ogg && ((flags & SOND_FLAG_COMPRESSED) == 0 || (flags & SOND_FLAG_EMBEDDED) != 0)) {
                sond_mark_compressed(dw.sond_blob.data(), dw.sond_blob.size(), sond, si);
                dw.sond_modified = true;
            }
            n_skipped++;
            continue;
        }

        if (wav) {
            announce_group(agrp_id);
            tasks.push_back({ TK_WAV, si, agrp_id, aid, payload, ent.orig_size });
        } else if (ogg && recompress) {
            announce_group(agrp_id);
            tasks.push_back({ TK_OGG_RECOMPRESS, si, agrp_id, aid, payload, ent.orig_size });
        } else if (ogg) {
            if ((flags & SOND_FLAG_COMPRESSED) == 0 || (flags & SOND_FLAG_EMBEDDED) != 0) {
                sond_mark_compressed(dw.sond_blob.data(), dw.sond_blob.size(), sond, si);
                dw.sond_modified = true;
            }
            n_skipped++;
        } else {
            n_skipped++;
        }
    }

    std::vector<EncodeResult> results(tasks.size());
    std::atomic<size_t> next_task{ 0 };
    std::atomic<size_t> total_in{ 0 };
    std::atomic<size_t> total_out{ 0 };
    std::atomic<int> first_error{ 0 };

    // Dynamic work-stealing pool: each worker pulls the next task atomically until tasks run out.
    auto worker = [&]() {
        for (;;) {
            size_t idx = next_task.fetch_add(1, std::memory_order_relaxed);
            if (idx >= tasks.size())
                return;
            if (first_error.load(std::memory_order_acquire))
                return;
            const EncodeTask& t = tasks[idx];
            EncodeResult& r = results[idx];
            Gmtoolkit::tprint("[AGRP %d] Compress AUDO entry %d\n", t.agrp_id, t.aid);
            std::fflush(stdout);
            int rc = (t.kind == TK_WAV) ? wav_to_ogg(t.payload, t.payload_size, opt, r.bytes)
                                        : ogg_recompress(t.payload, t.payload_size, opt, r.bytes);
            if (rc != 0) {
                first_error.store(1, std::memory_order_release);
                return;
            }
            r.orig_size = t.payload_size;
            r.ok = true;
            total_in.fetch_add(t.payload_size, std::memory_order_relaxed);
            total_out.fetch_add(r.bytes.size(), std::memory_order_relaxed);
        }
    };

    unsigned worker_count = threads;
    if (worker_count > tasks.size())
        worker_count = (unsigned)tasks.size();
    if (worker_count < 1)
        worker_count = 1;
    std::vector<std::thread> pool;
    pool.reserve(worker_count);
    for (unsigned i = 0; i + 1 < worker_count; i++)
        pool.emplace_back(worker);
    if (worker_count > 0)
        worker();
    for (auto& th : pool)
        th.join();
    if (first_error.load())
        return -1;

    for (size_t i = 0; i < tasks.size(); i++) {
        if (!results[i].ok)
            continue;
        const EncodeTask& t = tasks[i];
        auto git = groups.find((uint32_t)t.agrp_id);
        if (git == groups.end())
            continue;
        AgrpState& g = git->second;
        if ((size_t)t.aid >= g.entries.size())
            continue;
        g.entries[t.aid].new_bytes = std::move(results[i].bytes);
        g.entries[t.aid].action = 1;
        g.dirty = true;
        sond_mark_compressed(dw.sond_blob.data(), dw.sond_blob.size(), sond, t.sond_idx);
        dw.sond_modified = true;
        if (t.kind == TK_WAV)
            n_wav++;
        else
            n_ogg++;
    }
    bytes_in = total_in.load();
    bytes_out = total_out.load();

    if (dw.dirty || dw.sond_modified) {
        if (stream_flush(dw, data_win_path, src_total_size) != 0)
            return -1;
    }
    int siblings_written = 0;
    for (auto& kv : groups) {
        if (kv.first == 0)
            continue;
        AgrpState& g = kv.second;
        if (!g.dirty)
            continue;
        size_t g_total = g.mmap_in.size;
        if (stream_flush(g, g.path.c_str(), g_total) != 0)
            return -1;
        siblings_written++;
    }

    if (verbose || n_wav || n_ogg || siblings_written) {
        Gmtoolkit::tprint("[AUDIO] %u WAV->OGG, %u OGG->OGG, %u skipped; "
                          "%d sibling file(s) rewritten. (%.1f MB -> %.1f MB.)\n",
                          n_wav, n_ogg, n_skipped, siblings_written, bytes_in / 1048576.0, bytes_out / 1048576.0);
    }
    return 0;
}

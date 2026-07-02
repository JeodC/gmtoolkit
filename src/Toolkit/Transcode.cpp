// SPDX-License-Identifier: MIT

#include "Toolkit/Transcode.h"
#include "Toolkit/Log.h"

#ifndef GMTOOLKIT_HAVE_FFMPEG

// Stub for builds without FFmpeg: keeps the symbol, errors if reached.
namespace Gmtoolkit {
int transcode_video(const char* in_path, const char* out_path, const TranscodeOpts&) {
    (void)in_path;
    (void)out_path;
    Gmtoolkit::err("transcode: this gmtoolkit build has no video support "
                   "(configure with -DGMTOOLKIT_ENABLE_TRANSCODE=ON)");
    return 1;
}
} // namespace Gmtoolkit

#else

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <cmath>

namespace Gmtoolkit {

namespace {

// Snap a source timestamp onto the output fps grid; already-emitted slots drop.
int64_t out_slot_for(double ts_seconds, int fps) {
    return (int64_t)std::llround(ts_seconds * fps);
}

struct Ctx {
    AVFormatContext* ifmt = nullptr;
    AVFormatContext* ofmt = nullptr;
    AVCodecContext* dec = nullptr;
    AVCodecContext* enc = nullptr;
    SwsContext* sws = nullptr;
    AVFrame* dec_frame = nullptr;
    AVFrame* enc_frame = nullptr;
    AVPacket* pkt = nullptr;     // demux input
    AVPacket* opkt = nullptr;    // encoder output

    ~Ctx() {
        if (sws)
            sws_freeContext(sws);
        if (dec_frame)
            av_frame_free(&dec_frame);
        if (enc_frame)
            av_frame_free(&enc_frame);
        if (pkt)
            av_packet_free(&pkt);
        if (opkt)
            av_packet_free(&opkt);
        if (dec)
            avcodec_free_context(&dec);
        if (enc)
            avcodec_free_context(&enc);
        if (ofmt) {
            if (ofmt->pb && !(ofmt->oformat->flags & AVFMT_NOFILE))
                avio_closep(&ofmt->pb);
            avformat_free_context(ofmt);
        }
        if (ifmt)
            avformat_close_input(&ifmt);
    }
};

// Mux whatever packets the encoder has ready.
int drain_encoder(Ctx& c, int out_v_index, AVRational enc_tb, AVRational mux_tb) {
    for (;;) {
        int ret = avcodec_receive_packet(c.enc, c.opkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return 0;
        if (ret < 0) {
            Gmtoolkit::err("transcode: encode failed (%d)", ret);
            return -1;
        }
        c.opkt->stream_index = out_v_index;
        av_packet_rescale_ts(c.opkt, enc_tb, mux_tb);
        ret = av_interleaved_write_frame(c.ofmt, c.opkt);
        av_packet_unref(c.opkt);
        if (ret < 0) {
            Gmtoolkit::err("transcode: mux (video) failed (%d)", ret);
            return -1;
        }
    }
}

} // namespace

int transcode_video(const char* in_path, const char* out_path, const TranscodeOpts& opt) {
    av_log_set_level(AV_LOG_ERROR);
    Ctx c;

    // ---- open input ----
    if (avformat_open_input(&c.ifmt, in_path, nullptr, nullptr) < 0) {
        Gmtoolkit::err("transcode: cannot open input '%s'", in_path);
        return 1;
    }
    if (avformat_find_stream_info(c.ifmt, nullptr) < 0) {
        Gmtoolkit::err("transcode: no stream info in '%s'", in_path);
        return 1;
    }

    int v_idx = av_find_best_stream(c.ifmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (v_idx < 0) {
        Gmtoolkit::err("transcode: no video stream in '%s'", in_path);
        return 1;
    }
    int a_idx = av_find_best_stream(c.ifmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    AVStream* in_v = c.ifmt->streams[v_idx];

    // ---- video decoder ----
    const AVCodec* dcodec = avcodec_find_decoder(in_v->codecpar->codec_id);
    if (!dcodec) {
        Gmtoolkit::err("transcode: no decoder for source video codec");
        return 1;
    }
    c.dec = avcodec_alloc_context3(dcodec);
    avcodec_parameters_to_context(c.dec, in_v->codecpar);
    if (avcodec_open2(c.dec, dcodec, nullptr) < 0) {
        Gmtoolkit::err("transcode: cannot open video decoder");
        return 1;
    }

    // ---- output container (MP4) ----
    if (avformat_alloc_output_context2(&c.ofmt, nullptr, "mp4", out_path) < 0 || !c.ofmt) {
        Gmtoolkit::err("transcode: cannot allocate MP4 output");
        return 1;
    }

    // ---- MPEG-4 video encoder ----
    const AVCodec* ecodec = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
    if (!ecodec) {
        Gmtoolkit::err("transcode: MPEG-4 encoder missing from this build");
        return 1;
    }
    c.enc = avcodec_alloc_context3(ecodec);
    // Target dims default to source; mask to even (yuv420p requires it). sws scales.
    c.enc->width = (opt.width > 0 ? opt.width : c.dec->width) & ~1;
    c.enc->height = (opt.height > 0 ? opt.height : c.dec->height) & ~1;
    c.enc->pix_fmt = AV_PIX_FMT_YUV420P;
    c.enc->time_base = AVRational{ 1, opt.fps };
    c.enc->framerate = AVRational{ opt.fps, 1 };
    c.enc->gop_size = opt.fps; // ~1s keyframe interval
    c.enc->max_b_frames = 0;
    c.enc->bit_rate = opt.video_bitrate;
    c.enc->rc_max_rate = opt.rc_max_rate;
    c.enc->rc_buffer_size = (int)opt.rc_buffer_size;
    if (c.ofmt->oformat->flags & AVFMT_GLOBALHEADER)
        c.enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (avcodec_open2(c.enc, ecodec, nullptr) < 0) {
        Gmtoolkit::err("transcode: cannot open MPEG-4 encoder");
        return 1;
    }

    AVStream* out_v = avformat_new_stream(c.ofmt, nullptr);
    avcodec_parameters_from_context(out_v->codecpar, c.enc);
    out_v->time_base = c.enc->time_base;
    int out_v_index = out_v->index;

    // ---- audio stream, stream-copied ----
    int out_a_index = -1;
    if (a_idx >= 0) {
        AVStream* in_a = c.ifmt->streams[a_idx];
        AVStream* out_a = avformat_new_stream(c.ofmt, nullptr);
        if (avcodec_parameters_copy(out_a->codecpar, in_a->codecpar) >= 0) {
            out_a->codecpar->codec_tag = 0;
            out_a->time_base = in_a->time_base;
            out_a_index = out_a->index;
        }
    }

    if (!(c.ofmt->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&c.ofmt->pb, out_path, AVIO_FLAG_WRITE) < 0) {
            Gmtoolkit::err("transcode: cannot open output '%s'", out_path);
            return 1;
        }
    }
    if (avformat_write_header(c.ofmt, nullptr) < 0) {
        Gmtoolkit::err("transcode: cannot write MP4 header");
        return 1;
    }

    c.sws = sws_getContext(c.dec->width, c.dec->height, c.dec->pix_fmt, c.enc->width, c.enc->height, c.enc->pix_fmt,
                           SWS_BILINEAR, nullptr, nullptr, nullptr);
    c.dec_frame = av_frame_alloc();
    c.enc_frame = av_frame_alloc();
    c.pkt = av_packet_alloc();
    c.opkt = av_packet_alloc();
    c.enc_frame->format = c.enc->pix_fmt;
    c.enc_frame->width = c.enc->width;
    c.enc_frame->height = c.enc->height;
    if (av_frame_get_buffer(c.enc_frame, 0) < 0) {
        Gmtoolkit::err("transcode: cannot allocate frame buffer");
        return 1;
    }

    int64_t last_slot = -1;
    auto encode_frame = [&](AVFrame* f) -> int {
        if (avcodec_send_frame(c.enc, f) < 0) {
            Gmtoolkit::err("transcode: encoder rejected frame");
            return -1;
        }
        return drain_encoder(c, out_v_index, c.enc->time_base, out_v->time_base);
    };

    // ---- demux/transcode loop ----
    int rc = 0;
    while (av_read_frame(c.ifmt, c.pkt) >= 0) {
        if (c.pkt->stream_index == v_idx) {
            if (avcodec_send_packet(c.dec, c.pkt) >= 0) {
                while (avcodec_receive_frame(c.dec, c.dec_frame) >= 0) {
                    int64_t bts = c.dec_frame->best_effort_timestamp;
                    if (bts == AV_NOPTS_VALUE)
                        bts = c.dec_frame->pts;
                    double ts = (bts == AV_NOPTS_VALUE) ? 0.0 : bts * av_q2d(in_v->time_base);
                    int64_t slot = out_slot_for(ts, opt.fps);
                    if (slot <= last_slot) {
                        av_frame_unref(c.dec_frame);
                        continue; // decimated away
                    }
                    last_slot = slot;

                    if (av_frame_make_writable(c.enc_frame) < 0) {
                        rc = -1;
                        break;
                    }
                    sws_scale(c.sws, c.dec_frame->data, c.dec_frame->linesize, 0, c.dec->height, c.enc_frame->data,
                              c.enc_frame->linesize);
                    c.enc_frame->pts = slot;
                    av_frame_unref(c.dec_frame);
                    if (encode_frame(c.enc_frame) < 0) {
                        rc = -1;
                        break;
                    }
                }
            }
        } else if (c.pkt->stream_index == a_idx && out_a_index >= 0) {
            AVStream* in_a = c.ifmt->streams[a_idx];
            av_packet_rescale_ts(c.pkt, in_a->time_base, c.ofmt->streams[out_a_index]->time_base);
            c.pkt->stream_index = out_a_index;
            c.pkt->pos = -1;
            if (av_interleaved_write_frame(c.ofmt, c.pkt) < 0) {
                Gmtoolkit::err("transcode: mux (audio) failed");
                rc = -1;
            }
        }
        av_packet_unref(c.pkt);
        if (rc != 0)
            break;
    }

    // ---- flush encoder + finalize ----
    if (rc == 0) {
        avcodec_send_frame(c.enc, nullptr);
        drain_encoder(c, out_v_index, c.enc->time_base, out_v->time_base);
        av_write_trailer(c.ofmt);
    }

    if (rc == 0)
        Gmtoolkit::msg("transcode: %s -> %s (MPEG-4, %d fps)", in_path, out_path, opt.fps);
    return rc == 0 ? 0 : 1;
}

} // namespace Gmtoolkit

#endif // GMTOOLKIT_HAVE_FFMPEG

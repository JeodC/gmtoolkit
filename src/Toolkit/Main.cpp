// SPDX-License-Identifier: MIT

#include "astcenc.h"
#include "GMSLib/Compiler/CompileGroup.h"
#include "GMSLib/Compiler/CompileResult.h"
#include "GMSLib/GMSData.h"
#include "GMSLib/GMSGameContext.h"
#include "GMSLib/GMSIO.h"
#include "GMSLib/Models/GMSCode.h"
#include "GMSLib/Models/GMSString.h"
#include "GMSLib/SaveBackend/Stamp.h"
#include "Toolkit/Codec/Txtr.h"
#include "Toolkit/DebugOps.h"
#include "Toolkit/ExtractTextures.h"
#include "Toolkit/IO.h"
#include "Toolkit/Log.h"
#include "Toolkit/Options.h"
#include "Toolkit/PatchShaders.h"
#include "Toolkit/Platform.h"
#include "Toolkit/Verify.h"
#include "Toolkit/Version.h"

#include <chrono>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <unordered_set>
#include <vector>

extern const struct block_info BLOCKS[];
extern astcenc_context* astc_make_context(int block_x, int block_y, float quality, unsigned threads);
extern int compress_rgba_to_pvr(const uint8_t* rgba, int w, int h, const char* out_path, const struct block_info* blk,
                                astcenc_context* ctx, unsigned threads, size_t max_strip);

extern TxtrFormat detect_blob_format(const uint8_t* p, size_t len);
extern size_t scan_blob_len(TxtrFormat fmt, const uint8_t* src, size_t src_left);
extern int decode_blob_to_rgba(TxtrFormat fmt, const uint8_t* blob, size_t blob_len, uint8_t** out_rgba, int* out_w,
                               int* out_h);
extern int build_stub_for_format(TxtrFormat fmt, unsigned int idx, uint8_t** out, size_t* out_len);
extern long compact_txtr(FILE* f, size_t txtr_start, size_t txtr_size, uint8_t** stubs, size_t* stub_lens,
                         unsigned int count, size_t entry_size);

extern int run_repack(const char* data_win, const char* out_dir, const struct block_info* blk, float quality,
                      size_t max_strip, unsigned threads, int page_size, int max_dims, int max_area);

extern uint32_t parse_flag_list(const char* s);
extern int toggle_flags_and_uid_in_file(const char* path, uint32_t set_mask, uint32_t clear_mask);

extern int set_bytecode_version(const char* path, int target);

extern "C" int compress_audio(const char* data_win_path, size_t min_size, int bitrate, bool downmix, int resample,
                              bool recompress, bool process_audiogroups, bool verbose, unsigned threads);

static int parse_quality(const char* s, float* q) {
    while (*s == '-')
        s++;
    if (!strcmp(s, "fast")) {
        *q = ASTCENC_PRE_FAST;
        return 0;
    }
    if (!strcmp(s, "medium")) {
        *q = ASTCENC_PRE_MEDIUM;
        return 0;
    }
    if (!strcmp(s, "thorough")) {
        *q = ASTCENC_PRE_THOROUGH;
        return 0;
    }
    if (!strcmp(s, "fastest")) {
        *q = ASTCENC_PRE_FASTEST;
        return 0;
    }
    return -1;
}

static const struct block_info* find_block(const char* s) {
    for (int i = 0; BLOCKS[i].name; i++) {
        if (!strcmp(s, BLOCKS[i].name))
            return &BLOCKS[i];
    }
    return NULL;
}

static bool is_absolute_path(const std::string& path) {
    if (path.empty())
        return false;
    if (path[0] == '/' || path[0] == '\\')
        return true;
#ifdef _WIN32
    if (path.size() >= 3 && path[1] == ':' && (path[2] == '/' || path[2] == '\\'))
        return true;
#endif
    return false;
}

static std::string parent_dir(const char* path) {
    const char* last = NULL;
    for (const char* p = path; *p; p++) {
        if (*p == '/' || *p == '\\')
            last = p;
    }
    if (!last)
        return std::string();
    return std::string(path, (size_t)(last - path));
}

static void resolve_against(std::string& p, const std::string& base) {
    if (p.empty() || base.empty() || is_absolute_path(p))
        return;
    p = base + "/" + p;
}

static int is_known_gm_filename(const char* path) {
    static const char* KNOWN[] = {
        "data.win", "game.droid", "game.unx", "game.ios", "data.bin", NULL,
    };
    const char* base = path;
    for (const char* p = path; *p; p++) {
        if (*p == '/' || *p == '\\')
            base = p + 1;
    }
    for (int i = 0; KNOWN[i]; i++) {
        if (!strcmp(base, KNOWN[i]))
            return 1;
    }

    if (!strncmp(base, "audiogroup", 10)) {
        size_t n = strlen(base);
        if (n >= 4 && !strcmp(base + n - 4, ".dat"))
            return 1;
    }
    return 0;
}

static void usage(const char* prog) {
    Gmtoolkit::tprint("Usage: %s DATAFILE [operations] [options]\n"
                      "\n"
                      "DATAFILE is the GameMaker file:\n"
                      "  data.win  game.droid  game.unx  game.ios  data.bin\n"
                      "\n"
                      "All operations are opt-in; at least one is required.\n"
                      "\n"
                      "Operations:\n"
                      "  --config PATH                   Per-port JSON config -- bundles\n"
                      "                                  keep_inline_pages + shader_patches\n"
                      "  --threads N                     Worker thread count (default = nproc).\n"
                      "                                  Drives astcenc texture encoding, repack,\n"
                      "                                  and audio compression (one OGG encoder\n"
                      "  --bytecode-version N            Retarget GEN8.BytecodeVersion (15 -> 16 today)\n"
                      "  --externalize-textures DIR      Externalize each TXTR atlas to DIR/<idx>.pvr\n"
                      "                                  (one PVR per atlas, ASTC-compressed)\n"
                      "  --repack                        Bin-pack TPIs into tighter atlases (inline,\n"
                      "                                  lossless re-encode in the source format).\n"
                      "                                  Combine with --externalize-textures DIR to\n"
                      "                                  ASTC-compress the new atlases into PVRs.\n"
                      "  --set-flags A[,B,...]           Set GEN8 InfoFlags bits\n"
                      "  --clear-flags A[,B,...]         Clear GEN8 InfoFlags bits\n"
                      "  --compress-audio                Compress WAV/OGG entries in AUDO\n"
                      "  --check                         Probe for prior gmtoolkit patch and exit\n"
                      "                                  (0 = patched + config matches, 1 = unpatched,\n"
                      "                                  2 = patched + config differs)\n"
                      "  --force                         Bypass the already-patched sentinel check\n"
                      "\n"
                      "Texture options:\n"
                      "  --block 4x4|5x5|6x6             ASTC block size (default 4x4, externalize only)\n"
                      "  --quality fastest|fast|medium|thorough\n"
                      "                                  astcenc preset (default medium, externalize only)\n"
                      "                                  per worker, one entry at a time).\n"
                      "  --keep-color RRGGBB[,...]       Keep pages containing these colors inline\n"
                      "                                  (use for palette-key shaders, e.g. FFFF40)\n"
                      "  --keep-pages N[,...]            Keep these page indices inline\n"
                      "\n"
                      "Repack-only options (with --repack):\n"
                      "  --page-size N                   Atlas size (default 1024)\n"
                      "  --max-dims N                    Solo TPIs larger than N px\n"
                      "  --max-area N                    Solo TPIs with w*h larger than N\n"
                      "\n"
                      "Audio options (with --compress-audio; requires oggenc/oggdec on PATH):\n"
                      "  --min-audio-size N              Only compress entries >= N bytes\n"
                      "                                  (default 1048576 = 1 MiB)\n"
                      "  --bitrate N                     OGG bitrate kbps (0 = oggenc default)\n"
                      "  --downmix                       Stereo to mono\n"
                      "  --resample N                    Resample to N Hz\n"
                      "  --recompress-ogg                Also recompress existing OGG entries\n"
                      "\n"
                      "Flag names (for --set-flags / --clear-flags):\n"
                      "  Fullscreen SyncVertex1 SyncVertex2 Interpolate Scale\n"
                      "  ShowCursor Sizeable ScreenKey SyncVertex3 BorderlessWindow\n",
                      prog);
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    auto t_start = std::chrono::steady_clock::now();

    const char* data_win = NULL;
    const char* config_path = NULL;
    bool info_mode = false;
    bool verify_mode = false;
    bool check_mode = false;
    bool force_mode = false;
    const char* pool_test_string = NULL;
    const char* pool_test_out = NULL;
    Options opt;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--config") && i + 1 < argc) {
            config_path = argv[i + 1];
            break;
        }
    }
    if (config_path) {
        if (load_config_json(config_path, opt) != 0)
            return 2;
    }

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--block") && i + 1 < argc)
            opt.block_name = argv[++i];
        else if (!strcmp(argv[i], "--quality") && i + 1 < argc)
            opt.quality = argv[++i];
        else if (!strcmp(argv[i], "--max-strip-pixels") && i + 1 < argc)
            opt.max_strip = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--threads") && i + 1 < argc) {
            opt.threads = strtol(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--repack")) {
            opt.repack = true;
        } else if (!strcmp(argv[i], "--externalize-textures") && i + 1 < argc) {
            opt.externalize_dir = argv[++i];
        } else if (!strcmp(argv[i], "--page-size") && i + 1 < argc) {
            opt.page_size = (int)strtol(argv[++i], NULL, 10);
            if (opt.page_size < 64)
                opt.page_size = 64;
        } else if (!strcmp(argv[i], "--max-dims") && i + 1 < argc) {
            opt.max_dims = (int)strtol(argv[++i], NULL, 10);
            if (opt.max_dims < 1)
                opt.max_dims = 1;
        } else if (!strcmp(argv[i], "--max-area") && i + 1 < argc) {
            opt.max_area = (int)strtol(argv[++i], NULL, 10);
            if (opt.max_area < 1)
                opt.max_area = 1;
        } else if (!strcmp(argv[i], "--set-flags") && i + 1 < argc) {
            opt.set_flags = parse_flag_list(argv[++i]);
            if (opt.set_flags == UINT32_MAX)
                return 2;
        } else if (!strcmp(argv[i], "--clear-flags") && i + 1 < argc) {
            opt.clear_flags = parse_flag_list(argv[++i]);
            if (opt.clear_flags == UINT32_MAX)
                return 2;
        } else if (!strcmp(argv[i], "--bytecode-version") && i + 1 < argc) {
            opt.bytecode_target = (int)strtol(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--keep-color") && i + 1 < argc) {
            const char* s = argv[++i];
            while (*s) {
                char hex[7] = { 0 };
                int j = 0;
                while (j < 6 && *s && *s != ',')
                    hex[j++] = *s++;
                if (j == 6) {
                    uint32_t c = (uint32_t)strtoul(hex, NULL, 16) & 0xFFFFFFu;
                    opt.keep_colors.push_back(c);
                }
                if (*s == ',')
                    s++;
            }
        } else if (!strcmp(argv[i], "--keep-pages") && i + 1 < argc) {
            const char* s = argv[++i];
            while (*s) {
                char* end = NULL;
                long v = strtol(s, &end, 10);
                if (end == s)
                    break;
                if (v >= 0)
                    opt.keep_pages.push_back((unsigned int)v);
                s = end;
                if (*s == ',')
                    s++;
            }
        } else if (!strcmp(argv[i], "--config") && i + 1 < argc) {
            i++;
        } else if (!strcmp(argv[i], "--info")) {
            info_mode = true;
        } else if (!strcmp(argv[i], "--verify")) {
            verify_mode = true;
        } else if (!strcmp(argv[i], "--check")) {
            check_mode = true;
        } else if (!strcmp(argv[i], "--force")) {
            force_mode = true;
        } else if (!strcmp(argv[i], "--pool-test") && i + 2 < argc) {
            pool_test_string = argv[++i];
            pool_test_out = argv[++i];
        } else if (!strcmp(argv[i], "--compress-audio")) {
            opt.compress_audio = true;
        } else if (!strcmp(argv[i], "--verbose") || !strcmp(argv[i], "-v")) {
            opt.verbose = true;
        } else if (!strcmp(argv[i], "--no-audiogroups")) {
            opt.skip_audiogroups = true;
        } else if (!strcmp(argv[i], "--min-audio-size") && i + 1 < argc) {
            opt.audio_min_size = strtoull(argv[++i], NULL, 10);
        } else if ((!strcmp(argv[i], "--bitrate") || !strcmp(argv[i], "--audio-bitrate")) && i + 1 < argc) {
            opt.audio_bitrate = (int)strtol(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--downmix") || !strcmp(argv[i], "--audio-downmix")) {
            opt.audio_downmix = true;
        } else if ((!strcmp(argv[i], "--resample") || !strcmp(argv[i], "--audio-resample")) && i + 1 < argc) {
            opt.audio_resample = (int)strtol(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--recompress-ogg") || !strcmp(argv[i], "--audio-recompress")) {
            opt.audio_recompress = true;
        } else if (argv[i][0] != '-') {
            if (!data_win)
                data_win = argv[i];
            else {
                usage(argv[0]);
                return 2;
            }
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (!data_win) {
        usage(argv[0]);
        return 2;
    }

#if defined(_WIN32)
    {
        DWORD pids[2];
        if (GetConsoleProcessList(pids, 2) == 1) {
            Gmtoolkit::set_drag_drop_mode();
            // Drag-and-drop has no terminal to read; keep a sibling log for diagnosis.
            Gmtoolkit::start_output_tee(get_exe_dir() + "/gmtoolkit.log");
        }
    }
#endif

    // If a sibling gmtoolkit.json sits next to the executable, treat it as the implicit config.
    std::string auto_config_storage;
    if (!config_path && !info_mode && !pool_test_string) {
        std::string sibling = get_exe_dir() + "/gmtoolkit.json";
        FILE* probe = fopen(sibling.c_str(), "rb");
        if (probe) {
            fclose(probe);
            auto_config_storage = sibling;
            config_path = auto_config_storage.c_str();
            Gmtoolkit::msg("Auto-loaded config: %s", config_path);
            if (load_config_json(config_path, opt) != 0)
                return 2;
        }
    }

    if (verify_mode)
        return Gmtoolkit::verify_output(data_win);
    if (info_mode)
        return Cli::dispatch_info(data_win);
    if (pool_test_string) {
        return Cli::dispatch_pool_test(data_win, pool_test_string, pool_test_out);
    }
    if (check_mode) {
        std::string found;
        if (GMSLib::SaveBackend::find_sentinel(data_win, &found) == 0) {
            Gmtoolkit::tprint("%s\n", found.c_str());
            if (config_path) {
                std::string expected =
                    GMSLib::SaveBackend::make_sentinel(GMSLib::SaveBackend::compute_config_hash(config_path));
                if (found != expected) {
                    Gmtoolkit::tprint("config mismatch (expected %s)\n", expected.c_str());
                    return 2;
                }
            }
            return 0;
        }
        Gmtoolkit::tprint("unpatched\n");
        return 1;
    }

    {
        std::string base = parent_dir(data_win);
        resolve_against(opt.externalize_dir, base);
    }

    if (opt.threads <= 0)
        opt.threads = (long)std::thread::hardware_concurrency();
    if (opt.threads <= 0)
        opt.threads = 1;

    auto run_code_patches = [&]() -> int {
        if (opt.code_patches.empty())
            return 0;
        GMSLib::GMSData Data;
        if (GMSLib::LoadFromFile(data_win, Data) != 0) {
            Gmtoolkit::err("code_patches: failed to load %s", data_win);
            return 13;
        }
        GMSLib::GMSGameContext Ctx(Data);
        GMSLib::CompileGroup Group(Ctx);

        for (const auto& cp : opt.code_patches) {
            std::string gml_path = cp.gml_path;
            if (!is_absolute_path(gml_path) && !opt.config_dir.empty()) {
                gml_path = opt.config_dir + "/" + gml_path;
            }
            std::string src;
            if (Gmtoolkit::slurp(gml_path.c_str(), src) != 0)
                return 13;

            bool exists = Data.CodeByName.find(cp.entry_name) != Data.CodeByName.end();
            Group.QueueCodeReplace(cp.entry_name, std::move(src));
            Gmtoolkit::msg("%s %s with %s", exists ? "Replacing" : "Adding", cp.entry_name.c_str(),
                           cp.gml_path.c_str());
        }

        auto Result = Group.Compile();
        if (!Result.Successful) {
            Gmtoolkit::err("code_patches: compile failed\n%s", Result.PrintAllErrors(true).c_str());
            return 13;
        }
        if (GMSLib::SaveToFile(data_win, Data) != 0) {
            Gmtoolkit::err("code_patches: save failed");
            return 13;
        }
        Gmtoolkit::tprint("Applied %zu code patches.\n", opt.code_patches.size());
        return 0;
    };

    bool want_textures = !opt.externalize_dir.empty() || opt.repack;
    bool any_op = want_textures || (opt.bytecode_target != 0) || (opt.set_flags || opt.clear_flags) ||
                  opt.compress_audio || !opt.shader_patches.empty() || !opt.code_patches.empty();
    if (!any_op) {
        Gmtoolkit::tprint("Nothing to do. Pass at least one operation.\n\n");
        usage(argv[0]);
        return 2;
    }

    if (!is_known_gm_filename(data_win)) {
        Gmtoolkit::tprint("Refusing to process '%s': not a recognised GameMaker data "
                          "filename. Expected one of: data.win, game.droid, game.unx, "
                          "game.ios, data.bin.\n",
                          data_win);
        return 2;
    }

    const struct block_info* blk = find_block(opt.block_name.c_str());
    if (!blk) {
        Gmtoolkit::err("Unknown block: %s", opt.block_name.c_str());
        return 2;
    }
    float quality;
    if (parse_quality(opt.quality.c_str(), &quality) != 0) {
        Gmtoolkit::err("Unknown quality: %s", opt.quality.c_str());
        return 2;
    }
    const char* out_dir = opt.externalize_dir.empty() ? NULL : opt.externalize_dir.c_str();
    if (out_dir && portable_mkdir(out_dir) != 0) {
        perror(out_dir);
        return 3;
    }

    // Sentinel pre-check: detect data files this tool has already patched, so
    // re-running the patchscript on an already-processed payload is a no-op
    // instead of double-applying (audio recompress, texture re-externalize).
    std::string expected_sentinel =
        GMSLib::SaveBackend::make_sentinel(GMSLib::SaveBackend::compute_config_hash(config_path));
    if (!force_mode) {
        std::string found;
        if (GMSLib::SaveBackend::find_sentinel(data_win, &found) == 0) {
            if (found == expected_sentinel) {
                Gmtoolkit::msg("Already patched (%s); nothing to do.", found.c_str());
                Gmtoolkit::pause_if_drag_drop();
                Gmtoolkit::stop_output_tee();
                return 0;
            }
            Gmtoolkit::err("Already patched with a different config (found %s, expected %s). "
                           "Start from a fresh data file or pass --force.",
                           found.c_str(), expected_sentinel.c_str());
            Gmtoolkit::pause_if_drag_drop();
            Gmtoolkit::stop_output_tee();
            return 2;
        }
    }

    // Optional cross-check: the config's "game" string must match GEN8.FileName before we touch the file.
    MappedFile mf;
    if (mapped_file_open(data_win, &mf) == 0) {
        size_t gen8_off, gen8_size;
        if (Gmtoolkit::find_chunk(mf.data, mf.size, "GEN8", &gen8_off, &gen8_size) == 0 && gen8_size >= 8) {
            uint32_t name_ptr = Gmtoolkit::r_u32(mf.data + gen8_off + 4);
            std::string nm = Gmtoolkit::read_strg_at(mf.data, mf.size, name_ptr);
            if (!nm.empty()) {
                if (!opt.game.empty() && nm != opt.game) {
                    Gmtoolkit::err("config 'game' = %s does not match provided data: GEN8.FileName = %s",
                                   opt.game.c_str(), nm.c_str());
                    mapped_file_close(&mf);
                    Gmtoolkit::pause_if_drag_drop();
                    Gmtoolkit::stop_output_tee();
                    return 2;
                }
                Gmtoolkit::msg("Working on %s", nm.c_str());
                fflush(stdout);
            }
        }
        mapped_file_close(&mf);
    }

    if (opt.bytecode_target != 0) {
        if (set_bytecode_version(data_win, opt.bytecode_target) != 0)
            return 10;
    }

    if (!opt.shader_patches.empty()) {
        if (patch_shaders_in_file(data_win, opt.shader_patches) != 0)
            return 12;
    }

    if (opt.compress_audio) {
        if (compress_audio(data_win, opt.audio_min_size, opt.audio_bitrate, opt.audio_downmix, opt.audio_resample,
                           opt.audio_recompress, !opt.skip_audiogroups, opt.verbose, (unsigned)opt.threads) != 0)
            return 11;
    }

    if (want_textures) {
        if (opt.repack) {
            int r = run_repack(data_win, out_dir, blk, quality, opt.max_strip, (unsigned)opt.threads, opt.page_size,
                               opt.max_dims, opt.max_area);
            if (r != 0)
                return r;
        } else {
            Ops::ExtractOptions xopt;
            xopt.keep_pages = opt.keep_pages;
            xopt.keep_colors = opt.keep_colors;
            xopt.threads = opt.threads;
            xopt.max_strip = opt.max_strip;
            int r = Ops::extract_textures(data_win, out_dir, blk, quality, xopt);
            if (r != 0)
                return r;
        }
    }
    if (int r = run_code_patches(); r != 0)
        return r;

    if (opt.set_flags || opt.clear_flags) {
        if (toggle_flags_and_uid_in_file(data_win, opt.set_flags, opt.clear_flags) != 0)
            return 9;
    }

    if (Gmtoolkit::verify_output(data_win) != 0) {
        Gmtoolkit::err("verification failed! Output may not work as expected!");
        Gmtoolkit::pause_if_drag_drop();
        Gmtoolkit::stop_output_tee();
        return 14;
    }

    // Stamp the sentinel last so re-runs short-circuit on the pre-check above.
    if (GMSLib::SaveBackend::stamp_file(data_win, expected_sentinel) != 0) {
        Gmtoolkit::err("warning: failed to stamp sentinel; re-runs will repeat work");
    }

    auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t_start).count();
    size_t peak_ws_mb = peak_resident_bytes() / 1048576;
    size_t peak_priv_mb = peak_private_bytes() / 1048576;
    Gmtoolkit::msg("Patched in %.1fs, peak %zu MB (ws), peak %zu MB (priv)", elapsed_ms / 1000.0, peak_ws_mb,
                   peak_priv_mb);

    Gmtoolkit::pause_if_drag_drop();
    Gmtoolkit::stop_output_tee();
    return 0;
}

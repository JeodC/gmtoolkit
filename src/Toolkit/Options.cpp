// SPDX-License-Identifier: MIT

#include "Toolkit/Options.h"

#include "Toolkit/PatchShaders.h"
#include "Toolkit/Log.h"

#include <cstdint>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <vector>

extern uint32_t parse_flag_list(const char* s);

namespace {

// Single-pass tokenless JSON reader; line tracking is for error messages only.
struct Json {
    const char* p;
    const char* end;
    const char* path;
    int line;

    void skip_ws() {
        while (p < end) {
            char c = *p;
            if (c == ' ' || c == '\t' || c == '\r') {
                p++;
            } else if (c == '\n') {
                line++;
                p++;
            } else
                break;
        }
    }

    bool eat(char c) {
        skip_ws();
        if (p < end && *p == c) {
            p++;
            return true;
        }
        return false;
    }

    int fail(const char* what) {
        Gmtoolkit::tprint("%s:%d: JSON parse error: %s (near `%.20s')\n", path, line, what, p < end ? p : "<eof>");
        return -1;
    }

    int read_string(std::string& out) {
        out.clear();
        while (p < end) {
            char c = *p++;
            if (c == '"')
                return 0;
            if (c == '\n')
                line++;
            if (c == '\\' && p < end) {
                char esc = *p++;
                switch (esc) {
                    case '"':
                        out += '"';
                        break;
                    case '\\':
                        out += '\\';
                        break;
                    case '/':
                        out += '/';
                        break;
                    case 'b':
                        out += '\b';
                        break;
                    case 'f':
                        out += '\f';
                        break;
                    case 'n':
                        out += '\n';
                        break;
                    case 'r':
                        out += '\r';
                        break;
                    case 't':
                        out += '\t';
                        break;
                    case 'u': {
                        if (end - p < 4)
                            return fail("truncated \\u escape");
                        unsigned cp = 0;
                        for (int i = 0; i < 4; i++) {
                            char h = *p++;
                            cp <<= 4;
                            if (h >= '0' && h <= '9')
                                cp |= (h - '0');
                            else if (h >= 'a' && h <= 'f')
                                cp |= (h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F')
                                cp |= (h - 'A' + 10);
                            else
                                return fail("bad \\u hex");
                        }
                        if (cp < 0x80)
                            out += (char)cp;
                        else if (cp < 0x800) {
                            out += (char)(0xC0 | (cp >> 6));
                            out += (char)(0x80 | (cp & 0x3F));
                        } else {
                            out += (char)(0xE0 | (cp >> 12));
                            out += (char)(0x80 | ((cp >> 6) & 0x3F));
                            out += (char)(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default:
                        return fail("bad string escape");
                }
            } else {
                out += c;
            }
        }
        return fail("unterminated string");
    }

    int read_value();

    int read_int(long long* out) {
        skip_ws();
        char* e = NULL;
        const char* start = p;
        long long v = strtoll(p, &e, 10);
        if (e == start)
            return fail("expected integer");
        p = e;
        *out = v;
        return 0;
    }

    int read_bool(bool* out) {
        skip_ws();
        if (end - p >= 4 && !memcmp(p, "true", 4)) {
            p += 4;
            *out = true;
            return 0;
        }
        if (end - p >= 5 && !memcmp(p, "false", 5)) {
            p += 5;
            *out = false;
            return 0;
        }
        return fail("expected bool");
    }

    int read_quoted(std::string& out) {
        if (!eat('"'))
            return fail("expected string");
        return read_string(out);
    }

    int skip_value() {
        skip_ws();
        if (p >= end)
            return fail("eof in value");
        char c = *p;
        if (c == '"') {
            p++;
            std::string s;
            return read_string(s);
        }
        if (c == '{') {
            p++;
            if (eat('}'))
                return 0;
            for (;;) {
                if (!eat('"'))
                    return fail("expected \" in object key");
                std::string key;
                if (read_string(key) != 0)
                    return -1;
                if (!eat(':'))
                    return fail("expected ':'");
                if (skip_value() != 0)
                    return -1;
                if (eat(','))
                    continue;
                if (eat('}'))
                    return 0;
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            p++;
            if (eat(']'))
                return 0;
            for (;;) {
                if (skip_value() != 0)
                    return -1;
                if (eat(','))
                    continue;
                if (eat(']'))
                    return 0;
                return fail("expected ',' or ']'");
            }
        }
        if (c == 't' && end - p >= 4 && !memcmp(p, "true", 4)) {
            p += 4;
            return 0;
        }
        if (c == 'f' && end - p >= 5 && !memcmp(p, "false", 5)) {
            p += 5;
            return 0;
        }
        if (c == 'n' && end - p >= 4 && !memcmp(p, "null", 4)) {
            p += 4;
            return 0;
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            char* e2 = NULL;
            strtod(p, &e2);
            if (e2 == p)
                return fail("bad number");
            p = e2;
            return 0;
        }
        return fail("unexpected character");
    }
};

} // namespace

static int parse_int_array(Json& j, std::vector<unsigned int>& out) {
    if (!j.eat('['))
        return j.fail("expected '['");
    if (j.eat(']'))
        return 0;
    for (;;) {
        long long v;
        if (j.read_int(&v) != 0)
            return -1;
        if (v >= 0)
            out.push_back((unsigned int)v);
        if (j.eat(','))
            continue;
        if (j.eat(']'))
            return 0;
        return j.fail("expected ',' or ']'");
    }
}

static int parse_string_array(Json& j, std::vector<std::string>& out) {
    if (!j.eat('['))
        return j.fail("expected '['");
    if (j.eat(']'))
        return 0;
    for (;;) {
        std::string s;
        if (j.read_quoted(s) != 0)
            return -1;
        out.push_back(s);
        if (j.eat(','))
            continue;
        if (j.eat(']'))
            return 0;
        return j.fail("expected ',' or ']'");
    }
}

static int parse_compress_audio(Json& j, Options& opt) {
    j.skip_ws();
    if (j.p < j.end && (*j.p == 't' || *j.p == 'f')) {
        return j.read_bool(&opt.compress_audio);
    }
    if (!j.eat('{'))
        return j.fail("expected bool or '{' for compress_audio");
    opt.compress_audio = true;
    if (j.eat('}'))
        return 0;
    for (;;) {
        std::string k;
        if (j.read_quoted(k) != 0)
            return -1;
        if (!j.eat(':'))
            return j.fail("expected ':'");
        if (k == "min_size") {
            j.skip_ws();
            if (j.p < j.end && *j.p == '"') {
                std::string s;
                if (j.read_quoted(s) != 0)
                    return -1;
                if (s != "auto")
                    return j.fail("min_size string must be \"auto\"");
                opt.audio_min_size = SIZE_MAX;
            } else {
                long long v;
                if (j.read_int(&v) != 0)
                    return -1;
                if (v >= 0)
                    opt.audio_min_size = (size_t)v;
            }
        } else if (k == "bitrate") {
            long long v;
            if (j.read_int(&v) != 0)
                return -1;
            opt.audio_bitrate = (int)v;
        } else if (k == "downmix") {
            if (j.read_bool(&opt.audio_downmix) != 0)
                return -1;
        } else if (k == "resample") {
            long long v;
            if (j.read_int(&v) != 0)
                return -1;
            opt.audio_resample = (int)v;
        } else if (k == "recompress_ogg") {
            if (j.read_bool(&opt.audio_recompress) != 0)
                return -1;
        } else {
            if (j.skip_value() != 0)
                return -1;
        }
        if (j.eat(','))
            continue;
        if (j.eat('}'))
            return 0;
        return j.fail("expected ',' or '}' in compress_audio");
    }
}

int load_config_json(const char* path, Options& opt) {
    const char* last = NULL;
    for (const char* p = path; *p; p++)
        if (*p == '/' || *p == '\\')
            last = p;
    if (last)
        opt.config_dir = std::string(path, (size_t)(last - path));
    else
        opt.config_dir = ".";

    FILE* f = fopen(path, "rb");
    if (!f) {
        Gmtoolkit::tprint("--config: cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz < 0) {
        fclose(f);
        return -1;
    }
    std::string buf((size_t)sz, '\0');
    if (sz > 0 && fread(&buf[0], 1, (size_t)sz, f) != (size_t)sz) {
        Gmtoolkit::tprint("--config: short read on %s\n", path);
        fclose(f);
        return -1;
    }
    fclose(f);

    Json j;
    j.p = buf.data();
    j.end = buf.data() + buf.size();
    j.path = path;
    j.line = 1;

    if (!j.eat('{'))
        return j.fail("expected object at top level");
    if (j.eat('}'))
        return 0;

    for (;;) {
        std::string key;
        if (j.read_quoted(key) != 0)
            return -1;
        if (!j.eat(':'))
            return j.fail("expected ':'");

        if (key == "target_bytecode") {
            long long v;
            if (j.read_int(&v) != 0)
                return -1;
            opt.bytecode_target = (int)v;
        } else if (key == "game") {
            if (j.read_quoted(opt.game) != 0)
                return -1;
        } else if (key == "externalize_textures") {
            if (j.read_quoted(opt.externalize_dir) != 0)
                return -1;
        } else if (key == "repack") {
            if (j.read_bool(&opt.repack) != 0)
                return -1;
        } else if (key == "block") {
            if (j.read_quoted(opt.block_name) != 0)
                return -1;
        } else if (key == "quality") {
            if (j.read_quoted(opt.quality) != 0)
                return -1;
        } else if (key == "threads") {
            long long v;
            if (j.read_int(&v) != 0)
                return -1;
            opt.threads = (long)v;
        } else if (key == "page_size") {
            long long v;
            if (j.read_int(&v) != 0)
                return -1;
            opt.page_size = (int)v;
        } else if (key == "max_dims") {
            long long v;
            if (j.read_int(&v) != 0)
                return -1;
            opt.max_dims = (int)v;
        } else if (key == "max_area") {
            long long v;
            if (j.read_int(&v) != 0)
                return -1;
            opt.max_area = (int)v;
        } else if (key == "set_flags" || key == "clear_flags") {
            std::vector<std::string> names;
            if (parse_string_array(j, names) != 0)
                return -1;
            std::string joined;
            for (size_t i = 0; i < names.size(); i++) {
                if (i)
                    joined += ',';
                joined += names[i];
            }
            uint32_t mask = parse_flag_list(joined.c_str());
            if (mask == UINT32_MAX)
                return -1;
            if (key == "set_flags")
                opt.set_flags = mask;
            else
                opt.clear_flags = mask;
        } else if (key == "compress_audio") {
            if (parse_compress_audio(j, opt) != 0)
                return -1;
        } else if (key == "keep_inline_pages") {
            if (parse_int_array(j, opt.keep_pages) != 0)
                return -1;
        } else if (key == "keep_inline_colors") {
            std::vector<std::string> cols;
            if (parse_string_array(j, cols) != 0)
                return -1;
            for (const std::string& s : cols) {
                if (s.size() != 6) {
                    Gmtoolkit::err("%s: keep_inline_colors entry `%s' is not 6 hex chars", j.path, s.c_str());
                    return -1;
                }
                uint32_t c = (uint32_t)strtoul(s.c_str(), NULL, 16) & 0xFFFFFFu;
                opt.keep_colors.push_back(c);
            }
        } else if (key == "code_patches") {
            if (!j.eat('['))
                return j.fail("expected '[' for code_patches");
            if (!j.eat(']')) {
                for (;;) {
                    if (!j.eat('{'))
                        return j.fail("expected '{' for code patch");
                    Options::CodePatchSpec cp;
                    if (!j.eat('}')) {
                        for (;;) {
                            std::string k;
                            if (j.read_quoted(k) != 0)
                                return -1;
                            if (!j.eat(':'))
                                return j.fail("expected ':'");
                            std::string v;
                            if (j.read_quoted(v) != 0)
                                return -1;
                            if (k == "entry")
                                cp.entry_name = v;
                            else if (k == "gml")
                                cp.gml_path = v;
                            if (j.eat(','))
                                continue;
                            if (j.eat('}'))
                                break;
                            return j.fail("expected ',' or '}' in patch");
                        }
                    }
                    if (cp.entry_name.empty() || cp.gml_path.empty()) {
                        Gmtoolkit::err("%s: code patch missing entry/gml", j.path);
                        return -1;
                    }
                    opt.code_patches.push_back(cp);
                    if (j.eat(','))
                        continue;
                    if (j.eat(']'))
                        break;
                    return j.fail("expected ',' or ']' in code_patches");
                }
            }
        } else if (key == "shader_patches") {
            if (!j.eat('['))
                return j.fail("expected '[' for shader_patches");
            if (!j.eat(']')) {
                for (;;) {
                    if (!j.eat('{'))
                        return j.fail("expected '{' for shader patch");
                    ShaderPatch pp;
                    if (!j.eat('}')) {
                        for (;;) {
                            std::string pk;
                            if (j.read_quoted(pk) != 0)
                                return -1;
                            if (!j.eat(':'))
                                return j.fail("expected ':'");
                            std::string pv;
                            if (j.read_quoted(pv) != 0)
                                return -1;
                            if (pk == "shader")
                                pp.shader = pv;
                            else if (pk == "stage")
                                pp.stage = pv;
                            else if (pk == "find")
                                pp.find = pv;
                            else if (pk == "replace")
                                pp.replace = pv;
                            if (j.eat(','))
                                continue;
                            if (j.eat('}'))
                                break;
                            return j.fail("expected ',' or '}' in patch");
                        }
                    }
                    if (pp.shader.empty() || pp.stage.empty() || pp.find.empty()) {
                        Gmtoolkit::err("%s:%d: shader patch missing shader/stage/find", j.path, j.line);
                        return -1;
                    }
                    opt.shader_patches.push_back(pp);
                    if (j.eat(','))
                        continue;
                    if (j.eat(']'))
                        break;
                    return j.fail("expected ',' or ']' in shader_patches");
                }
            }
        } else {
            if (j.skip_value() != 0)
                return -1;
        }

        if (j.eat(','))
            continue;
        if (j.eat('}'))
            break;
        return j.fail("expected ',' or '}' at top level");
    }
    return 0;
}

# GMToolkit

A native C++ replacement for the .NET + Python stack that GameMaker ports historically called out to during [linux arm64 port](https://github.com/JeodC/RHH-Ports/wiki/GameMaker-Studio-Ports) installation. One static binary on the device replaces UndertaleModTool, DotNet, and the GMTools audio compression kit. Instead of loading a massive datafile into heap memory where each chunk is an UndertaleObject, GMToolkit operates directly on the chunk in-place.

## What it does

* **`--info`** — open a datafile, run version detection, print what was found. GEN8-declared vs heuristically-detected GMS version, bytecode version, YYC status, derived feature flags (`using_self_to_builtin`, `using_extended_sound_info`, etc).

* **Texture externalize** — walk the TXTR chunk, decode each blob (PNG / `2zoq` BZ2+YYG-QOIF / bare `fioq` YYG-QOIF), ASTC-compress to `DIR/<idx>.pvr`, replace the inline blob with a small stub that gmloader-next's texhack maps back to the external PVR at runtime. Optional `--repack` bin-packs TPIs into smaller atlases first. Honors `--keep-pages` / `--keep-color` for palette-key textures that must stay inline.

* **Audio compression** — SOND-driven walk: qualifying entries get WAV->OGG encoded (or OGG->OGG recompressed) in-process via libvorbis, SOND flags are flipped to `IsCompressed` in lockstep so the runtime never sees a flag/payload mismatch.

* **Flag toggle** — flip `GEN8.InfoFlags` bits and recompute the `GMS2RandomUID` checksum so the runtime (and UTMT itself) still accepts the file.

* **Shader patches** — locate a SHDR entry's named stage source by name, substring-replace in place (replacement must be ≤ original size; padded with whitespace). Used for things like `precision mediump float;` → `precision highp float;` on Mali drivers.

* **CODE patching** — replace a CODE entry's GML body. Parses GML, emits bytecode, rewrites VARI/FUNC occurrence chains, grows STRG/VARI/FUNC/SCPT pools, slides every chunk after the patch site and fixes up downstream absolute file offsets.

* **`--bytecode-version N`** — retarget `GEN8.BytecodeVersion` (e.g. 15 -> 16 for older runtimes that need it).

## Using a configuration file

A json file can be passed to GMToolkit to outline the jobs it will perform. Every key is optional; unknown keys are ignored. The example below uses every recognised key with a real value so it doubles as a reference of the supported schema.

```json
{
  "game":                 "data.win",
  "target_bytecode":      16,
  "externalize_textures": "textures",
  "block":                "4x4",
  "quality":              "medium",
  "threads":              0,
  "repack":               true,
  "page_size":            1024,
  "max_dims":             0,
  "max_area":             0,
  "set_flags":            ["Fullscreen", "Interpolate"],
  "clear_flags":          ["Scale"],
  "compress_audio": {
    "min_size":       1000,
    "bitrate":        64,
    "downmix":        false,
    "resample":       0,
    "recompress_ogg": true
  },
  "keep_inline_pages":  [4, 5, 6],
  "keep_inline_colors": ["FFFF40", "00FFFF"],
  "shader_patches": [
    { "shader":  "shd_pal_swapper",
      "stage":   "glsl_es_fragment",
      "find":    "precision mediump float;",
      "replace": "precision highp   float;" }
  ],
  "code_patches": [
    { "entry": "gml_Object_oPauseMenu_Other_14",
      "gml":   "gml/gml_Object_oPauseMenu_Other_14.gml" }
  ]
}
```

Keys and their meaning:

| Key                                   | Type             | Effect                                                                                                                       |
| ------------------------------------- | ---------------- | ---------------------------------------------------------------------------------------------------------------------------- |
| `game`                                | string           | Sanity check: must match `GEN8.FileName` on disk, otherwise the run aborts. Useful as a guardrail against pointing the config at the wrong file. |
| `target_bytecode`                     | int              | Retarget `GEN8.BytecodeVersion` (e.g. `15` -> `16`). `0` leaves it alone.                                                    |
| `externalize_textures`                | string (dir)     | Run the texture-externalize transform; PVR files land in this directory.                                                     |
| `block`                               | string           | ASTC block size: `4x4`, `5x5`, `6x6`.                                                                                        |
| `quality`                             | string           | astcenc preset: `fastest`, `fast`, `medium`, `thorough`.                                                                     |
| `threads`                             | int              | astcenc thread count. `0` = `nproc`.                                                                                         |
| `repack`                              | bool             | Bin-pack TPIs into smaller atlases. Runs before externalizing.                                                               |
| `page_size`                           | int              | Atlas dimension when `repack` is on.                                                                                         |
| `max_dims`                            | int              | TPIs wider/taller than this skip atlas packing and get their own page. `0` defaults to `page_size`.                          |
| `max_area`                            | int              | TPIs with `w * h` past this also get their own page. `0` defaults to `page_size ^ 2`.                                        |
| `set_flags`                           | array of strings | `GEN8.InfoFlags` bits to set; recomputes `GMS2RandomUID`.                                                                    |
| `clear_flags`                         | array of strings | `GEN8.InfoFlags` bits to clear; recomputes `GMS2RandomUID`.                                                                  |
| `compress_audio`                      | object or bool   | Audio compression. `true` enables with defaults; an object supplies the per-field overrides below.                           |
| `compress_audio.min_size`             | int              | Minimum entry size in bytes to compress; smaller entries are skipped.                                                        |
| `compress_audio.bitrate`              | int              | OGG target bitrate (kbps). `0` lets libvorbis pick a default.                                                                |
| `compress_audio.downmix`              | bool             | Stereo to mono before encoding.                                                                                              |
| `compress_audio.resample`             | int              | Resample to this Hz before encoding. `0` keeps the source rate.                                                              |
| `compress_audio.recompress_ogg`       | bool             | Decode + re-encode entries that are already OGG.                                                                             |
| `keep_inline_pages`                   | array of int     | Texture page indices that stay inline in TXTR (no PVR extraction).                                                           |
| `keep_inline_colors`                  | array of hex     | Texture pages containing any of these `RRGGBB` colors stay inline (palette-key textures).                                    |
| `shader_patches`                      | array of object  | SHDR text-source substring replacements.                                                                                     |
| `shader_patches[].shader`             | string           | SHDR entry name to target (e.g. `shd_pal_swapper`).                                                                          |
| `shader_patches[].stage`              | string           | Which compiled stage of the shader to patch; see accepted names below.                                                       |
| `shader_patches[].find`               | string           | Exact substring to locate in the stage's source.                                                                             |
| `shader_patches[].replace`            | string           | Replacement. Must be ≤ `find` in size; the difference is space-padded.                                                       |
| `code_patches`                        | array of object  | CODE entry replacements.                                                                                                     |
| `code_patches[].entry`                | string           | CODE entry name to overwrite must be an exact match.                                                                         |
| `code_patches[].gml`                  | string (path)    | GML source file. Resolved relative to the config file's directory.                                                           |

Flag names accepted under `set_flags` / `clear_flags`: `Fullscreen`, `SyncVertex1`, `SyncVertex2`, `Interpolate`, `Scale`, `ShowCursor`, `Sizeable`, `ScreenKey`, `SyncVertex3`, `BorderlessWindow`.

Shader stage names accepted: `glsl_es_vertex`, `glsl_es_fragment`, `glsl_vertex`, `glsl_fragment`, `hlsl9_vertex`, `hlsl9_fragment`.

## Usage

### On a handheld

GameMaker ports authored against gmloader-next call gmtoolkit from their patchscript automatically. Pharos pulls the matching binary from this repo's Releases the first time a GameMaker port is installed and drops it at `$controlfolder/gmtoolkit.${DEVICE_ARCH}`. From there the per-port patchscript invokes it with the port's JSON config; nothing to configure by hand.

### Offline on a desktop

The desktop builds (Windows `.exe`, Linux x86_64) are attached to each release for users who want to patch a `data.win` without going through the device:

```bash
# Windows (PowerShell or cmd)
gmtoolkit.exe --config myport.json data.win

# Linux / macOS
./gmtoolkit --config myport.json data.win

# Inspect a datafile without modifying it
./gmtoolkit --info data.win
```

On Windows the `.exe` also supports drag-and-drop: drop a `data.win` onto the binary and a sibling `gmtoolkit.json` (if present) gets used as the config; output is teed to `gmtoolkit.log` next to the binary.

The JSON schema is the same across all platforms — every port that ships a config under `tools/<port>.json` can be patched locally first and then dropped onto the device, which is useful for working offline or running heavy ASTC / vorbis passes on a faster machine.

## Build + install

See [BUILDING.md](BUILDING.md).

## License

gmtoolkit is mixed-licensed. The linked binary is GPL-3.0-or-later because the `GMSLib/` tree is a port of UTMT GPL-3 code. Most other source files are MIT or MPL-2.0. Full breakdown in [LICENSE.md](LICENSE.md). Upstream attributions in [NOTICE](NOTICE).

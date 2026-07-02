# Building gmtoolkit

gmtoolkit is CMake-driven. One source tree builds for the target device (aarch64 static binary, GLIBC ~2.27 floor) and for PC use (Windows x64 .exe with the MSVC static runtime, or a host-native build on Linux/macOS). Most dependencies — astc-encoder, bzip2, libogg, libvorbis, stb_image — are fetched at configure time. FFmpeg (for `--transcode-video`) is provided by [vcpkg](https://vcpkg.io) via the `vcpkg.json` manifest, so builds pass the vcpkg toolchain file. To build without video support (skips vcpkg/FFmpeg entirely), add `-DGMTOOLKIT_ENABLE_TRANSCODE=OFF`.

## Windows x64 (MSVC)

From a Visual Studio Developer Command Prompt (any modern VS with the C++ workload + Ninja):

```bat
cmake -S . -B build-msvc -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build-msvc
```

Produces `build-msvc/gmtoolkit.exe`. The MSVC C/C++ runtime is statically linked (`/MT`), so the binary has no VC++ redistributable dependency.

## Linux / macOS (host-native)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build
```

FFmpeg's build needs `nasm` and `pkg-config` on `PATH`.

## aarch64 (cross-compile in Docker)

vcpkg cross-builds FFmpeg via the `vcpkg-triplets/arm64-linux` overlay triplet, which chainloads the cross toolchain:

```bash
docker run --rm -v "$PWD:/work" -w /work debian:bullseye bash -c '
  apt-get update -qq && apt-get install -y -qq --no-install-recommends \
    g++-aarch64-linux-gnu build-essential cmake ninja-build git ca-certificates \
    curl zip unzip tar pkg-config python3
  git clone --depth 1 https://github.com/microsoft/vcpkg /opt/vcpkg
  /opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics
  cmake -S . -B build-aarch64 -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
        -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$PWD/cmake/aarch64-linux-gnu.cmake" \
        -DVCPKG_OVERLAY_TRIPLETS=vcpkg-triplets \
        -DVCPKG_TARGET_TRIPLET=arm64-linux
  cmake --build build-aarch64
  aarch64-linux-gnu-strip build-aarch64/gmtoolkit
'
```

Produces `build-aarch64/gmtoolkit`. `libstdc++` and `libgcc` are statically linked; glibc is left dynamic.

## Distribution

The aarch64 binary is published as a release asset on this repository. Port frameworks pull it down on first GameMaker-port install and drop it at `$controlfolder/gmtoolkit.aarch64`. Port patchscripts invoke `"$controlfolder/gmtoolkit.${DEVICE_ARCH}"`.

## Dependencies (fetched at configure)

| Package          | Version | License            |
| ---------------- | ------- | ------------------ |
| astc-encoder     | 4.8.0   | Apache-2.0         |
| bzip2            | 1.0.8   | bzip2 (BSD-like)   |
| libogg           | 1.3.5   | BSD-3-Clause       |
| libvorbis        | 1.3.7   | BSD-3-Clause       |
| stb_image{,_write}.h | pinned | MIT / public domain |
| FFmpeg (via vcpkg)   | vcpkg   | LGPL-2.1-or-later  |

FFmpeg is fetched/built by vcpkg (`vcpkg.json`), not FetchContent, and only when `GMTOOLKIT_ENABLE_TRANSCODE` is on (the default).

See [LICENSE.md](LICENSE.md) for the per-file license breakdown.

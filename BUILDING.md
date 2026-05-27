# Building gmtoolkit

gmtoolkit is CMake-driven. One source tree builds for the target device (aarch64 static binary, GLIBC ~2.27 floor) and for PC use (Windows x64 .exe with the MSVC static runtime, or a host-native build on Linux/macOS). All dependencies — astc-encoder, bzip2, libogg, libvorbis, stb_image — are fetched at configure time. There is no vendored source in the repo.

## Windows x64 (MSVC)

From a Visual Studio Developer Command Prompt (any modern VS with the C++ workload + Ninja):

```bat
cmake -S . -B build-msvc -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-msvc
```

Produces `build-msvc/gmtoolkit.exe`. The MSVC C/C++ runtime is statically linked (`/MT`), so the binary has no VC++ redistributable dependency.

## Linux / macOS (host-native)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## aarch64 (cross-compile in Docker)

```bash
docker run --rm -v "$PWD:/work" -w /work debian:bullseye bash -c '
  apt-get update -qq && apt-get install -y -qq --no-install-recommends \
    g++-aarch64-linux-gnu cmake ninja-build git ca-certificates
  cmake -S . -B build-aarch64 -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake \
        -DCMAKE_BUILD_TYPE=Release
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

See [LICENSE.md](LICENSE.md) for the per-file license breakdown.

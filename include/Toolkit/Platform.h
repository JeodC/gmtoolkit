// SPDX-License-Identifier: MIT

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <stdio.h>

#if defined(_WIN32)
#include <io.h>
#include <direct.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

#include <filesystem>
#include <system_error>

static inline int portable_truncate(FILE* f, long long size) {
#if defined(_WIN32)
    return _chsize_s(_fileno(f), size);
#else
    return ftruncate(fileno(f), (off_t)size);
#endif
}

static inline int portable_mkdir(const char* path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return ec ? -1 : 0;
}

static inline int portable_rename(const char* from, const char* to) {
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    return ec ? -1 : 0;
}

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#else
#include <sys/mman.h>
#include <sys/resource.h>
#include <fcntl.h>
#endif

struct MappedFile {
    uint8_t* data = nullptr;
    size_t size = 0;
#if defined(_WIN32)
    HANDLE file_handle = nullptr;
    HANDLE mapping_handle = nullptr;
#else
    int fd = -1;
#endif
};

enum MappedFileMode {
    MMAP_RO,
    MMAP_COW,
    MMAP_RW,
    MMAP_CREATE,
};

// Unified open/create driver behind the named wrappers below. Each mode selects an access/share/prot
// tuple on Win32 and the equivalent O_/PROT_/MAP_ flags on POSIX.
static inline int mapped_file_impl(const char* path, MappedFile* m, MappedFileMode mode, size_t create_size) {
    m->data = nullptr;
    m->size = 0;
#if defined(_WIN32)
    DWORD access = (mode == MMAP_RO) ? GENERIC_READ : (GENERIC_READ | GENERIC_WRITE);
    DWORD share = (mode == MMAP_CREATE) ? FILE_SHARE_READ : (FILE_SHARE_READ | FILE_SHARE_WRITE);
    DWORD disp = (mode == MMAP_CREATE) ? CREATE_ALWAYS : OPEN_EXISTING;
    DWORD prot, view;
    switch (mode) {
        case MMAP_RO:
            prot = PAGE_READONLY;
            view = FILE_MAP_READ;
            break;
        case MMAP_COW:
            prot = PAGE_WRITECOPY;
            view = FILE_MAP_COPY;
            break;
        case MMAP_RW:
        case MMAP_CREATE:
            prot = PAGE_READWRITE;
            view = FILE_MAP_WRITE;
            break;
    }
    m->file_handle = CreateFileA(path, access, share, nullptr, disp, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (m->file_handle == INVALID_HANDLE_VALUE)
        return -1;
    size_t sz;
    if (mode == MMAP_CREATE) {
        if (create_size == 0) {
            CloseHandle(m->file_handle);
            return -1;
        }
        LARGE_INTEGER eof;
        eof.QuadPart = (LONGLONG)create_size;
        if (!SetFilePointerEx(m->file_handle, eof, nullptr, FILE_BEGIN) || !SetEndOfFile(m->file_handle)) {
            CloseHandle(m->file_handle);
            return -1;
        }
        sz = create_size;
    } else {
        LARGE_INTEGER fs;
        if (!GetFileSizeEx(m->file_handle, &fs) || fs.QuadPart == 0) {
            CloseHandle(m->file_handle);
            return -1;
        }
        sz = (size_t)fs.QuadPart;
    }
    m->mapping_handle = CreateFileMappingA(m->file_handle, nullptr, prot, 0, 0, nullptr);
    if (!m->mapping_handle) {
        CloseHandle(m->file_handle);
        return -1;
    }
    m->data = (uint8_t*)MapViewOfFile(m->mapping_handle, view, 0, 0, 0);
    if (!m->data) {
        CloseHandle(m->mapping_handle);
        CloseHandle(m->file_handle);
        return -1;
    }
    m->size = sz;
#else
    int oflags = (mode == MMAP_RO) ? O_RDONLY : O_RDWR;
    if (mode == MMAP_CREATE)
        oflags |= O_CREAT | O_TRUNC;
    m->fd = open(path, oflags, 0644);
    if (m->fd < 0)
        return -1;
    size_t sz;
    if (mode == MMAP_CREATE) {
        if (create_size == 0 || ftruncate(m->fd, (off_t)create_size) != 0) {
            close(m->fd);
            return -1;
        }
        sz = create_size;
    } else {
        off_t fs = lseek(m->fd, 0, SEEK_END);
        if (fs <= 0) {
            close(m->fd);
            return -1;
        }
        sz = (size_t)fs;
    }
    int prot = (mode == MMAP_RO) ? PROT_READ : (PROT_READ | PROT_WRITE);
    int flags = (mode == MMAP_RW || mode == MMAP_CREATE) ? MAP_SHARED : MAP_PRIVATE;
    m->data = (uint8_t*)mmap(nullptr, sz, prot, flags, m->fd, 0);
    if (m->data == MAP_FAILED) {
        close(m->fd);
        m->data = nullptr;
        return -1;
    }
    m->size = sz;
#endif
    return 0;
}

static inline int mapped_file_open(const char* p, MappedFile* m) {
    return mapped_file_impl(p, m, MMAP_RO, 0);
}
static inline int mapped_file_open_cow(const char* p, MappedFile* m) {
    return mapped_file_impl(p, m, MMAP_COW, 0);
}
static inline int mapped_file_open_rw(const char* p, MappedFile* m) {
    return mapped_file_impl(p, m, MMAP_RW, 0);
}
static inline int mapped_file_create_rw(const char* p, size_t sz, MappedFile* m) {
    return mapped_file_impl(p, m, MMAP_CREATE, sz);
}

static inline int mapped_file_flush(MappedFile* m) {
    if (!m->data)
        return 0;
#if defined(_WIN32)
    return FlushViewOfFile(m->data, 0) ? 0 : -1;
#else
    return msync(m->data, m->size, MS_SYNC);
#endif
}

static inline void mapped_file_close(MappedFile* m) {
    if (!m->data)
        return;
#if defined(_WIN32)
    UnmapViewOfFile(m->data);
    CloseHandle(m->mapping_handle);
    CloseHandle(m->file_handle);
#else
    munmap(m->data, m->size);
    close(m->fd);
#endif
    m->data = nullptr;
    m->size = 0;
}

static inline std::string get_exe_dir() {
#if defined(_WIN32)
    wchar_t wbuf[1024];
    DWORD n = GetModuleFileNameW(nullptr, wbuf, 1024);
    if (n == 0 || n >= 1024)
        return std::string(".");
    std::filesystem::path p(wbuf, wbuf + n);
    return p.parent_path().string();
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t sz = sizeof(buf);
    extern int _NSGetExecutablePath(char* buf, uint32_t* bufsize);
    if (_NSGetExecutablePath(buf, &sz) != 0)
        return std::string(".");
    std::error_code ec;
    auto p = std::filesystem::canonical(buf, ec);
    if (ec)
        return std::filesystem::path(buf).parent_path().string();
    return p.parent_path().string();
#else
    std::error_code ec;
    auto p = std::filesystem::canonical("/proc/self/exe", ec);
    if (ec)
        return std::string(".");
    return p.parent_path().string();
#endif
}

static inline size_t peak_resident_bytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return (size_t)pmc.PeakWorkingSetSize;
    return 0;
#else
    struct rusage ru {};
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
#if defined(__APPLE__)
        return (size_t)ru.ru_maxrss;
#else
        return (size_t)ru.ru_maxrss * 1024;
#endif
    }
    return 0;
#endif
}

// Pagefile/commit usage on Win32, VmData+VmStk+VmExe on Linux. Counts pages we own, not mmap shares.
static inline size_t peak_private_bytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return (size_t)pmc.PeakPagefileUsage;
    return 0;
#else

    FILE* f = std::fopen("/proc/self/status", "r");
    if (!f)
        return 0;
    char line[256];
    size_t vm_data_kb = 0, vm_stk_kb = 0, vm_exe_kb = 0;
    while (std::fgets(line, sizeof(line), f)) {
        unsigned long kb = 0;
        if (std::sscanf(line, "VmData: %lu kB", &kb) == 1)
            vm_data_kb = kb;
        else if (std::sscanf(line, "VmStk: %lu kB", &kb) == 1)
            vm_stk_kb = kb;
        else if (std::sscanf(line, "VmExe: %lu kB", &kb) == 1)
            vm_exe_kb = kb;
    }
    std::fclose(f);
    return (vm_data_kb + vm_stk_kb + vm_exe_kb) * 1024;
#endif
}

// SPDX-License-Identifier: MIT

#pragma once

#include "Toolkit/Platform.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace Gmtoolkit {

// Two-mode byte container: starts as a copy-on-write mmap view, materialises into a heap vector
// the moment any write or size change happens. Lets callers pay full RAM only when they actually mutate.
class Buffer {
  public:
    Buffer() = default;

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    Buffer(Buffer&& o) noexcept {
        steal_from(o);
    }
    Buffer& operator=(Buffer&& o) noexcept {
        if (this != &o) {
            release();
            steal_from(o);
        }
        return *this;
    }

    Buffer(std::vector<uint8_t>&& v) noexcept : vec_(std::move(v)), mode_(VECTOR) {
    }
    Buffer& operator=(std::vector<uint8_t>&& v) noexcept {
        release();
        vec_ = std::move(v);
        mode_ = VECTOR;
        return *this;
    }

    ~Buffer() {
        release();
    }

    int load_cow(const char* path) {
        release();
        if (mapped_file_open_cow(path, &mmap_) != 0)
            return -1;
        mode_ = MMAP_COW;
        return 0;
    }

    // Copy mmap pages into a heap vector and release the mapping. Called automatically before
    // any resize/reserve since a mmap view has fixed length.
    void materialize() {
        if (mode_ != MMAP_COW)
            return;
        vec_.assign(mmap_.data, mmap_.data + mmap_.size);
        mapped_file_close(&mmap_);
        mode_ = VECTOR;
    }

    uint8_t* data() noexcept {
        return mode_ == VECTOR ? vec_.data() : mmap_.data;
    }
    const uint8_t* data() const noexcept {
        return mode_ == VECTOR ? vec_.data() : mmap_.data;
    }

    size_t size() const noexcept {
        return mode_ == VECTOR ? vec_.size() : mmap_.size;
    }

    bool empty() const noexcept {
        return size() == 0;
    }

    uint8_t& operator[](size_t i) noexcept {
        return data()[i];
    }
    const uint8_t& operator[](size_t i) const noexcept {
        return data()[i];
    }

    void resize(size_t n) {
        if (mode_ == MMAP_COW)
            materialize();
        vec_.resize(n);
    }

    void resize(size_t n, uint8_t fill) {
        if (mode_ == MMAP_COW)
            materialize();
        vec_.resize(n, fill);
    }

    void reserve(size_t n) {
        if (mode_ == MMAP_COW)
            materialize();
        vec_.reserve(n);
    }

    size_t capacity() const noexcept {
        return mode_ == VECTOR ? vec_.capacity() : mmap_.size;
    }

    void clear() {
        release();
    }

    const std::vector<uint8_t>& as_vector() {
        materialize();
        return vec_;
    }

    bool is_mmap() const noexcept {
        return mode_ == MMAP_COW;
    }

  private:
    enum Mode { VECTOR, MMAP_COW };
    std::vector<uint8_t> vec_;
    MappedFile mmap_{};
    Mode mode_ = VECTOR;

    void release() {
        if (mode_ == MMAP_COW)
            mapped_file_close(&mmap_);
        vec_.clear();
        vec_.shrink_to_fit();
        mode_ = VECTOR;
    }

    void steal_from(Buffer& o) {
        vec_ = std::move(o.vec_);
        mmap_ = o.mmap_;
        mode_ = o.mode_;
        o.mmap_.data = nullptr;
        o.mmap_.size = 0;
#if defined(_WIN32)
        o.mmap_.file_handle = nullptr;
        o.mmap_.mapping_handle = nullptr;
#else
        o.mmap_.fd = -1;
#endif
        o.mode_ = VECTOR;
    }
};

} // namespace Gmtoolkit

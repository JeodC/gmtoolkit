// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace Underanalyzer::Compiler {

// Bump allocator tuned for the AST: nodes are constructed in place inside slabs and
// freed wholesale when the arena dies. Skipping the dtor list for trivially-destructible
// types saves work for the bulk of token/node types that hold only PODs.
class NodeArena {
  public:
    NodeArena() = default;
    ~NodeArena();
    NodeArena(const NodeArena&) = delete;
    NodeArena& operator=(const NodeArena&) = delete;

    template <class T, class... Args> T* New(Args&&... args) {
        void* mem = AllocateRaw(sizeof(T), alignof(T));
        T* p = ::new (mem) T(std::forward<Args>(args)...);
        if constexpr (!std::is_trivially_destructible_v<T>) {
            _dtors.push_back({ p, +[](void* x) { static_cast<T*>(x)->~T(); } });
        }
        return p;
    }

  private:
    void* AllocateRaw(std::size_t size, std::size_t align);

    static constexpr std::size_t kSlabSize = 64 * 1024;
    struct Slab {
        std::unique_ptr<std::byte[]> data;
        std::size_t used = 0;
        std::size_t cap = 0;
    };
    std::vector<Slab> _slabs;
    std::vector<std::pair<void*, void (*)(void*)>> _dtors;
};

} // namespace Underanalyzer::Compiler

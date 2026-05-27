
// SPDX-License-Identifier: MIT
#include "Underanalyzer/Compiler/NodeArena.h"

#include <algorithm>

namespace Underanalyzer::Compiler {

// Run destructors in reverse construction order so child nodes are torn down before their parents.
NodeArena::~NodeArena() {

    for (auto it = _dtors.rbegin(); it != _dtors.rend(); ++it) {
        it->second(it->first);
    }
}

// Deviation: upstream relies on the GC; here a bump-allocator hands out aligned chunks
// from page-sized slabs and grows by appending a new slab whenever the active one is full.
void* NodeArena::AllocateRaw(std::size_t size, std::size_t align) {
    auto tryFit = [&](Slab& s) -> void* {
        std::uintptr_t base = reinterpret_cast<std::uintptr_t>(s.data.get()) + s.used;
        std::uintptr_t aligned = (base + align - 1) & ~(std::uintptr_t)(align - 1);
        std::size_t pad = aligned - base;
        if (s.used + pad + size > s.cap)
            return nullptr;
        s.used += pad + size;
        return reinterpret_cast<void*>(aligned);
    };

    if (!_slabs.empty()) {
        if (void* p = tryFit(_slabs.back()))
            return p;
    }

    // Oversized requests get a slab tailored to fit (kSlabSize is only the minimum).
    std::size_t cap = std::max(size + align, kSlabSize);
    Slab s;
    s.data = std::make_unique<std::byte[]>(cap);
    s.cap = cap;
    _slabs.push_back(std::move(s));
    void* p = tryFit(_slabs.back());
    return p;
}

} // namespace Underanalyzer::Compiler

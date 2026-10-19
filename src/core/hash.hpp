#ifndef BODYCAM_HASH_HPP
#define BODYCAM_HASH_HPP

#include <cstdint>
#include <cstddef>

namespace bodycam {

inline uint64_t fnv1a(const void* data, size_t len) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint64_t hash = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < len; ++i) {
        hash ^= bytes[i];
        hash *= 0x100000001b3ull;
    }
    return hash;
}

} // namespace bodycam
#endif

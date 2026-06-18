#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace ChronoSync {

    class FileHash {
    public:
        static bool Sha256File(const std::wstring& path, std::array<uint8_t, 32>& outHash);
        static bool HashesEqual(const std::array<uint8_t, 32>& a, const std::array<uint8_t, 32>& b);
        static std::wstring ToHex(const std::array<uint8_t, 32>& hash);
    };

} // namespace ChronoSync

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace PrevueSync {

    struct Sha256Progress {
        std::function<void(unsigned long long bytesHashed, unsigned long long fileSize)> onProgress;
    };

    class Sha256Session {
    public:
        Sha256Session();
        ~Sha256Session();

        Sha256Session(const Sha256Session&) = delete;
        Sha256Session& operator=(const Sha256Session&) = delete;

        bool IsValid() const { return valid_; }

        bool HashFile(const std::wstring& path,
                      std::array<uint8_t, 32>& outHash,
                      const Sha256Progress* progress = nullptr);

    private:
        bool valid_ = false;
        void* hAlg_ = nullptr;
        unsigned long hashObjectSize_ = 0;
        unsigned long hashLength_ = 0;
    };

    class FileHash {
    public:
        static bool Sha256File(const std::wstring& path, std::array<uint8_t, 32>& outHash,
                               const Sha256Progress* progress = nullptr);
        static bool HashesEqual(const std::array<uint8_t, 32>& a, const std::array<uint8_t, 32>& b);
        static std::wstring ToHex(const std::array<uint8_t, 32>& hash);
    };

} // namespace PrevueSync

#pragma once

#include "FileHash.h"
#include "SyncEngine.h"
#include <array>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace PrevueSync {

    struct CachedFileHash {
        unsigned long long fileSize = 0;
        unsigned long mtimeLow = 0;
        unsigned long mtimeHigh = 0;
        std::array<uint8_t, 32> hash{};
    };

    class SyncHashCache {
    public:
        static std::filesystem::path CachePath(const std::filesystem::path& destinationRoot);

        bool Load(const std::filesystem::path& destinationRoot, std::wstring& errorMessage);
        bool Save(const std::filesystem::path& destinationRoot, std::wstring& errorMessage);

        bool GetOrCompute(const std::wstring& relativePath,
                        const SyncItem& item,
                        const std::filesystem::path& fullPath,
                        Sha256Session& session,
                        std::array<uint8_t, 32>& outHash,
                        const Sha256Progress* progress);

        void Store(const std::wstring& relativePath, const SyncItem& item, const std::array<uint8_t, 32>& hash);

    private:
        std::unordered_map<std::wstring, CachedFileHash> entries_;
        bool dirty_ = false;

        static bool EntryMatches(const CachedFileHash& entry, const SyncItem& item);
    };

} // namespace PrevueSync

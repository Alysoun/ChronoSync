#pragma once

#include "SyncOptions.h"
#include <string>

namespace ChronoSync {

    struct SyncProfile {
        std::wstring name;
        std::wstring source;
        std::wstring destination;
        SyncOptions options;
    };

    class SyncProfileIO {
    public:
        static bool SaveToFile(const SyncProfile& profile, const std::wstring& filePath, std::wstring& errorMessage);
        static bool LoadFromFile(const std::wstring& filePath, SyncProfile& profile, std::wstring& errorMessage);
    };

} // namespace ChronoSync

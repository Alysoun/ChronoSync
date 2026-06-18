#pragma once

#include "SyncOptions.h"
#include <string>
#include <vector>

namespace ChronoSync {

    struct SyncJob {
        std::wstring name;
        std::wstring source;
        std::wstring destination;
        SyncOptions options;
    };

    class SyncJobQueueIO {
    public:
        static bool SaveToFile(const std::vector<SyncJob>& jobs, const std::wstring& filePath, std::wstring& errorMessage);
        static bool LoadFromFile(const std::wstring& filePath, std::vector<SyncJob>& jobs, std::wstring& errorMessage);
    };

} // namespace ChronoSync

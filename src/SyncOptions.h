#pragma once

#include "PathFilter.h"
#include <cstddef>

namespace ChronoSync {

    enum class CompareMode {
        Timestamp = 0,
        Sha256 = 1
    };

    struct SyncOptions {
        bool prune = false;
        FilterOptions filters;
        CompareMode compareMode = CompareMode::Timestamp;
        bool verifyAfterCopy = false;
        bool versionedBackups = true;
        size_t maxBackupVersions = 5;
    };

} // namespace ChronoSync

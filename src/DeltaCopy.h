#pragma once

#include <functional>
#include <string>

namespace PrevueSync {

    struct DeltaCopyResult {
        bool success = false;
        unsigned long long bytesWritten = 0;
        std::wstring errorMessage;
    };

    class DeltaCopy {
    public:
        // Compares source vs destination in fixed-size blocks, writes a full temp file
        // (reusing unchanged destination blocks where possible), then atomically replaces.
        // bytesWritten counts only blocks that differ from the destination.
        static DeltaCopyResult CopyFileBlocks(const std::wstring& sourcePath,
                                        const std::wstring& destinationPath,
                                        unsigned long long fileSizeBytes,
                                        const std::function<void(unsigned long long, unsigned long long)>& progressCallback);
    };

} // namespace PrevueSync

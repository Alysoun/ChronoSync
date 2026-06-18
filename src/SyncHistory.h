#pragma once

#include "SyncEngine.h"
#include "SyncOptions.h"
#include <string>
#include <vector>

namespace ChronoSync {

    struct SnapshotFileEntry {
        std::wstring relativePath;
        unsigned long long fileSize = 0;
        unsigned long mtimeLow = 0;
        unsigned long mtimeHigh = 0;
        bool isDirectory = false;
    };

    struct DestinationSnapshot {
        std::wstring id;
        std::wstring timestampUtc;
        std::vector<SnapshotFileEntry> entries;
    };

    struct SyncHistoryEntry {
        std::wstring id;
        std::wstring timestampUtc;
        std::wstring source;
        std::wstring destination;
        size_t filesCopied = 0;
        size_t filesSkipped = 0;
        size_t itemsDeleted = 0;
        size_t dirsCreated = 0;
        unsigned long long totalBytesCopied = 0;
        std::wstring snapshotId;
    };

    struct SnapshotDiffResult {
        size_t added = 0;
        size_t removed = 0;
        size_t modified = 0;
        unsigned long long addedBytes = 0;
        unsigned long long removedBytes = 0;
        unsigned long long modifiedBytes = 0;
        std::vector<std::wstring> sampleAdded;
        std::vector<std::wstring> sampleRemoved;
        std::vector<std::wstring> sampleModified;
    };

    class SyncHistoryIO {
    public:
        static constexpr int MaxRuns = 100;

        static bool RecordRun(const std::wstring& source,
                              const std::wstring& destination,
                              const SyncOptions& options,
                              const SyncStats& stats,
                              std::wstring& errorMessage);

        static bool LoadEntries(const std::wstring& destination,
                                std::vector<SyncHistoryEntry>& entries,
                                std::wstring& errorMessage);

        static std::vector<SyncHistoryEntry> QuerySinceDays(const std::wstring& destination, int days);

        static bool LoadSnapshot(const std::wstring& destination,
                                 const std::wstring& snapshotId,
                                 DestinationSnapshot& snapshot,
                                 std::wstring& errorMessage);

        static SnapshotDiffResult DiffSnapshots(const DestinationSnapshot& older,
                                                const DestinationSnapshot& newer);

        static std::wstring FormatHistoryReport(const std::vector<SyncHistoryEntry>& entries,
                                                int daysFilter = 0);

        static std::wstring FormatSnapshotDiffReport(const SnapshotDiffResult& diff,
                                                     const std::wstring& labelOlder,
                                                     const std::wstring& labelNewer);
    };

} // namespace ChronoSync

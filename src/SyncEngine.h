#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <filesystem>
#include <functional>
#include "PathFilter.h"
#include "SyncOptions.h"

namespace ChronoSync {

    // Represents an item scanned in a directory
    struct SyncItem {
        std::wstring relativePath;
        unsigned long long fileSize = 0;
        FILETIME lastWriteTime = {0};
        bool isDirectory = false;
        bool isReparsePoint = false;
        unsigned long reparseTag = 0;
        std::wstring reparseTarget;
    };

    // Represents a planned change in dry-run preview mode
    struct PreviewItem {
        std::wstring relativePath;
        std::wstring action; // L"Copy (New)", L"Copy (Update)", L"Delete", L"Create Dir", etc.
        std::wstring sizeStr;
        unsigned long long fileSize = 0;
        bool isReparsePoint = false;
        unsigned long reparseTag = 0;
        std::wstring reparseTarget;
    };

    // Callback structures for reporting engine events to the console UI
    struct SyncCallbacks {
        std::function<void(const std::wstring& rootDir)> onScanStart;
        std::function<void(const std::wstring& subDir)> onScanDir;
        std::function<void(size_t totalItems)> onScanComplete;
        std::function<void()> onCompareStart;
        std::function<void(size_t dirsToCreate, size_t filesToCopy, size_t itemsToDelete)> onCompareComplete;
        std::function<void(const std::wstring& relPath, unsigned long long fileSizeBytes, size_t fileIndex, size_t totalFiles)> onCopyStart;
        std::function<void(unsigned long long bytesCopied, unsigned long long fileSizeBytes)> onCopyProgress;
        std::function<void(const std::wstring& relPath, bool success, const std::wstring& errorMessage)> onCopyComplete;
        std::function<void(const std::wstring& relPath, bool isDirectory)> onDeleteItem;
        std::function<void(const std::wstring& relPath, const std::wstring& errorMessage)> onDeleteFailed;
        std::function<void(const std::wstring& message, bool isError)> onLog;
    };

    // Statistics about the completed sync run
    struct SyncStats {
        size_t dirsCreated = 0;
        size_t filesCopied = 0;
        size_t filesSkipped = 0;
        size_t itemsDeleted = 0;
        size_t filesVerified = 0;
        size_t verifyFailures = 0;
        unsigned long long totalBytesCopied = 0;
        double scanTimeMs = 0.0;
        double compareTimeMs = 0.0;
        double copyTimeMs = 0.0;
        double totalTimeMs = 0.0;
    };

    class SyncEngine {
    public:
        static std::vector<SyncItem> ScanDirectory(const std::wstring& rootDir, const FilterOptions& filters, const SyncCallbacks& callbacks);

        static SyncStats Sync(const std::wstring& source, const std::wstring& destination, const SyncOptions& options, const SyncCallbacks& callbacks);

        static std::vector<PreviewItem> Preview(const std::wstring& source, const std::wstring& destination, const SyncOptions& options, const SyncCallbacks& callbacks);

        static bool UndoPruning(const std::wstring& destination, const SyncCallbacks& callbacks);

        static bool HasRestorableBackups(const std::wstring& destination);
    };

} // namespace ChronoSync

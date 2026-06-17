#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <filesystem>
#include <functional>

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
        // Called when starting to scan a root directory
        std::function<void(const std::wstring& rootDir)> onScanStart;
        
        // Called when scanning a subdirectory
        std::function<void(const std::wstring& subDir)> onScanDir;
        
        // Called when directory scanning completes
        std::function<void(size_t totalItems)> onScanComplete;
        
        // Called when beginning the comparison phase
        std::function<void()> onCompareStart;
        
        // Called when comparison completes, detailing work queues
        std::function<void(size_t dirsToCreate, size_t filesToCopy, size_t itemsToDelete)> onCompareComplete;
        
        // Called when copying a file begins
        std::function<void(const std::wstring& relPath, unsigned long long fileSizeBytes, size_t fileIndex, size_t totalFiles)> onCopyStart;
        
        // Called periodically during binary copy stream
        std::function<void(unsigned long long bytesCopied, unsigned long long fileSizeBytes)> onCopyProgress;
        
        // Called when a file copy completes (success or failure)
        std::function<void(const std::wstring& relPath, bool success, const std::wstring& errorMessage)> onCopyComplete;
        
        // Called when deleting an item (for pruning)
        std::function<void(const std::wstring& relPath, bool isDirectory)> onDeleteItem;

        // Called when deleting an item fails
        std::function<void(const std::wstring& relPath, const std::wstring& errorMessage)> onDeleteFailed;
        
        // Logger callback for general notifications/errors
        std::function<void(const std::wstring& message, bool isError)> onLog;
    };

    // Statistics about the completed sync run
    struct SyncStats {
        size_t dirsCreated = 0;
        size_t filesCopied = 0;
        size_t filesSkipped = 0;
        size_t itemsDeleted = 0;
        unsigned long long totalBytesCopied = 0;
        double scanTimeMs = 0.0;
        double compareTimeMs = 0.0;
        double copyTimeMs = 0.0;
        double totalTimeMs = 0.0;
    };

    class SyncEngine {
    public:
        // Helper to perform recursive scanning using FindFirstFileW/FindNextFileW
        static std::vector<SyncItem> ScanDirectory(const std::wstring& rootDir, const SyncCallbacks& callbacks);

        // Run the full synchronization flow
        static SyncStats Sync(const std::wstring& source, const std::wstring& destination, bool prune, const SyncCallbacks& callbacks);

        // Run a dry-run comparison and return preview details
        static std::vector<PreviewItem> Preview(const std::wstring& source, const std::wstring& destination, bool prune, const SyncCallbacks& callbacks);

        // Restore pruned files from .chrono_trash folder back to destination
        static bool UndoPruning(const std::wstring& destination, const SyncCallbacks& callbacks);
    };

} // namespace ChronoSync

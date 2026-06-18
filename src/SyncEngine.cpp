#include "SyncEngine.h"
#include "FileHash.h"
#include <windows.h>
#include <iostream>
#include <fstream>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <chrono>
#include <array>

#ifndef IO_REPARSE_TAG_MOUNT_POINT
#define IO_REPARSE_TAG_MOUNT_POINT 0xA0000003L
#endif
#ifndef IO_REPARSE_TAG_SYMLINK
#define IO_REPARSE_TAG_SYMLINK 0xA000000CL
#endif

namespace ChronoSync {

    static bool FileContentsDiffer(const std::filesystem::path& srcPath,
                                   const std::filesystem::path& destPath,
                                   const SyncItem& srcItem,
                                   const SyncItem& destItem,
                                   CompareMode mode) {
        if (srcItem.fileSize != destItem.fileSize) {
            return true;
        }

        if (mode == CompareMode::Timestamp) {
            LONG cmp = CompareFileTime(&srcItem.lastWriteTime, &destItem.lastWriteTime);
            return cmp > 0;
        }

        std::array<uint8_t, 32> srcHash{};
        std::array<uint8_t, 32> destHash{};
        if (!FileHash::Sha256File(srcPath.wstring(), srcHash) ||
            !FileHash::Sha256File(destPath.wstring(), destHash)) {
            return true;
        }
        return !FileHash::HashesEqual(srcHash, destHash);
    }

    static std::wstring MakeBackupTimestamp() {
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t buf[32];
        swprintf_s(buf, L"%04d-%02d-%02d_%02d%02d%02d",
                   st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        return buf;
    }

    static void PruneOldBackupVersions(const std::filesystem::path& backupRoot, size_t maxVersions, std::error_code& ec) {
        if (!std::filesystem::exists(backupRoot, ec)) {
            return;
        }

        std::vector<std::filesystem::path> versions;
        for (const auto& entry : std::filesystem::directory_iterator(backupRoot, ec)) {
            if (entry.is_directory()) {
                versions.push_back(entry.path());
            }
        }

        std::sort(versions.begin(), versions.end(), std::greater<std::filesystem::path>());
        for (size_t i = maxVersions; i < versions.size(); ++i) {
            std::filesystem::remove_all(versions[i], ec);
        }
    }

    static std::filesystem::path GetLatestBackupFolder(const std::filesystem::path& backupRoot) {
        std::error_code ec;
        std::filesystem::path latest;
        if (!std::filesystem::exists(backupRoot, ec)) {
            return latest;
        }

        for (const auto& entry : std::filesystem::directory_iterator(backupRoot, ec)) {
            if (entry.is_directory()) {
                if (latest.empty() || entry.path().filename().wstring() > latest.filename().wstring()) {
                    latest = entry.path();
                }
            }
        }
        return latest;
    }

    static bool VerifyCopiedFile(const std::filesystem::path& srcPath, const std::filesystem::path& destPath) {
        std::array<uint8_t, 32> srcHash{};
        std::array<uint8_t, 32> destHash{};
        if (!FileHash::Sha256File(srcPath.wstring(), srcHash) ||
            !FileHash::Sha256File(destPath.wstring(), destHash)) {
            return false;
        }
        return FileHash::HashesEqual(srcHash, destHash);
    }

    // Helper recursive directory scanner using FindFirstFileW/FindNextFileW
    static void ScanDirectoryHelper(const std::wstring& rootDir, const std::wstring& subDir,
                                    const FilterOptions& filters,
                                    std::vector<SyncItem>& items, const SyncCallbacks& callbacks) {
        std::wstring searchPath = rootDir + L"\\" + (subDir.empty() ? L"" : subDir + L"\\") + L"*";
        WIN32_FIND_DATAW findData;
        HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
        
        if (hFind == INVALID_HANDLE_VALUE) {
            return;
        }

        do {
            std::wstring name = findData.cFileName;
            if (name == L"." || name == L"..") {
                continue;
            }

            // Safety guard: skip internal trash, backups, and temp items at all levels
            if (name == L".chrono_trash" || name == L".chrono_backups") {
                continue;
            }
            if (name == L".chrono_tmp" || (name.size() >= 11 && name.compare(name.size() - 11, 11, L".chrono_tmp") == 0)) {
                continue;
            }

            std::wstring relPath = subDir.empty() ? name : subDir + L"\\" + name;
            bool isDirectory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

            if (isDirectory && PathFilter::ShouldSkipDirectory(filters, name)) {
                continue;
            }
            if (PathFilter::IsExcluded(filters, relPath, name, isDirectory)) {
                continue;
            }

            SyncItem item;
            item.relativePath = relPath;
            item.isDirectory = isDirectory;
            item.lastWriteTime = findData.ftLastWriteTime;

            bool isReparsePoint = (findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
            item.isReparsePoint = isReparsePoint;

            if (isReparsePoint) {
                item.reparseTag = findData.dwReserved0;
                std::error_code ec;
                std::wstring fullPath = rootDir + L"\\" + relPath;
                auto target = std::filesystem::read_symlink(fullPath, ec);
                if (!ec) {
                    std::wstring targetStr = target.wstring();
                    // Strip NT namespace manager prefix if present (\??\ or \\?\)
                    if (targetStr.size() >= 4 && 
                        ((targetStr[0] == L'\\' && targetStr[1] == L'?' && targetStr[2] == L'?' && targetStr[3] == L'\\') ||
                         (targetStr[0] == L'\\' && targetStr[1] == L'\\' && targetStr[2] == L'?' && targetStr[3] == L'\\'))) {
                        targetStr = targetStr.substr(4);
                    }
                    item.reparseTarget = targetStr;
                }
            }

            if (!item.isDirectory) {
                ULARGE_INTEGER fileSize;
                fileSize.LowPart = findData.nFileSizeLow;
                fileSize.HighPart = findData.nFileSizeHigh;
                item.fileSize = fileSize.QuadPart;
            }

            items.push_back(item);

            if (item.isDirectory) {
                if (callbacks.onScanDir) {
                    callbacks.onScanDir(relPath);
                }
                // Only recurse if the directory is NOT a symbolic link/junction (reparse point)
                if (!isReparsePoint) {
                    ScanDirectoryHelper(rootDir, relPath, filters, items, callbacks);
                }
            }
        } while (FindNextFileW(hFind, &findData));

        FindClose(hFind);
    }

    std::vector<SyncItem> SyncEngine::ScanDirectory(const std::wstring& rootDir, const FilterOptions& filters, const SyncCallbacks& callbacks) {
        std::vector<SyncItem> items;
        if (callbacks.onScanStart) {
            callbacks.onScanStart(rootDir);
        }
        
        if (std::filesystem::exists(rootDir)) {
            ScanDirectoryHelper(rootDir, L"", filters, items, callbacks);
        }
        
        if (callbacks.onScanComplete) {
            callbacks.onScanComplete(items.size());
        }
        return items;
    }

    // Context for CopyProgressCallback
    struct CopyContext {
        std::function<void(unsigned long long bytesCopied, unsigned long long fileSize)> progressCallback;
    };

    // Native CopyFileExW progress tracking routine
    static DWORD CALLBACK CopyProgressCallback(
        LARGE_INTEGER TotalFileSize,
        LARGE_INTEGER TotalBytesTransferred,
        LARGE_INTEGER StreamSize,
        LARGE_INTEGER StreamBytesTransferred,
        DWORD dwStreamNumber,
        DWORD dwCallbackReason,
        HANDLE hSourceFile,
        HANDLE hDestinationFile,
        LPVOID lpData
    ) {
        (void)StreamSize;
        (void)StreamBytesTransferred;
        (void)dwStreamNumber;
        (void)dwCallbackReason;
        (void)hSourceFile;
        (void)hDestinationFile;

        CopyContext* ctx = static_cast<CopyContext*>(lpData);
        if (ctx && ctx->progressCallback) {
            ctx->progressCallback(TotalBytesTransferred.QuadPart, TotalFileSize.QuadPart);
        }
        return PROGRESS_CONTINUE;
    }

    SyncStats SyncEngine::Sync(const std::wstring& source, const std::wstring& destination, const SyncOptions& options, const SyncCallbacks& callbacks) {
        auto startTime = std::chrono::high_resolution_clock::now();
        SyncStats stats;

        std::filesystem::path srcRoot(source);
        std::filesystem::path destRoot(destination);
        const bool prune = options.prune;
        const FilterOptions& filters = options.filters;

        // 1. Validate source directory
        if (!std::filesystem::exists(srcRoot)) {
            if (callbacks.onLog) {
                callbacks.onLog(L"Source directory does not exist: " + source, true);
            }
            return stats;
        }

        // 2. Scan source directory
        auto scanStart = std::chrono::high_resolution_clock::now();
        std::vector<SyncItem> srcItems = ScanDirectory(source, filters, callbacks);
        auto scanEnd = std::chrono::high_resolution_clock::now();
        stats.scanTimeMs = std::chrono::duration<double, std::milli>(scanEnd - scanStart).count();

        // 3. Scan destination directory (if exists)
        std::vector<SyncItem> destItems;
        if (std::filesystem::exists(destRoot)) {
            destItems = ScanDirectory(destination, filters, callbacks);
        }

        // 4. Compare directories
        if (callbacks.onCompareStart) {
            callbacks.onCompareStart();
        }
        auto compareStart = std::chrono::high_resolution_clock::now();

        // Build mapping for destination items
        std::unordered_map<std::wstring, SyncItem> destMap;
        for (const auto& item : destItems) {
            destMap[item.relativePath] = item;
        }

        std::unordered_set<std::wstring> srcRelPaths;
        for (const auto& item : srcItems) {
            srcRelPaths.insert(item.relativePath);
        }

        std::vector<SyncItem> dirsToCreate;
        std::vector<SyncItem> filesToCopy;
        std::vector<SyncItem> linksToCreate;
        std::vector<SyncItem> itemsToDelete;

        // Find items in source that need to be created/copied/linked
        for (const auto& srcItem : srcItems) {
            auto it = destMap.find(srcItem.relativePath);
            if (srcItem.isReparsePoint) {
                if (it == destMap.end()) {
                    linksToCreate.push_back(srcItem);
                } else {
                    const auto& destItem = it->second;
                    if (!destItem.isReparsePoint || 
                        srcItem.reparseTag != destItem.reparseTag || 
                        srcItem.reparseTarget != destItem.reparseTarget) {
                        itemsToDelete.push_back(destItem);
                        linksToCreate.push_back(srcItem);
                    }
                }
            } else if (srcItem.isDirectory) {
                if (it == destMap.end()) {
                    dirsToCreate.push_back(srcItem);
                } else {
                    const auto& destItem = it->second;
                    if (destItem.isReparsePoint) {
                        itemsToDelete.push_back(destItem);
                        dirsToCreate.push_back(srcItem);
                    }
                }
            } else {
                bool needsCopy = false;
                if (it == destMap.end()) {
                    needsCopy = true;
                } else {
                    const auto& destItem = it->second;
                    if (destItem.isReparsePoint) {
                        itemsToDelete.push_back(destItem);
                        needsCopy = true;
                    } else {
                        std::filesystem::path srcPath = srcRoot / srcItem.relativePath;
                        std::filesystem::path destPath = destRoot / srcItem.relativePath;
                        needsCopy = FileContentsDiffer(srcPath, destPath, srcItem, destItem, options.compareMode);
                    }
                }

                if (needsCopy) {
                    filesToCopy.push_back(srcItem);
                } else {
                    stats.filesSkipped++;
                }
            }
        }

        // Find items in destination that no longer exist in source (for pruning)
        if (prune) {
            for (const auto& destItem : destItems) {
                if (srcRelPaths.find(destItem.relativePath) == srcRelPaths.end()) {
                    itemsToDelete.push_back(destItem);
                }
            }
        }

        // Sort deletion items by length descending so children are deleted before parents
        std::sort(itemsToDelete.begin(), itemsToDelete.end(), [](const SyncItem& a, const SyncItem& b) {
            return a.relativePath.length() > b.relativePath.length();
        });

        auto compareEnd = std::chrono::high_resolution_clock::now();
        stats.compareTimeMs = std::chrono::duration<double, std::milli>(compareEnd - compareStart).count();

        if (callbacks.onCompareComplete) {
            callbacks.onCompareComplete(dirsToCreate.size(), filesToCopy.size() + linksToCreate.size(), itemsToDelete.size());
        }

        // 5. Execute synchronization actions
        auto copyStart = std::chrono::high_resolution_clock::now();

        // 5.1 Prune/Clean Destination Files/Folders/Links
        std::filesystem::path backupRoot = destRoot / L".chrono_backups";
        std::filesystem::path trashRoot;
        std::error_code ec;
        bool backupFolderCreated = false;

        for (const auto& item : itemsToDelete) {
            std::filesystem::path fullDestPath = destRoot / item.relativePath;

            if (callbacks.onDeleteItem) {
                callbacks.onDeleteItem(item.relativePath, item.isDirectory);
            }

            if (item.isReparsePoint) {
                // Delete reparse points safely using native Win32 APIs (never move to .chrono_trash)
                BOOL ok;
                if (item.isDirectory) {
                    ok = RemoveDirectoryW(fullDestPath.c_str());
                } else {
                    ok = DeleteFileW(fullDestPath.c_str());
                }
                if (ok) {
                    stats.itemsDeleted++;
                } else {
                    DWORD err = GetLastError();
                    std::wstring errStr = L"Native link delete failed. Win32 Error: " + std::to_wstring(err);
                    if (callbacks.onDeleteFailed) {
                        callbacks.onDeleteFailed(item.relativePath, errStr);
                    }
                }
            } else {
                if (prune) {
                    if (!backupFolderCreated) {
                        if (options.versionedBackups) {
                            trashRoot = backupRoot / MakeBackupTimestamp();
                        } else {
                            trashRoot = destRoot / L".chrono_trash";
                            std::filesystem::remove_all(trashRoot, ec);
                        }
                        std::filesystem::create_directories(trashRoot, ec);
                        backupFolderCreated = true;
                    }

                    std::filesystem::path trashPath = trashRoot / item.relativePath;
                    std::filesystem::create_directories(trashPath.parent_path(), ec);
                    std::filesystem::rename(fullDestPath, trashPath, ec);
                    if (!ec) {
                        stats.itemsDeleted++;
                    } else {
                        bool success = std::filesystem::remove_all(fullDestPath, ec);
                        if (success && !ec) {
                            stats.itemsDeleted++;
                        } else {
                            std::wstring errStr = ec ? std::wstring(ec.message().begin(), ec.message().end()) : L"Item not found";
                            if (callbacks.onDeleteFailed) {
                                callbacks.onDeleteFailed(item.relativePath, errStr);
                            }
                        }
                    }
                } else {
                    // Prune is false: permanent delete of collision
                    std::filesystem::remove_all(fullDestPath, ec);
                    if (!ec) {
                        stats.itemsDeleted++;
                    } else {
                        std::wstring errStr = std::wstring(ec.message().begin(), ec.message().end());
                        if (callbacks.onDeleteFailed) {
                            callbacks.onDeleteFailed(item.relativePath, errStr);
                        }
                    }
                }
            }
        }

        if (prune && options.versionedBackups && backupFolderCreated) {
            PruneOldBackupVersions(backupRoot, options.maxBackupVersions, ec);
        }

        // 5.2 Create Directories
        for (const auto& dir : dirsToCreate) {
            std::filesystem::path destPath = destRoot / dir.relativePath;
            std::filesystem::create_directories(destPath, ec);
            if (!ec) {
                stats.dirsCreated++;
            } else {
                if (callbacks.onLog) {
                    std::wstring errStr(ec.message().begin(), ec.message().end());
                    callbacks.onLog(L"Failed to create directory " + dir.relativePath + L": " + errStr, true);
                }
            }
        }

        // 5.3 Copy Files Atomically via CopyFileExW
        for (size_t i = 0; i < filesToCopy.size(); ++i) {
            const auto& fileItem = filesToCopy[i];
            std::filesystem::path srcPath = srcRoot / fileItem.relativePath;
            std::filesystem::path destPath = destRoot / fileItem.relativePath;
            std::filesystem::path tmpPath = destPath;
            tmpPath += L".chrono_tmp";

            if (callbacks.onCopyStart) {
                callbacks.onCopyStart(fileItem.relativePath, fileItem.fileSize, i + 1, filesToCopy.size() + linksToCreate.size());
            }

            // Create parent directories if missing (safety check)
            std::filesystem::create_directories(destPath.parent_path(), ec);

            // Clean up any existing temp file
            std::filesystem::remove(tmpPath, ec);

            CopyContext context;
            context.progressCallback = callbacks.onCopyProgress;

            // Perform kernel-optimized copy to temp file
            BOOL copySuccess = CopyFileExW(
                srcPath.c_str(),
                tmpPath.c_str(),
                (LPPROGRESS_ROUTINE)CopyProgressCallback,
                &context,
                NULL,
                0
            );

            if (copySuccess) {
                // Perform the atomic Move operation
                BOOL moveSuccess = MoveFileExW(tmpPath.c_str(), destPath.c_str(), 
                                              MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
                if (moveSuccess) {
                    // Explicitly set the timestamp on the destination file to preserve 100ns precision post-swap
                    HANDLE hFile = CreateFileW(destPath.c_str(), FILE_WRITE_ATTRIBUTES,
                                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                               NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
                    if (hFile != INVALID_HANDLE_VALUE) {
                        SetFileTime(hFile, NULL, NULL, &fileItem.lastWriteTime);
                        CloseHandle(hFile);
                    }

                    bool copyOk = true;
                    std::wstring verifyError;
                    if (options.verifyAfterCopy || options.compareMode == CompareMode::Sha256) {
                        stats.filesVerified++;
                        if (!VerifyCopiedFile(srcPath, destPath)) {
                            copyOk = false;
                            verifyError = L"SHA256 verification failed after copy";
                            stats.verifyFailures++;
                        }
                    }

                    if (copyOk) {
                        stats.filesCopied++;
                        stats.totalBytesCopied += fileItem.fileSize;

                        if (callbacks.onCopyComplete) {
                            callbacks.onCopyComplete(fileItem.relativePath, true, L"");
                        }
                    } else {
                        std::filesystem::remove(destPath, ec);
                        if (callbacks.onCopyComplete) {
                            callbacks.onCopyComplete(fileItem.relativePath, false, verifyError);
                        }
                        if (callbacks.onLog) {
                            callbacks.onLog(L"Verification failed: " + fileItem.relativePath, true);
                        }
                    }
                } else {
                    DWORD err = GetLastError();
                    std::wstring errStr = L"Atomic move failed. Win32 Error: " + std::to_wstring(err);
                    std::filesystem::remove(tmpPath, ec);
                    if (callbacks.onCopyComplete) {
                        callbacks.onCopyComplete(fileItem.relativePath, false, errStr);
                    }
                }
            } else {
                DWORD err = GetLastError();
                std::wstring errStr = L"CopyFileExW failed. Win32 Error: " + std::to_wstring(err);
                std::filesystem::remove(tmpPath, ec);
                if (callbacks.onCopyComplete) {
                    callbacks.onCopyComplete(fileItem.relativePath, false, errStr);
                }
            }
        }

        // 5.4 Recreate Directory Junctions and Symbolic Links
        for (size_t i = 0; i < linksToCreate.size(); ++i) {
            const auto& linkItem = linksToCreate[i];
            std::filesystem::path destPath = destRoot / linkItem.relativePath;

            if (callbacks.onCopyStart) {
                callbacks.onCopyStart(linkItem.relativePath, 0, filesToCopy.size() + i + 1, filesToCopy.size() + linksToCreate.size());
            }

            // Ensure parent directories exist
            std::filesystem::create_directories(destPath.parent_path(), ec);

            bool success = false;
            std::wstring linkTypeStr;
            std::wstring errStr;

            if (linkItem.reparseTag == IO_REPARSE_TAG_MOUNT_POINT) {
                linkTypeStr = L"junction";
                // Silently run: cmd.exe /c mklink /j "dest" "target"
                std::wstring cmdArgs = L"cmd.exe /c mklink /j \"" + destPath.wstring() + L"\" \"" + linkItem.reparseTarget + L"\"";
                std::vector<wchar_t> cmdBuffer(cmdArgs.begin(), cmdArgs.end());
                cmdBuffer.push_back(L'\0');

                STARTUPINFOW si = { sizeof(si) };
                si.dwFlags = STARTF_USESHOWWINDOW;
                si.wShowWindow = SW_HIDE;
                PROCESS_INFORMATION pi = { 0 };

                BOOL procSuccess = CreateProcessW(
                    NULL,
                    cmdBuffer.data(),
                    NULL,
                    NULL,
                    FALSE,
                    CREATE_NO_WINDOW,
                    NULL,
                    NULL,
                    &si,
                    &pi
                );

                if (procSuccess) {
                    WaitForSingleObject(pi.hProcess, INFINITE);
                    DWORD exitCode = 0;
                    GetExitCodeProcess(pi.hProcess, &exitCode);
                    CloseHandle(pi.hProcess);
                    CloseHandle(pi.hThread);
                    if (exitCode == 0) {
                        success = true;
                    } else {
                        errStr = L"mklink process exited with code " + std::to_wstring(exitCode);
                    }
                } else {
                    DWORD err = GetLastError();
                    errStr = L"CreateProcessW failed. Win32 Error: " + std::to_wstring(err);
                }
            } else {
                linkTypeStr = L"symlink";
                if (linkItem.isDirectory) {
                    std::filesystem::create_directory_symlink(linkItem.reparseTarget, destPath, ec);
                } else {
                    std::filesystem::create_symlink(linkItem.reparseTarget, destPath, ec);
                }
                if (!ec) {
                    success = true;
                } else {
                    errStr = std::wstring(ec.message().begin(), ec.message().end());
                }
            }

            if (success) {
                if (linkItem.isDirectory) {
                    stats.dirsCreated++;
                } else {
                    stats.filesCopied++;
                }
                if (callbacks.onCopyComplete) {
                    callbacks.onCopyComplete(linkItem.relativePath, true, L"");
                }
                if (callbacks.onLog) {
                    callbacks.onLog(L"Created " + linkTypeStr + L": " + linkItem.relativePath + L" -> " + linkItem.reparseTarget, false);
                }
            } else {
                if (callbacks.onCopyComplete) {
                    callbacks.onCopyComplete(linkItem.relativePath, false, errStr);
                }
                if (callbacks.onLog) {
                    callbacks.onLog(L"Failed to create " + linkTypeStr + L": " + linkItem.relativePath + L" -> " + linkItem.reparseTarget + L". Error: " + errStr, true);
                }
            }
        }

        auto copyEnd = std::chrono::high_resolution_clock::now();
        stats.copyTimeMs = std::chrono::duration<double, std::milli>(copyEnd - copyStart).count();

        auto endTime = std::chrono::high_resolution_clock::now();
        stats.totalTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

        return stats;
    }

    // Helper to format bytes to wide string
    static std::wstring FormatBytesWide(unsigned long long bytes) {
        double size = static_cast<double>(bytes);
        int unitIndex = 0;
        const wchar_t* units[] = { L"Bytes", L"KB", L"MB", L"GB", L"TB" };
        while (size >= 1024.0 && unitIndex < 4) {
            size /= 1024.0;
            unitIndex++;
        }
        wchar_t buf[64];
        swprintf_s(buf, L"%.2f %s", size, units[unitIndex]);
        return std::wstring(buf);
    }

    std::vector<PreviewItem> SyncEngine::Preview(const std::wstring& source, const std::wstring& destination, const SyncOptions& options, const SyncCallbacks& callbacks) {
        std::vector<PreviewItem> previewList;
        const bool prune = options.prune;
        const FilterOptions& filters = options.filters;

        std::filesystem::path srcRoot(source);
        std::filesystem::path destRoot(destination);

        // 1. Validate source directory
        if (!std::filesystem::exists(srcRoot)) {
            if (callbacks.onLog) {
                callbacks.onLog(L"Source directory does not exist: " + source, true);
            }
            return previewList;
        }

        // 2. Scan source directory
        std::vector<SyncItem> srcItems = ScanDirectory(source, filters, callbacks);

        // 3. Scan destination directory (if exists)
        std::vector<SyncItem> destItems;
        if (std::filesystem::exists(destRoot)) {
            destItems = ScanDirectory(destination, filters, callbacks);
        }

        // 4. Compare directories
        if (callbacks.onCompareStart) {
            callbacks.onCompareStart();
        }

        std::unordered_map<std::wstring, SyncItem> destMap;
        for (const auto& item : destItems) {
            destMap[item.relativePath] = item;
        }

        std::unordered_set<std::wstring> srcRelPaths;
        for (const auto& item : srcItems) {
            srcRelPaths.insert(item.relativePath);
        }

        for (const auto& srcItem : srcItems) {
            auto it = destMap.find(srcItem.relativePath);
            if (srcItem.isReparsePoint) {
                bool needsLink = false;
                if (it == destMap.end()) {
                    needsLink = true;
                } else {
                    const auto& destItem = it->second;
                    if (!destItem.isReparsePoint || 
                        srcItem.reparseTag != destItem.reparseTag || 
                        srcItem.reparseTarget != destItem.reparseTarget) {
                        needsLink = true;
                    }
                }

                if (needsLink) {
                    PreviewItem pi;
                    pi.relativePath = srcItem.relativePath;
                    pi.action = (srcItem.reparseTag == IO_REPARSE_TAG_MOUNT_POINT) ? L"Create Junction" : L"Create Symlink";
                    pi.sizeStr = L"-> " + srcItem.reparseTarget;
                    pi.fileSize = 0;
                    pi.isReparsePoint = true;
                    pi.reparseTag = srcItem.reparseTag;
                    pi.reparseTarget = srcItem.reparseTarget;
                    previewList.push_back(pi);
                }
            } else if (srcItem.isDirectory) {
                bool needsDir = false;
                if (it == destMap.end()) {
                    needsDir = true;
                } else {
                    const auto& destItem = it->second;
                    if (destItem.isReparsePoint) {
                        needsDir = true;
                    }
                }

                if (needsDir) {
                    PreviewItem pi;
                    pi.relativePath = srcItem.relativePath;
                    pi.action = L"Create Dir";
                    pi.sizeStr = L"-";
                    pi.fileSize = 0;
                    previewList.push_back(pi);
                }
            } else {
                bool needsCopy = false;
                std::wstring actionType = L"Copy (New)";
                
                if (it == destMap.end()) {
                    needsCopy = true;
                } else {
                    const auto& destItem = it->second;
                    if (destItem.isReparsePoint) {
                        needsCopy = true;
                    } else {
                        std::filesystem::path srcPath = srcRoot / srcItem.relativePath;
                        std::filesystem::path destPath = destRoot / srcItem.relativePath;
                        if (FileContentsDiffer(srcPath, destPath, srcItem, destItem, options.compareMode)) {
                            needsCopy = true;
                            actionType = L"Copy (Update)";
                        }
                    }
                }

                if (needsCopy) {
                    PreviewItem pi;
                    pi.relativePath = srcItem.relativePath;
                    pi.action = actionType;
                    pi.sizeStr = FormatBytesWide(srcItem.fileSize);
                    pi.fileSize = srcItem.fileSize;
                    previewList.push_back(pi);
                }
            }
        }

        if (prune) {
            for (const auto& destItem : destItems) {
                if (srcRelPaths.find(destItem.relativePath) == srcRelPaths.end()) {
                    PreviewItem pi;
                    pi.relativePath = destItem.relativePath;
                    pi.action = L"Delete";
                    pi.isReparsePoint = destItem.isReparsePoint;
                    pi.reparseTag = destItem.reparseTag;
                    pi.reparseTarget = destItem.reparseTarget;
                    if (destItem.isReparsePoint) {
                        pi.sizeStr = L"-> " + destItem.reparseTarget;
                        pi.fileSize = 0;
                    } else if (destItem.isDirectory) {
                        pi.sizeStr = L"-";
                        pi.fileSize = 0;
                    } else {
                        pi.sizeStr = FormatBytesWide(destItem.fileSize);
                        pi.fileSize = destItem.fileSize;
                    }
                    previewList.push_back(pi);
                }
            }
        }

        if (callbacks.onCompareComplete) {
            size_t dirs = 0, copies = 0, deletes = 0;
            for (const auto& pi : previewList) {
                if (pi.action == L"Create Dir") dirs++;
                else if (pi.action == L"Delete") deletes++;
                else copies++;
            }
            callbacks.onCompareComplete(dirs, copies, deletes);
        }

        return previewList;
    }

    bool SyncEngine::HasRestorableBackups(const std::wstring& destination) {
        std::filesystem::path destRoot(destination);
        std::error_code ec;
        if (std::filesystem::exists(destRoot / L".chrono_trash", ec)) {
            return true;
        }

        std::filesystem::path backupRoot = destRoot / L".chrono_backups";
        if (!std::filesystem::exists(backupRoot, ec)) {
            return false;
        }

        for (const auto& entry : std::filesystem::directory_iterator(backupRoot, ec)) {
            if (entry.is_directory()) {
                return true;
            }
        }
        return false;
    }

    bool SyncEngine::UndoPruning(const std::wstring& destination, const SyncCallbacks& callbacks) {
        std::filesystem::path destRoot(destination);
        std::filesystem::path backupRoot = destRoot / L".chrono_backups";
        std::filesystem::path trashRoot = GetLatestBackupFolder(backupRoot);

        if (trashRoot.empty()) {
            trashRoot = destRoot / L".chrono_trash";
        }

        if (!std::filesystem::exists(trashRoot)) {
            if (callbacks.onLog) {
                callbacks.onLog(L"No backup folder found to restore.", false);
            }
            return false;
        }

        if (callbacks.onLog) {
            callbacks.onLog(L"Undoing pruning. Restoring files from " + trashRoot.filename().wstring() + L"...", false);
        }

        // Scan trash folder recursively to find files/dirs to restore
        std::vector<SyncItem> trashItems = ScanDirectory(trashRoot.wstring(), FilterOptions{}, callbacks);

        // Sort items by length ascending to restore directories first
        std::sort(trashItems.begin(), trashItems.end(), [](const SyncItem& a, const SyncItem& b) {
            return a.relativePath.length() < b.relativePath.length();
        });

        size_t restoredCount = 0;
        std::error_code ec;

        for (const auto& item : trashItems) {
            std::filesystem::path srcPath = trashRoot / item.relativePath;
            std::filesystem::path destPath = destRoot / item.relativePath;

            if (item.isDirectory) {
                std::filesystem::create_directories(destPath, ec);
            } else {
                std::filesystem::create_directories(destPath.parent_path(), ec);
                std::filesystem::rename(srcPath, destPath, ec);
                if (!ec) {
                    restoredCount++;
                } else {
                    if (callbacks.onLog) {
                        std::wstring errStr(ec.message().begin(), ec.message().end());
                        callbacks.onLog(L"Failed to restore " + item.relativePath + L": " + errStr, true);
                    }
                }
            }
        }

        // Clean up restored backup directory
        std::filesystem::remove_all(trashRoot, ec);
        if (trashRoot.parent_path().filename() == L".chrono_backups") {
            // Leave .chrono_backups root in place for history metadata
        }

        if (callbacks.onLog) {
            callbacks.onLog(L"Restoration complete. " + std::to_wstring(restoredCount) + L" items restored successfully.", false);
        }
        return true;
    }

} // namespace ChronoSync

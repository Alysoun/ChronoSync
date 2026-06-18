#include "SyncEngine.h"
#include "SyncPlan.h"
#include "SyncExecutor.h"
#include "SyncBackup.h"
#include "NetworkShare.h"
#include "SyncPlanAnalysis.h"
#include "SyncHistory.h"
#include <windows.h>
#include <chrono>

#ifndef IO_REPARSE_TAG_SYMLINK
#define IO_REPARSE_TAG_SYMLINK 0xA000000CL
#endif

namespace ChronoSync {

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

            if (name == L".chrono_trash" || name == L".chrono_backups" || name == L".chrono_history") {
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

    SyncStats SyncEngine::Sync(const std::wstring& source, const std::wstring& destination, const SyncOptions& options, const SyncCallbacks& callbacks) {
        auto startTime = std::chrono::high_resolution_clock::now();
        SyncStats stats;

        std::filesystem::path srcRoot(source);
        std::filesystem::path destRoot(destination);

        if (!std::filesystem::exists(srcRoot)) {
            if (callbacks.onLog) {
                callbacks.onLog(L"Source directory does not exist: " + source, true);
            }
            return stats;
        }

        std::wstring networkError;
        if (!NetworkShare::EnsureAccessible(source, networkError) ||
            !NetworkShare::EnsureAccessible(destination, networkError)) {
            if (callbacks.onLog) {
                callbacks.onLog(networkError, true);
            }
            return stats;
        }

        auto scanStart = std::chrono::high_resolution_clock::now();
        std::vector<SyncItem> srcItems = ScanDirectory(source, options.filters, callbacks);
        std::vector<SyncItem> destItems;
        if (std::filesystem::exists(destRoot)) {
            destItems = ScanDirectory(destination, options.filters, callbacks);
        }
        auto scanEnd = std::chrono::high_resolution_clock::now();
        stats.scanTimeMs = std::chrono::duration<double, std::milli>(scanEnd - scanStart).count();

        if (callbacks.onCompareStart) {
            callbacks.onCompareStart();
        }
        auto compareStart = std::chrono::high_resolution_clock::now();
        SyncPlan plan = BuildSyncPlan(srcItems, destItems, srcRoot, destRoot, options);
        stats.filesSkipped = plan.filesSkipped;
        auto compareEnd = std::chrono::high_resolution_clock::now();
        stats.compareTimeMs = std::chrono::duration<double, std::milli>(compareEnd - compareStart).count();

        if (callbacks.onCompareComplete) {
            callbacks.onCompareComplete(plan.dirsToCreate.size(),
                                        plan.filesToCopy.size() + plan.linksToCreate.size(),
                                        plan.itemsToDelete.size());
        }

        auto copyStart = std::chrono::high_resolution_clock::now();
        ExecuteSyncPlan(srcRoot, destRoot, destination, options, plan, callbacks, stats);
        auto copyEnd = std::chrono::high_resolution_clock::now();
        stats.copyTimeMs = std::chrono::duration<double, std::milli>(copyEnd - copyStart).count();

        auto endTime = std::chrono::high_resolution_clock::now();
        stats.totalTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

        std::wstring historyError;
        SyncHistoryIO::RecordRun(source, destination, options, stats, historyError);
        if (!historyError.empty() && callbacks.onLog) {
            callbacks.onLog(L"History: " + historyError, false);
        }

        return stats;
    }

    std::vector<PreviewItem> SyncEngine::Preview(const std::wstring& source, const std::wstring& destination, const SyncOptions& options, const SyncCallbacks& callbacks) {
        return BuildSyncPlanReport(source, destination, options, callbacks).previewItems;
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

        std::vector<SyncItem> trashItems = ScanDirectory(trashRoot.wstring(), FilterOptions{}, callbacks);

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
                } else if (callbacks.onLog) {
                    std::wstring errStr(ec.message().begin(), ec.message().end());
                    callbacks.onLog(L"Failed to restore " + item.relativePath + L": " + errStr, true);
                }
            }
        }

        std::filesystem::remove_all(trashRoot, ec);

        if (callbacks.onLog) {
            callbacks.onLog(L"Restoration complete. " + std::to_wstring(restoredCount) + L" items restored successfully.", false);
        }
        return true;
    }

} // namespace ChronoSync

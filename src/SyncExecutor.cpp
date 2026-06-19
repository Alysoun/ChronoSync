#include "SyncExecutor.h"
#include "SyncBackup.h"
#include "NetworkShare.h"
#include "DeltaCopy.h"
#include "FileHash.h"
#include "WinPath.h"
#include <windows.h>

#ifndef IO_REPARSE_TAG_MOUNT_POINT
#define IO_REPARSE_TAG_MOUNT_POINT 0xA0000003L
#endif

namespace PrevueSync {

    struct CopyContext {
        std::function<void(unsigned long long bytesCopied, unsigned long long fileSize)> progressCallback;
    };

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

    static void ExecutePrunePhase(const std::filesystem::path& destRoot,
                                  const SyncOptions& options,
                                  const std::vector<PlannedDelete>& itemsToDelete,
                                  const SyncCallbacks& callbacks,
                                  SyncStats& stats) {
        std::filesystem::path backupRoot = destRoot / L".prevue_backups";
        std::filesystem::path trashRoot;
        std::error_code ec;
        bool backupFolderCreated = false;
        const bool prune = options.prune;

        for (const auto& plannedDelete : itemsToDelete) {
            const auto& item = plannedDelete.item;
            std::filesystem::path fullDestPath = WinPath::Join(destRoot, item.relativePath);

            if (callbacks.onDeleteItem) {
                callbacks.onDeleteItem(item.relativePath, item.isDirectory);
            }

            if (item.isReparsePoint) {
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
                            trashRoot = destRoot / L".prevue_trash";
                            std::filesystem::remove_all(trashRoot, ec);
                        }
                        std::filesystem::create_directories(trashRoot, ec);
                        backupFolderCreated = true;
                    }

                    std::filesystem::path trashPath = WinPath::Join(trashRoot, item.relativePath);
                    WinPath::CreateParentDirectories(trashRoot, item.relativePath, ec);
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
    }

    static void ExecuteCreateDirsPhase(const std::filesystem::path& destRoot,
                                       const std::vector<SyncItem>& dirsToCreate,
                                       const SyncCallbacks& callbacks,
                                       SyncStats& stats) {
        for (const auto& dir : dirsToCreate) {
            try {
                std::error_code ec;
                if (WinPath::CreateDirectories(destRoot, dir.relativePath, ec)) {
                    stats.dirsCreated++;
                } else if (callbacks.onLog) {
                    std::wstring errStr(ec.message().begin(), ec.message().end());
                    callbacks.onLog(L"Failed to create directory " + dir.relativePath + L": " + errStr, true);
                }
            } catch (const std::exception& ex) {
                if (callbacks.onLog) {
                    std::string err = ex.what() ? ex.what() : "path error";
                    callbacks.onLog(L"Failed to create directory " + dir.relativePath + L": " +
                                    std::wstring(err.begin(), err.end()), true);
                }
            }
        }
    }

    static void ExecuteCopyFilesPhase(const std::filesystem::path& srcRoot,
                                      const std::filesystem::path& destRoot,
                                      const std::wstring& destination,
                                      const SyncOptions& options,
                                      const std::vector<SyncItem>& filesToCopy,
                                      size_t linkCount,
                                      const SyncCallbacks& callbacks,
                                      Sha256Session* verifySession,
                                      SyncStats& stats) {
        std::wstring networkError;

        for (size_t i = 0; i < filesToCopy.size(); ++i) {
            const auto& fileItem = filesToCopy[i];

            try {
            std::filesystem::path srcPath = WinPath::Join(srcRoot, fileItem.relativePath);
            std::filesystem::path destPath = WinPath::Join(destRoot, fileItem.relativePath);
            std::filesystem::path tmpPath = std::filesystem::path(destPath.wstring() + L".prevue_tmp");

            if (callbacks.onCopyStart) {
                callbacks.onCopyStart(fileItem.relativePath, fileItem.fileSize, i + 1, filesToCopy.size() + linkCount);
            }

            std::error_code ec;
            WinPath::CreateParentDirectories(destRoot, fileItem.relativePath, ec);
            std::filesystem::remove(tmpPath, ec);

            bool copySuccess = false;
            std::wstring copyError;
            const bool destExists = std::filesystem::exists(destPath, ec);
            const bool canUseDelta = options.deltaBlockCopy && destExists &&
                                     std::filesystem::file_size(destPath, ec) == fileItem.fileSize;

            unsigned long long bytesCounted = fileItem.fileSize;

            if (canUseDelta) {
                auto deltaResult = DeltaCopy::CopyFileBlocks(
                    srcPath.wstring(),
                    destPath.wstring(),
                    fileItem.fileSize,
                    callbacks.onCopyProgress);
                copySuccess = deltaResult.success;
                copyError = deltaResult.errorMessage;
                if (copySuccess) {
                    bytesCounted = deltaResult.bytesWritten;
                    stats.deltaBytesWritten += deltaResult.bytesWritten;
                }
            } else {
                CopyContext context;
                context.progressCallback = callbacks.onCopyProgress;

                for (int attempt = 0; attempt < 3; ++attempt) {
                    std::filesystem::remove(tmpPath, ec);
                    copySuccess = CopyFileExW(
                        srcPath.c_str(),
                        tmpPath.c_str(),
                        (LPPROGRESS_ROUTINE)CopyProgressCallback,
                        &context,
                        NULL,
                        0
                    ) != FALSE;

                    if (copySuccess) {
                        break;
                    }

                    DWORD err = GetLastError();
                    copyError = L"CopyFileExW failed. Win32 Error: " + std::to_wstring(err);
                    if (!NetworkShare::IsRetryableNetworkError(err) || attempt == 2) {
                        break;
                    }
                    if (NetworkShare::IsUncPath(destination)) {
                        NetworkShare::EnsureAccessible(destination, networkError);
                    }
                    Sleep(500);
                }

                if (copySuccess) {
                    copySuccess = MoveFileExW(tmpPath.c_str(), destPath.c_str(),
                                              MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
                    if (!copySuccess) {
                        DWORD err = GetLastError();
                        copyError = L"Atomic move failed. Win32 Error: " + std::to_wstring(err);
                        std::filesystem::remove(tmpPath, ec);
                    }
                }
            }

            if (copySuccess) {
                HANDLE hFile = CreateFileW(destPath.c_str(), FILE_WRITE_ATTRIBUTES,
                                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                           NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
                if (hFile != INVALID_HANDLE_VALUE) {
                    SetFileTime(hFile, NULL, NULL, &fileItem.lastWriteTime);
                    CloseHandle(hFile);
                }

                bool copyOk = true;
                std::wstring verifyError;
                if (options.verifyAfterCopy) {
                    stats.filesVerified++;
                    if (!VerifyCopiedFile(srcPath, destPath, verifySession, &callbacks)) {
                        copyOk = false;
                        verifyError = L"SHA256 verification failed after copy";
                        stats.verifyFailures++;
                    }
                }

                if (copyOk) {
                    stats.filesCopied++;
                    stats.totalBytesCopied += bytesCounted;
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
            } else if (callbacks.onCopyComplete) {
                callbacks.onCopyComplete(fileItem.relativePath, false, copyError);
            }
            } catch (const std::exception& ex) {
                std::string err = ex.what() ? ex.what() : "path error";
                std::wstring errWide(err.begin(), err.end());
                if (callbacks.onCopyComplete) {
                    callbacks.onCopyComplete(fileItem.relativePath, false, errWide);
                }
                if (callbacks.onLog) {
                    callbacks.onLog(L"Failed to copy " + fileItem.relativePath + L": " + errWide, true);
                }
            }
        }
    }

    static std::wstring StripExtendedPrefix(std::wstring path) {
        if (path.rfind(L"\\\\?\\UNC\\", 0) == 0) {
            return L"\\\\" + path.substr(8);
        }
        if (path.rfind(L"\\\\?\\", 0) == 0) {
            return path.substr(4);
        }
        return path;
    }

    static std::wstring ResolveDestLinkTarget(const std::filesystem::path& srcRoot,
                                              const std::filesystem::path& destRoot,
                                              const std::wstring& linkRelPath,
                                              const std::wstring& reparseTarget) {
        if (reparseTarget.empty()) {
            return {};
        }

        std::error_code ec;
        std::filesystem::path srcLinkParent = WinPath::Join(srcRoot, linkRelPath).parent_path();
        std::filesystem::path target(reparseTarget);
        if (!target.is_absolute()) {
            target = (srcLinkParent / target).lexically_normal();
        }

        std::filesystem::path resolved = std::filesystem::weakly_canonical(target, ec);
        if (ec) {
            resolved = target.lexically_normal();
        }

        std::wstring srcBase = StripExtendedPrefix(srcRoot.wstring());
        std::wstring destBase = StripExtendedPrefix(destRoot.wstring());
        std::wstring resolvedW = resolved.wstring();

        if (resolvedW.size() >= srcBase.size() &&
            _wcsnicmp(resolvedW.c_str(), srcBase.c_str(), static_cast<unsigned>(srcBase.size())) == 0 &&
            (resolvedW.size() == srcBase.size() || resolvedW[srcBase.size()] == L'\\')) {
            return destBase + resolvedW.substr(srcBase.size());
        }

        return resolvedW;
    }

    static void ExecuteCreateLinksPhase(const std::filesystem::path& srcRoot,
                                        const std::filesystem::path& destRoot,
                                        const std::vector<SyncItem>& linksToCreate,
                                        size_t fileCount,
                                        const SyncCallbacks& callbacks,
                                        SyncStats& stats) {
        std::error_code ec;

        for (size_t i = 0; i < linksToCreate.size(); ++i) {
            const auto& linkItem = linksToCreate[i];
            try {
            std::filesystem::path destPath = WinPath::Join(destRoot, linkItem.relativePath);
            std::wstring destTarget = ResolveDestLinkTarget(srcRoot, destRoot, linkItem.relativePath, linkItem.reparseTarget);
            if (destTarget.empty()) {
                if (callbacks.onCopyComplete) {
                    callbacks.onCopyComplete(linkItem.relativePath, false, L"Empty or invalid link target");
                }
                if (callbacks.onLog) {
                    callbacks.onLog(L"Skipped link with empty target: " + linkItem.relativePath, true);
                }
                continue;
            }

            if (callbacks.onCopyStart) {
                callbacks.onCopyStart(linkItem.relativePath, 0, fileCount + i + 1, fileCount + linksToCreate.size());
            }

            WinPath::CreateParentDirectories(destRoot, linkItem.relativePath, ec);

            bool success = false;
            std::wstring linkTypeStr;
            std::wstring errStr;

            if (linkItem.reparseTag == IO_REPARSE_TAG_MOUNT_POINT) {
                linkTypeStr = L"junction";
                std::wstring cmdArgs = L"cmd.exe /c mklink /j \"" + destPath.wstring() + L"\" \"" + destTarget + L"\"";
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
                    std::filesystem::create_directory_symlink(destTarget, destPath, ec);
                } else {
                    std::filesystem::create_symlink(destTarget, destPath, ec);
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
                    callbacks.onLog(L"Created " + linkTypeStr + L": " + linkItem.relativePath + L" -> " + destTarget, false);
                }
            } else {
                if (callbacks.onCopyComplete) {
                    callbacks.onCopyComplete(linkItem.relativePath, false, errStr);
                }
                if (callbacks.onLog) {
                    callbacks.onLog(L"Failed to create " + linkTypeStr + L": " + linkItem.relativePath + L" -> " + destTarget + L". Error: " + errStr, true);
                }
            }
            } catch (const std::exception& ex) {
                std::string err = ex.what() ? ex.what() : "link error";
                std::wstring errWide(err.begin(), err.end());
                if (callbacks.onCopyComplete) {
                    callbacks.onCopyComplete(linkItem.relativePath, false, errWide);
                }
                if (callbacks.onLog) {
                    callbacks.onLog(L"Failed to create link " + linkItem.relativePath + L": " + errWide, true);
                }
            } catch (...) {
                if (callbacks.onCopyComplete) {
                    callbacks.onCopyComplete(linkItem.relativePath, false, L"Unexpected link error");
                }
                if (callbacks.onLog) {
                    callbacks.onLog(L"Failed to create link " + linkItem.relativePath + L": unexpected error", true);
                }
            }
        }
    }

    void ExecuteSyncPlan(const std::filesystem::path& srcRoot,
                         const std::filesystem::path& destRoot,
                         const std::wstring& destination,
                         const SyncOptions& options,
                         const SyncPlan& plan,
                         const SyncCallbacks& callbacks,
                         SyncStats& stats) {
        Sha256Session verifySession;
        Sha256Session* verifyPtr = (options.verifyAfterCopy && verifySession.IsValid()) ? &verifySession : nullptr;

        ExecutePrunePhase(destRoot, options, plan.itemsToDelete, callbacks, stats);
        ExecuteCreateDirsPhase(destRoot, plan.dirsToCreate, callbacks, stats);
        ExecuteCopyFilesPhase(srcRoot, destRoot, destination, options, plan.filesToCopy, plan.linksToCreate.size(), callbacks, verifyPtr, stats);
        ExecuteCreateLinksPhase(srcRoot, destRoot, plan.linksToCreate, plan.filesToCopy.size(), callbacks, stats);
    }

} // namespace PrevueSync

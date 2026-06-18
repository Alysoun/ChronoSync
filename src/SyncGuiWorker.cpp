#include "SyncGuiWorker.h"
#include "SyncPlanAnalysis.h"

#include <exception>

namespace {

constexpr size_t kVerboseCopyLogLimit = 200;
constexpr size_t kCopyMilestoneInterval = 500;

static void AttachHashProgress(ChronoSync::SyncCallbacks& callbacks) {
    callbacks.onHashProgress = [](const std::wstring& relPath, unsigned long long bytesHashed,
                                unsigned long long fileSize, bool hashingSource) {
        int pct = fileSize > 0 ? static_cast<int>((bytesHashed * 100) / fileSize) : 0;
        if (pct > 100) {
            pct = 100;
        }
        const wchar_t* side = hashingSource ? L"source" : L"destination";
        g_MsgRegistry.SetStatus(L"SHA256 (" + std::wstring(side) + L"): " +
                                TruncateForStatus(relPath) + L" \u2014 " + std::to_wstring(pct) + L"%");
    };
}

static void AttachPlanCallbacks(ChronoSync::SyncCallbacks& callbacks, ChronoSync::SyncOptions options,
                                const wchar_t* compareStatus, const wchar_t* defaultCompareStatus) {
    callbacks.onScanStart = [](const std::wstring& rootDir) {
        g_MsgRegistry.SetStatus(L"Scanning: " + TruncateForStatus(rootDir, 200));
    };
    callbacks.onScanComplete = [](size_t totalItems) {
        g_MsgRegistry.PushLog(L"Scan complete. Found " + std::to_wstring(totalItems) + L" items.");
    };
    callbacks.onCompareStart = [options, compareStatus, defaultCompareStatus]() {
        if (options.compareMode == ChronoSync::CompareMode::Sha256) {
            g_MsgRegistry.SetStatus(compareStatus);
        } else {
            g_MsgRegistry.SetStatus(defaultCompareStatus);
        }
    };
    callbacks.onCompareComplete = [](size_t dirsToCreate, size_t filesToCopy, size_t itemsToDelete) {
        std::wstring plan = L"Plan: Create " + std::to_wstring(dirsToCreate) + L" dirs, Copy " +
                            std::to_wstring(filesToCopy) + L" files, Remove/Delete " +
                            std::to_wstring(itemsToDelete) + L" items.";
        g_MsgRegistry.PushLog(plan);
    };
    callbacks.onLog = [](const std::wstring& message, bool isError) {
        std::wstring prefix = isError ? L"[ERROR] " : L"[INFO] ";
        g_MsgRegistry.PushLog(prefix + message);
    };
}

static void AttachCopyCallbacks(ChronoSync::SyncCallbacks& callbacks) {
    callbacks.onCopyStart = [](const std::wstring& relPath, unsigned long long fileSizeBytes,
                               size_t fileIndex, size_t totalFiles) {
        std::wstring shortPath = TruncateForStatus(relPath);
        g_MsgRegistry.SetStatus(L"[" + std::to_wstring(fileIndex) + L"/" + std::to_wstring(totalFiles) +
                                L"] Syncing: " + shortPath);
        if (totalFiles <= kVerboseCopyLogLimit) {
            g_MsgRegistry.PushLog(L"Syncing: " + relPath + L" (" + std::to_wstring(fileSizeBytes / 1024) + L" KB)");
        } else if (fileIndex == 1 || fileIndex % kCopyMilestoneInterval == 0 || fileIndex == totalFiles) {
            g_MsgRegistry.PushLog(L"Syncing [" + std::to_wstring(fileIndex) + L"/" + std::to_wstring(totalFiles) +
                                  L"]: " + shortPath);
        }
    };

    callbacks.onCopyProgress = [](unsigned long long bytesCopied, unsigned long long fileSizeBytes) {
        if (fileSizeBytes > 0) {
            int pct = static_cast<int>(bytesCopied * 100 / fileSizeBytes);
            g_MsgRegistry.SetProgress(pct);
        }
    };

    callbacks.onCopyComplete = [](const std::wstring& relPath, bool success, const std::wstring& errorMessage) {
        if (!success) {
            g_MsgRegistry.PushLog(L"  \u2718 Failed: " + relPath + L" - " + errorMessage);
        }
        g_MsgRegistry.SetProgress(0);
    };

    callbacks.onDeleteItem = [](const std::wstring& relPath, bool isDirectory) {
        std::wstring typeStr = isDirectory ? L"directory" : L"file";
        g_MsgRegistry.PushLog(L"[PRUNE] Archiving " + typeStr + L": " + TruncateForStatus(relPath, 200));
    };

    callbacks.onDeleteFailed = [](const std::wstring& relPath, const std::wstring& errorMessage) {
        g_MsgRegistry.PushLog(L"[PRUNE] Archive failed: " + TruncateForStatus(relPath, 200) +
                              L" (" + errorMessage + L")");
    };
}

} // namespace

void SetControlsState(BOOL enabled) {
    EnableWindow(g_hWndSrcBrowse, enabled);
    EnableWindow(g_hWndDestBrowse, enabled);
    EnableWindow(g_hWndPruneCheck, enabled);
    EnableWindow(GetDlgItem(g_hWndMain, ID_PRUNE_LABEL), enabled);
    EnableWindow(g_hWndExcludeEdit, enabled);
    EnableWindow(g_hWndIncludeEdit, enabled);
    EnableWindow(g_hWndSaveProfileBtn, enabled);
    EnableWindow(g_hWndLoadProfileBtn, enabled);
    EnableWindow(g_hWndSha256Check, enabled);
    EnableWindow(g_hWndVerifyCheck, enabled);
    EnableWindow(g_hWndVersionedBackupCheck, enabled);
    EnableWindow(g_hWndDeltaCopyCheck, enabled);
    EnableWindow(g_hWndScheduleBtn, enabled);
    EnableWindow(g_hWndHistoryBtn, enabled);
    EnableWindow(g_hWndAddQueueBtn, enabled);
    EnableWindow(g_hWndRunQueueBtn, enabled);
    EnableWindow(g_hWndClearQueueBtn, enabled);
    EnableWindow(g_hWndSaveQueueBtn, enabled);
    EnableWindow(g_hWndLoadQueueBtn, enabled);
    EnableWindow(g_hWndPreviewBtn, enabled);
    EnableWindow(g_hWndAnalyzeBtn, enabled);
    EnableWindow(g_hWndSyncBtn, enabled);
    if (!enabled) {
        EnableWindow(g_hWndUndoBtn, FALSE);
    }
}

void PreviewThreadProc(std::wstring src, std::wstring dest, ChronoSync::SyncOptions options) {
    ChronoSync::SyncCallbacks callbacks;
    AttachHashProgress(callbacks);
    AttachPlanCallbacks(callbacks, options,
                        L"SHA256 compare: hashing files (updates every 8 MB per file)...",
                        L"Comparing structures for preview...");

    auto report = ChronoSync::BuildSyncPlanReport(src, dest, options, callbacks);

    auto* launchBundle = new PreviewLaunchData();
    launchBundle->pList = new std::vector<ChronoSync::PreviewItem>(std::move(report.previewItems));
    launchBundle->analysis = std::move(report.analysis);
    launchBundle->hasAnalysis = true;
    launchBundle->sourceRoot = src;
    launchBundle->destRoot = dest;
    PostMessageW(g_hWndMain, WM_SYNC_PREVIEW_COMPLETE, 0, (LPARAM)launchBundle);
}

void AnalyzeThreadProc(std::wstring src, std::wstring dest, ChronoSync::SyncOptions options) {
    ChronoSync::SyncCallbacks callbacks;
    AttachHashProgress(callbacks);
    AttachPlanCallbacks(callbacks, options,
                        L"SHA256 compare: hashing files (updates every 8 MB per file)...",
                        L"Analyzing planned sync impact...");

    auto report = ChronoSync::BuildSyncPlanReport(src, dest, options, callbacks);
    auto* data = new AnalyzeCompleteData();
    data->analysis = std::move(report.analysis);
    data->hasAnalysis = true;
    data->report = ChronoSync::FormatSyncPlanReport(data->analysis, src, dest);
    PostMessageW(g_hWndMain, WM_SYNC_ANALYZE_COMPLETE, 0, (LPARAM)data);
}

void SyncThreadProc(std::wstring src, std::wstring dest, ChronoSync::SyncOptions options) {
    ChronoSync::SyncCallbacks callbacks;
    AttachHashProgress(callbacks);
    AttachPlanCallbacks(callbacks, options,
                        L"SHA256 compare: hashing files (updates every 8 MB per file)...",
                        L"Comparing directory structures...");
    AttachCopyCallbacks(callbacks);

    callbacks.onScanDir = [](const std::wstring& subDir) {
        (void)subDir;
    };

    auto* pStats = new ChronoSync::SyncStats();
    try {
        *pStats = ChronoSync::SyncEngine::Sync(src, dest, options, callbacks);
    } catch (const std::exception& ex) {
        std::string err = ex.what() ? ex.what() : "unknown error";
        g_MsgRegistry.PushLog(L"[ERROR] Sync failed: " + std::wstring(err.begin(), err.end()));
    } catch (...) {
        g_MsgRegistry.PushLog(L"[ERROR] Sync failed with an unexpected error.");
    }

    PostMessageW(g_hWndMain, WM_SYNC_COMPLETE, 1, (LPARAM)pStats);
}

void QueueThreadProc(std::vector<ChronoSync::SyncJob> jobs) {
    ChronoSync::SyncCallbacks callbacks;
    AttachHashProgress(callbacks);
    AttachPlanCallbacks(callbacks, ChronoSync::SyncOptions{},
                        L"SHA256 compare: hashing files (updates every 8 MB per file)...",
                        L"Comparing directory structures...");
    AttachCopyCallbacks(callbacks);

    size_t completedJobs = 0;
    for (size_t i = 0; i < jobs.size(); ++i) {
        g_MsgRegistry.PushLog(L"=== Queue job " + std::to_wstring(i + 1) + L"/" + std::to_wstring(jobs.size()) +
                              L": " + jobs[i].name + L" ===");
        ChronoSync::SyncStats stats = ChronoSync::SyncEngine::Sync(jobs[i].source, jobs[i].destination, jobs[i].options, callbacks);
        if (stats.filesCopied > 0 || stats.itemsDeleted > 0 || stats.dirsCreated > 0) {
            completedJobs++;
        }
    }

    size_t* pCompleted = new size_t(completedJobs);
    PostMessageW(g_hWndMain, WM_SYNC_QUEUE_COMPLETE, static_cast<WPARAM>(jobs.size()), (LPARAM)pCompleted);
}

void UndoThreadProc(std::wstring dest) {
    ChronoSync::SyncCallbacks callbacks;
    callbacks.onLog = [](const std::wstring& message, bool isError) {
        std::wstring prefix = isError ? L"[ERROR] " : L"[INFO] ";
        g_MsgRegistry.PushLog(prefix + message);
    };
    callbacks.onScanStart = [](const std::wstring& rootDir) {
        g_MsgRegistry.SetStatus(L"Scanning trash: " + TruncateForStatus(rootDir, 200));
    };

    ChronoSync::SyncEngine::UndoPruning(dest, callbacks);
    PostMessageW(g_hWndMain, WM_SYNC_UNDO_COMPLETE, 0, 0);
}

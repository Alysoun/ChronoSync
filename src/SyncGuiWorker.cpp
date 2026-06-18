#include "SyncGuiWorker.h"
#include "SyncPlanAnalysis.h"

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

    callbacks.onScanStart = [](const std::wstring& rootDir) {
        g_MsgRegistry.SetStatus(L"Scanning: " + rootDir);
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };
    callbacks.onScanComplete = [](size_t totalItems) {
        g_MsgRegistry.PushLog(L"Scan complete. Found " + std::to_wstring(totalItems) + L" items.");
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };
    callbacks.onCompareStart = []() {
        g_MsgRegistry.SetStatus(L"Comparing structures for preview...");
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };
    callbacks.onCompareComplete = [](size_t dirsToCreate, size_t filesToCopy, size_t itemsToDelete) {
        std::wstring plan = L"Plan: Create " + std::to_wstring(dirsToCreate) + L" dirs, Copy " + std::to_wstring(filesToCopy)
                        + L" files, Remove/Delete " + std::to_wstring(itemsToDelete) + L" items.";
        g_MsgRegistry.PushLog(plan);
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };
    callbacks.onLog = [](const std::wstring& message, bool isError) {
        std::wstring prefix = isError ? L"[ERROR] " : L"[INFO] ";
        g_MsgRegistry.PushLog(prefix + message);
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };

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

    callbacks.onScanStart = [](const std::wstring& rootDir) {
        g_MsgRegistry.SetStatus(L"Scanning: " + rootDir);
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };
    callbacks.onScanComplete = [](size_t totalItems) {
        g_MsgRegistry.PushLog(L"Scan complete. Found " + std::to_wstring(totalItems) + L" items.");
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };
    callbacks.onCompareStart = []() {
        g_MsgRegistry.SetStatus(L"Analyzing planned sync impact...");
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };
    callbacks.onCompareComplete = [](size_t dirsToCreate, size_t filesToCopy, size_t itemsToDelete) {
        std::wstring plan = L"Plan: Create " + std::to_wstring(dirsToCreate) + L" dirs, Copy " + std::to_wstring(filesToCopy)
                        + L" files, Remove/Delete " + std::to_wstring(itemsToDelete) + L" items.";
        g_MsgRegistry.PushLog(plan);
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };
    callbacks.onLog = [](const std::wstring& message, bool isError) {
        std::wstring prefix = isError ? L"[ERROR] " : L"[INFO] ";
        g_MsgRegistry.PushLog(prefix + message);
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };

    auto report = ChronoSync::BuildSyncPlanReport(src, dest, options, callbacks);
    auto* data = new AnalyzeCompleteData();
    data->analysis = std::move(report.analysis);
    data->hasAnalysis = true;
    data->report = ChronoSync::FormatSyncPlanReport(data->analysis, src, dest);
    PostMessageW(g_hWndMain, WM_SYNC_ANALYZE_COMPLETE, 0, (LPARAM)data);
}

void SyncThreadProc(std::wstring src, std::wstring dest, ChronoSync::SyncOptions options) {
    ChronoSync::SyncCallbacks callbacks;

    callbacks.onScanStart = [](const std::wstring& rootDir) {
        g_MsgRegistry.SetStatus(L"Scanning: " + rootDir);
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };

    callbacks.onScanDir = [](const std::wstring& subDir) {
        (void)subDir;
    };

    callbacks.onScanComplete = [](size_t totalItems) {
        g_MsgRegistry.PushLog(L"Scan complete. Found " + std::to_wstring(totalItems) + L" items.");
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };

    callbacks.onCompareStart = []() {
        g_MsgRegistry.SetStatus(L"Comparing directory structures...");
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };

    callbacks.onCompareComplete = [](size_t dirsToCreate, size_t filesToCopy, size_t itemsToDelete) {
        std::wstring plan = L"Plan: Create " + std::to_wstring(dirsToCreate) + L" dirs, Copy " + std::to_wstring(filesToCopy)
                        + L" files, Remove/Delete " + std::to_wstring(itemsToDelete) + L" items.";
        g_MsgRegistry.PushLog(plan);
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };

    callbacks.onCopyStart = [](const std::wstring& relPath, unsigned long long fileSizeBytes, size_t fileIndex, size_t totalFiles) {
        g_MsgRegistry.SetStatus(L"[" + std::to_wstring(fileIndex) + L"/" + std::to_wstring(totalFiles) + L"] Syncing: " + relPath);
        g_MsgRegistry.PushLog(L"Syncing: " + relPath + L" (" + std::to_wstring(fileSizeBytes / 1024) + L" KB)");
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };

    callbacks.onCopyProgress = [](unsigned long long bytesCopied, unsigned long long fileSizeBytes) {
        if (fileSizeBytes > 0) {
            int pct = static_cast<int>(bytesCopied * 100 / fileSizeBytes);
            g_MsgRegistry.SetProgress(pct);
            PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
        }
    };

    callbacks.onCopyComplete = [](const std::wstring& relPath, bool success, const std::wstring& errorMessage) {
        if (success) {
            g_MsgRegistry.PushLog(L"  ✔ Success: " + relPath);
        } else {
            g_MsgRegistry.PushLog(L"  ✘ Failed: " + relPath + L" - " + errorMessage);
        }
        g_MsgRegistry.SetProgress(0);
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };

    callbacks.onDeleteItem = [](const std::wstring& relPath, bool isDirectory) {
        std::wstring typeStr = isDirectory ? L"directory" : L"file";
        g_MsgRegistry.PushLog(L"[PRUNE] Archiving " + typeStr + L" to trash: " + relPath);
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };

    callbacks.onDeleteFailed = [](const std::wstring& relPath, const std::wstring& errorMessage) {
        g_MsgRegistry.PushLog(L"[PRUNE] Archive failed: " + relPath + L" (" + errorMessage + L")");
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };

    callbacks.onLog = [](const std::wstring& message, bool isError) {
        std::wstring prefix = isError ? L"[ERROR] " : L"[INFO] ";
        g_MsgRegistry.PushLog(prefix + message);
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };

    ChronoSync::SyncStats* pStats = new ChronoSync::SyncStats();
    *pStats = ChronoSync::SyncEngine::Sync(src, dest, options, callbacks);

    PostMessageW(g_hWndMain, WM_SYNC_COMPLETE, 1, (LPARAM)pStats);
}

void QueueThreadProc(std::vector<ChronoSync::SyncJob> jobs) {
    ChronoSync::SyncCallbacks callbacks;
    callbacks.onScanStart = [](const std::wstring& rootDir) {
        g_MsgRegistry.SetStatus(L"Scanning: " + rootDir);
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };
    callbacks.onScanComplete = [](size_t totalItems) {
        g_MsgRegistry.PushLog(L"Scan complete. Found " + std::to_wstring(totalItems) + L" items.");
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };
    callbacks.onCompareStart = []() {
        g_MsgRegistry.SetStatus(L"Comparing directory structures...");
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };
    callbacks.onCompareComplete = [](size_t dirsToCreate, size_t filesToCopy, size_t itemsToDelete) {
        std::wstring plan = L"Plan: Create " + std::to_wstring(dirsToCreate) + L" dirs, Copy " +
                            std::to_wstring(filesToCopy) + L" files, Remove/Delete " + std::to_wstring(itemsToDelete) + L" items.";
        g_MsgRegistry.PushLog(plan);
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };
    callbacks.onCopyStart = [](const std::wstring& relPath, unsigned long long fileSizeBytes, size_t fileIndex, size_t totalFiles) {
        g_MsgRegistry.SetStatus(L"[" + std::to_wstring(fileIndex) + L"/" + std::to_wstring(totalFiles) + L"] Syncing: " + relPath);
        g_MsgRegistry.PushLog(L"Syncing: " + relPath + L" (" + std::to_wstring(fileSizeBytes / 1024) + L" KB)");
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };
    callbacks.onCopyProgress = [](unsigned long long bytesCopied, unsigned long long fileSizeBytes) {
        if (fileSizeBytes > 0) {
            int pct = static_cast<int>(bytesCopied * 100 / fileSizeBytes);
            g_MsgRegistry.SetProgress(pct);
            PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
        }
    };
    callbacks.onCopyComplete = [](const std::wstring& relPath, bool success, const std::wstring& errorMessage) {
        if (success) {
            g_MsgRegistry.PushLog(L"  ✔ Success: " + relPath);
        } else {
            g_MsgRegistry.PushLog(L"  ✘ Failed: " + relPath + L" - " + errorMessage);
        }
        g_MsgRegistry.SetProgress(0);
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };
    callbacks.onDeleteItem = [](const std::wstring& relPath, bool isDirectory) {
        std::wstring typeStr = isDirectory ? L"directory" : L"file";
        g_MsgRegistry.PushLog(L"[PRUNE] Archiving " + typeStr + L": " + relPath);
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };
    callbacks.onDeleteFailed = [](const std::wstring& relPath, const std::wstring& errorMessage) {
        g_MsgRegistry.PushLog(L"[PRUNE] Archive failed: " + relPath + L" (" + errorMessage + L")");
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };
    callbacks.onLog = [](const std::wstring& message, bool isError) {
        std::wstring prefix = isError ? L"[ERROR] " : L"[INFO] ";
        g_MsgRegistry.PushLog(prefix + message);
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };

    size_t completedJobs = 0;
    for (size_t i = 0; i < jobs.size(); ++i) {
        g_MsgRegistry.PushLog(L"=== Queue job " + std::to_wstring(i + 1) + L"/" + std::to_wstring(jobs.size()) +
                              L": " + jobs[i].name + L" ===");
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
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
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };
    callbacks.onScanStart = [](const std::wstring& rootDir) {
        g_MsgRegistry.SetStatus(L"Scanning trash: " + rootDir);
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };

    ChronoSync::SyncEngine::UndoPruning(dest, callbacks);
    PostMessageW(g_hWndMain, WM_SYNC_UNDO_COMPLETE, 0, 0);
}

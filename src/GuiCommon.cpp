#include "GuiCommon.h"

HWND g_hWndMain = NULL;
HWND g_hWndSrcEdit = NULL;
HWND g_hWndSrcBrowse = NULL;
HWND g_hWndDestEdit = NULL;
HWND g_hWndDestBrowse = NULL;
HWND g_hWndPruneCheck = NULL;
HWND g_hWndUndoBtn = NULL;
HWND g_hWndPreviewBtn = NULL;
HWND g_hWndAnalyzeBtn = NULL;
HWND g_hWndSyncBtn = NULL;
HWND g_hWndProgressBar = NULL;
HWND g_hWndStatusLabel = NULL;
HWND g_hWndLogEdit = NULL;
HWND g_hWndExcludeEdit = NULL;
HWND g_hWndIncludeEdit = NULL;
HWND g_hWndSaveProfileBtn = NULL;
HWND g_hWndLoadProfileBtn = NULL;
HWND g_hWndSha256Check = NULL;
HWND g_hWndVerifyCheck = NULL;
HWND g_hWndVersionedBackupCheck = NULL;
HWND g_hWndQueueList = NULL;
HWND g_hWndAddQueueBtn = NULL;
HWND g_hWndRunQueueBtn = NULL;
HWND g_hWndClearQueueBtn = NULL;
HWND g_hWndSaveQueueBtn = NULL;
HWND g_hWndLoadQueueBtn = NULL;
HWND g_hWndDeltaCopyCheck = NULL;
HWND g_hWndScheduleBtn = NULL;
HWND g_hWndHistoryBtn = NULL;
HWND g_hWndAdvancedGroup = NULL;
HWND g_hWndRiskLabel = NULL;

ChronoSync::SyncPlanAnalysis g_CachedPlanAnalysis;
bool g_HasCachedPlanAnalysis = false;

std::vector<ChronoSync::SyncJob> g_SyncJobQueue;

HBRUSH g_hbrBackground = NULL;
HBRUSH g_hbrEditBackground = NULL;
HBRUSH g_hbrInputBackground = NULL;
HBRUSH g_hbrLogBackground = NULL;

HFONT g_hFontNormal = NULL;
HFONT g_hFontLabel = NULL;
HFONT g_hFontLog = NULL;

bool g_SyncRunning = false;
SyncMessageRegistry g_MsgRegistry;

void SyncMessageRegistry::PushLog(const std::wstring& line) {
    std::lock_guard<std::mutex> lock(mtx);
    logs.push_back(line);
}

void SyncMessageRegistry::SetStatus(const std::wstring& stat) {
    std::lock_guard<std::mutex> lock(mtx);
    status = stat;
    statusChanged = true;
}

void SyncMessageRegistry::SetProgress(int pct) {
    std::lock_guard<std::mutex> lock(mtx);
    progressPct = pct;
    progressChanged = true;
}

bool SyncMessageRegistry::Drain(std::vector<std::wstring>& outLogs, std::wstring& outStatus, int& outProgress,
                                bool& outStatusChanged, bool& outProgressChanged) {
    std::lock_guard<std::mutex> lock(mtx);

    outStatusChanged = statusChanged;
    outProgressChanged = progressChanged;

    if (logs.empty() && !statusChanged && !progressChanged) {
        return false;
    }

    outLogs = std::move(logs);
    logs.clear();

    if (statusChanged) {
        outStatus = status;
        statusChanged = false;
    }
    if (progressChanged) {
        outProgress = progressPct;
        progressChanged = false;
    }
    return true;
}

bool IsEditControl(HWND hwndCtrl) {
    wchar_t className[16] = {};
    GetClassNameW(hwndCtrl, className, 16);
    return wcscmp(className, L"Edit") == 0;
}

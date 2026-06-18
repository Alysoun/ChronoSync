#include "GuiCommon.h"
#include "resource.h"

#include <cstring>

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
HWND g_hWndLogSplitter = NULL;
HWND g_hWndCopyLogBtn = NULL;

bool g_logFollowTail = true;
int g_logHeightBias = 80;

WNDPROC g_logEditOrigProc = nullptr;

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
static std::atomic<bool> g_syncUiEventQueued{false};

void RequestSyncUiEvent() {
    if (!g_hWndMain) {
        return;
    }
    bool expected = false;
    if (g_syncUiEventQueued.compare_exchange_strong(expected, true)) {
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    }
}

void BeginSyncUiDrain() {
    g_syncUiEventQueued = false;
}

std::wstring TruncateForStatus(const std::wstring& text, size_t maxChars) {
    if (text.size() <= maxChars) {
        return text;
    }
    return L"..." + text.substr(text.size() - maxChars + 3);
}

void SyncMessageRegistry::PushLog(const std::wstring& line) {
    std::lock_guard<std::mutex> lock(mtx);
    if (logs.size() >= kMaxPendingLogs) {
        if (!logOverflowNotified) {
            logs.push_back(L"[INFO] Log output truncated during large sync; status line shows current progress.");
            logOverflowNotified = true;
        }
        return;
    }
    logs.push_back(line);
    RequestSyncUiEvent();
}

void SyncMessageRegistry::SetStatus(const std::wstring& stat) {
    std::lock_guard<std::mutex> lock(mtx);
    status = stat;
    statusChanged = true;
    RequestSyncUiEvent();
}

void SyncMessageRegistry::SetProgress(int pct) {
    std::lock_guard<std::mutex> lock(mtx);
    progressPct = pct;
    progressChanged = true;
    RequestSyncUiEvent();
}

void SyncMessageRegistry::ResetForNewRun() {
    std::lock_guard<std::mutex> lock(mtx);
    logs.clear();
    status.clear();
    progressPct = 0;
    progressChanged = false;
    statusChanged = false;
    logOverflowNotified = false;
}

bool SyncMessageRegistry::HasPending() {
    std::lock_guard<std::mutex> lock(mtx);
    return !logs.empty() || statusChanged || progressChanged;
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

void ApplyAppWindowIcons(WNDCLASSEXW& wc, HINSTANCE hInstance) {
    HICON hIcon = reinterpret_cast<HICON>(LoadImageW(
        hInstance, MAKEINTRESOURCEW(IDI_CHRONOSYNC), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
    HICON hIconSm = reinterpret_cast<HICON>(LoadImageW(
        hInstance, MAKEINTRESOURCEW(IDI_CHRONOSYNC), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    wc.hIcon = hIcon ? hIcon : LoadIconW(NULL, MAKEINTRESOURCEW(32512));
    wc.hIconSm = hIconSm ? hIconSm : wc.hIcon;
}

static LRESULT CALLBACK ReadOnlyEditCopySubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

bool HandleReadOnlyEditAccelerator(HWND hwndEdit, WPARAM keyCode) {
    if (!hwndEdit || (GetKeyState(VK_CONTROL) & 0x8000) == 0) {
        return false;
    }

    switch (keyCode) {
        case 'A':
        case 'a':
            SendMessageW(hwndEdit, EM_SETSEL, 0, -1);
            return true;
        case 'C':
        case 'c':
            CopyEditContentToClipboard(hwndEdit);
            return true;
        default:
            return false;
    }
}

bool CopyWideTextToClipboard(const std::wstring& text) {
    if (text.empty()) {
        return false;
    }

    if (!OpenClipboard(NULL)) {
        return false;
    }
    EmptyClipboard();

    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!hMem) {
        CloseClipboard();
        return false;
    }

    void* locked = GlobalLock(hMem);
    if (!locked) {
        GlobalFree(hMem);
        CloseClipboard();
        return false;
    }
    memcpy(locked, text.c_str(), bytes);
    GlobalUnlock(hMem);

    if (!SetClipboardData(CF_UNICODETEXT, hMem)) {
        GlobalFree(hMem);
        CloseClipboard();
        return false;
    }

    CloseClipboard();
    return true;
}

bool CopyEditContentToClipboard(HWND hwndEdit) {
    if (!hwndEdit) {
        return false;
    }

    DWORD selStart = 0;
    DWORD selEnd = 0;
    SendMessageW(hwndEdit, EM_GETSEL, reinterpret_cast<WPARAM>(&selStart), reinterpret_cast<LPARAM>(&selEnd));
    if (selStart != selEnd) {
        SendMessageW(hwndEdit, WM_COPY, 0, 0);
        return true;
    }

    const int len = GetWindowTextLengthW(hwndEdit);
    if (len <= 0) {
        return false;
    }

    std::wstring text(static_cast<size_t>(len), L'\0');
    GetWindowTextW(hwndEdit, text.data(), len + 1);
    return CopyWideTextToClipboard(text);
}

static LRESULT CALLBACK ReadOnlyEditCopySubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    WNDPROC orig = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(orig));
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    }
    if (message == WM_KEYDOWN && HandleReadOnlyEditAccelerator(hwnd, wParam)) {
        return 0;
    }
    return CallWindowProcW(orig, hwnd, message, wParam, lParam);
}

void AttachReadOnlyEditCopySupport(HWND hwndEdit) {
    if (!hwndEdit) {
        return;
    }
    WNDPROC orig = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwndEdit, GWLP_WNDPROC));
    SetWindowLongPtrW(hwndEdit, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(orig));
    SetWindowLongPtrW(hwndEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ReadOnlyEditCopySubclassProc));
}

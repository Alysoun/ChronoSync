#pragma once

#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <mutex>
#include "SyncPlanAnalysis.h"
#include "SyncJob.h"
#include "SyncEngine.h"

namespace UiTheme {
    constexpr COLORREF WindowBg = RGB(48, 48, 54);
    constexpr COLORREF LabelText = RGB(225, 225, 230);
    constexpr COLORREF MutedText = RGB(175, 175, 185);
    constexpr COLORREF InputBg = RGB(250, 250, 252);
    constexpr COLORREF InputText = RGB(24, 24, 28);
    constexpr COLORREF LogBg = RGB(244, 244, 248);
    constexpr COLORREF LogText = RGB(28, 28, 34);
    constexpr int Margin = 20;
    constexpr int ContentWidth = 600;
    constexpr int EditHeight = 28;
    constexpr int ButtonHeight = 30;
    constexpr int RowGap = 10;
    constexpr int SectionGap = 14;
}

enum ControlIds {
    ID_SRC_EDIT = 101,
    ID_SRC_BROWSE,
    ID_DEST_EDIT,
    ID_DEST_BROWSE,
    ID_PRUNE_CHECKBOX,
    ID_PRUNE_LABEL,
    ID_PREVIEW_BUTTON,
    ID_ANALYZE_BUTTON,
    ID_SYNC_BUTTON,
    ID_PROGRESS_BAR,
    ID_STATUS_LABEL,
    ID_LOG_EDIT,
    ID_UNDO_BUTTON,
    ID_EXPORT_CSV_BUTTON,
    ID_EXCLUDE_FILTER_EDIT,
    ID_INCLUDE_FILTER_EDIT,
    ID_SAVE_PROFILE_BUTTON,
    ID_LOAD_PROFILE_BUTTON,
    ID_PREVIEW_FILTER_EDIT = 203,
    ID_PREVIEW_LOCATE_EXPLORER,
    ID_SHA256_CHECKBOX,
    ID_VERIFY_COPY_CHECKBOX,
    ID_VERSIONED_BACKUP_CHECKBOX,
    ID_ADD_QUEUE_BUTTON,
    ID_RUN_QUEUE_BUTTON,
    ID_CLEAR_QUEUE_BUTTON,
    ID_SAVE_QUEUE_BUTTON,
    ID_LOAD_QUEUE_BUTTON,
    ID_QUEUE_LISTBOX,
    ID_DELTA_COPY_CHECKBOX,
    ID_SCHEDULE_BUTTON,
    ID_HISTORY_BUTTON,
    ID_SCHEDULE_TIME_EDIT = 9001,
    ID_SCHEDULE_TYPE_COMBO,
    ID_SCHEDULE_DAY_COMBO,
    ID_SCHEDULE_OK,
    ID_SCHEDULE_CANCEL,
    ID_RISK_LABEL
};

#define WM_SYNC_EVENT               (WM_USER + 10)
#define WM_SYNC_COMPLETE            (WM_USER + 11)
#define WM_SYNC_PREVIEW_COMPLETE    (WM_USER + 12)
#define WM_SYNC_UNDO_COMPLETE       (WM_USER + 13)
#define WM_SYNC_QUEUE_COMPLETE      (WM_USER + 14)
#define WM_SYNC_ANALYZE_COMPLETE    (WM_USER + 15)

namespace MainLayout {
    constexpr int MinClientW = 660;
    constexpr int MinClientH = 880;
    constexpr int DefaultClientW = 680;
    constexpr int DefaultClientH = 920;
}

struct SyncMessageRegistry {
    std::mutex mtx;
    std::vector<std::wstring> logs;
    std::wstring status;
    int progressPct = 0;
    bool progressChanged = false;
    bool statusChanged = false;

    void PushLog(const std::wstring& line);
    void SetStatus(const std::wstring& stat);
    void SetProgress(int pct);
    bool Drain(std::vector<std::wstring>& outLogs, std::wstring& outStatus, int& outProgress,
               bool& outStatusChanged, bool& outProgressChanged);
};

struct PreviewLaunchData {
    std::vector<ChronoSync::PreviewItem>* pList = nullptr;
    ChronoSync::SyncPlanAnalysis analysis;
    bool hasAnalysis = false;
    std::wstring sourceRoot;
    std::wstring destRoot;
};

struct AnalyzeCompleteData {
    std::wstring report;
    ChronoSync::SyncPlanAnalysis analysis;
    bool hasAnalysis = false;
};

extern HWND g_hWndMain;
extern HWND g_hWndSrcEdit;
extern HWND g_hWndSrcBrowse;
extern HWND g_hWndDestEdit;
extern HWND g_hWndDestBrowse;
extern HWND g_hWndPruneCheck;
extern HWND g_hWndUndoBtn;
extern HWND g_hWndPreviewBtn;
extern HWND g_hWndAnalyzeBtn;
extern HWND g_hWndSyncBtn;
extern HWND g_hWndProgressBar;
extern HWND g_hWndStatusLabel;
extern HWND g_hWndLogEdit;
extern HWND g_hWndExcludeEdit;
extern HWND g_hWndIncludeEdit;
extern HWND g_hWndSaveProfileBtn;
extern HWND g_hWndLoadProfileBtn;
extern HWND g_hWndSha256Check;
extern HWND g_hWndVerifyCheck;
extern HWND g_hWndVersionedBackupCheck;
extern HWND g_hWndQueueList;
extern HWND g_hWndAddQueueBtn;
extern HWND g_hWndRunQueueBtn;
extern HWND g_hWndClearQueueBtn;
extern HWND g_hWndSaveQueueBtn;
extern HWND g_hWndLoadQueueBtn;
extern HWND g_hWndDeltaCopyCheck;
extern HWND g_hWndScheduleBtn;
extern HWND g_hWndHistoryBtn;
extern HWND g_hWndAdvancedGroup;
extern HWND g_hWndRiskLabel;

extern ChronoSync::SyncPlanAnalysis g_CachedPlanAnalysis;
extern bool g_HasCachedPlanAnalysis;
extern std::vector<ChronoSync::SyncJob> g_SyncJobQueue;

extern HBRUSH g_hbrBackground;
extern HBRUSH g_hbrEditBackground;
extern HBRUSH g_hbrInputBackground;
extern HBRUSH g_hbrLogBackground;

extern HFONT g_hFontNormal;
extern HFONT g_hFontLabel;
extern HFONT g_hFontLog;

extern bool g_SyncRunning;
extern SyncMessageRegistry g_MsgRegistry;

bool IsEditControl(HWND hwndCtrl);

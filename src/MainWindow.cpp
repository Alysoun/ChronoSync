#include "MainWindow.h"

#include "GuiCommon.h"
#include "GuiDialogs.h"
#include "SyncGuiWorker.h"
#include "Disclaimer.h"
#include "ScheduleDialog.h"
#include "AnalysisWindow.h"
#include "HistoryWindow.h"
#include "PreviewWindow.h"
#include "SyncProfile.h"
#include "SyncOptions.h"
#include "SyncJob.h"
#include "TaskScheduler.h"
#include "SyncPlanAnalysis.h"

#include <uxtheme.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <sstream>
#include <iomanip>
#include <memory>
#include <algorithm>
#include <thread>

static constexpr int kLogSplitterH = 6;
static constexpr int kMinQueueH = 48;
static constexpr int kMinLogH = 100;
static constexpr int kMaxLogHeightBias = 500;

static bool g_logSplitterDragging = false;
static int g_logSplitterDragStartY = 0;
static int g_logSplitterBiasStart = 0;

static std::wstring FormatRiskSummary(const ChronoSync::SyncPlanAnalysis& analysis) {
    std::wstringstream ss;
    ss << L"Risk: " << ChronoSync::RiskLevelToString(analysis.risk);
    const size_t copies = analysis.filesToCopyNew + analysis.filesToCopyUpdate;
    const size_t removals = analysis.deletesPrune + analysis.deletesReplace;
    if (copies > 0 || analysis.totalBytesToTransfer > 0) {
        ss << L"  |  Transfer: " << copies << L" file(s)";
        double gb = static_cast<double>(analysis.totalBytesToTransfer) / (1024.0 * 1024.0 * 1024.0);
        if (gb >= 1.0) {
            ss << L", " << std::fixed << std::setprecision(1) << gb << L" GB";
        } else {
            double mb = static_cast<double>(analysis.totalBytesToTransfer) / (1024.0 * 1024.0);
            ss << L", " << std::fixed << std::setprecision(1) << mb << L" MB";
        }
    }
    if (removals > 0) {
        ss << L"  |  Removals: " << removals;
        if (analysis.deletesReplace > 0 && analysis.deletesReplace <= 3) {
            for (const auto& entry : analysis.plannedRemovals) {
                if (entry.reason == ChronoSync::DeleteReason::Replace) {
                    ss << L" (" << entry.relativePath << L")";
                }
            }
        }
    }
    if (copies == 0 && removals == 0 && analysis.totalBytesToTransfer == 0) {
        ss << L"  |  Already in sync";
    }
    return ss.str();
}

static void SetRiskIndicator(const ChronoSync::SyncPlanAnalysis& analysis) {
    g_CachedPlanAnalysis = analysis;
    g_HasCachedPlanAnalysis = true;
    if (g_hWndRiskLabel) {
        SetWindowTextW(g_hWndRiskLabel, FormatRiskSummary(analysis).c_str());
    }
}

static void ClearRiskIndicator() {
    g_HasCachedPlanAnalysis = false;
    if (g_hWndRiskLabel) {
        SetWindowTextW(g_hWndRiskLabel, L"Risk: \u2014  |  Run Analyze Plan for impact summary");
    }
}

static void ApplyReadableTheme(HWND hWnd) {
    auto styleButton = [](HWND hwnd) {
        if (hwnd) {
            SetWindowTheme(hwnd, L"Explorer", nullptr);
        }
    };

    styleButton(g_hWndSrcBrowse);
    styleButton(g_hWndDestBrowse);
    styleButton(g_hWndUndoBtn);
    styleButton(g_hWndSaveProfileBtn);
    styleButton(g_hWndLoadProfileBtn);
    styleButton(g_hWndScheduleBtn);
    styleButton(g_hWndHistoryBtn);
    styleButton(g_hWndPreviewBtn);
    styleButton(g_hWndAnalyzeBtn);
    styleButton(g_hWndSyncBtn);
    styleButton(g_hWndAddQueueBtn);
    styleButton(g_hWndRunQueueBtn);
    styleButton(g_hWndClearQueueBtn);
    styleButton(g_hWndSaveQueueBtn);
    styleButton(g_hWndLoadQueueBtn);

    if (g_hWndCopyLogBtn) {
        styleButton(g_hWndCopyLogBtn);
    }

    SendMessageW(g_hWndQueueList, LB_SETITEMHEIGHT, 0, MAKELPARAM(24, 0));
    (void)hWnd;
}

static bool IsLogScrolledToBottom(HWND hwnd) {
    if (!hwnd) {
        return true;
    }
    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    if (!GetScrollInfo(hwnd, SB_VERT, &si)) {
        return true;
    }
    if (si.nMax <= 0) {
        return true;
    }
    int bottomPos = si.nPos + static_cast<int>(si.nPage);
    return bottomPos >= si.nMax - 1;
}

static void ScrollLogToBottom(HWND hwnd) {
    if (!hwnd) {
        return;
    }
    int len = GetWindowTextLengthW(hwnd);
    SendMessageW(hwnd, EM_SETSEL, len, len);
    SendMessageW(hwnd, EM_SCROLLCARET, 0, 0);
}

static LRESULT CALLBACK LogEditSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_KEYDOWN && HandleReadOnlyEditAccelerator(hwnd, wParam)) {
        if (wParam == 'A' || wParam == 'a') {
            g_logFollowTail = false;
        }
        return 0;
    }

    LRESULT result = CallWindowProcW(g_logEditOrigProc, hwnd, message, wParam, lParam);

    switch (message) {
        case WM_VSCROLL:
        case WM_KEYDOWN:
        case WM_LBUTTONUP:
            g_logFollowTail = IsLogScrolledToBottom(hwnd);
            break;
        default:
            break;
    }

    return result;
}

static void ClearOperationLog() {
    SetWindowTextW(g_hWndLogEdit, L"");
    g_logFollowTail = true;
}

static bool EnsureDisclaimerAccepted(HWND hWnd) {
    return ChronoSync::PromptDisclaimerAcceptance(hWnd);
}

static void LayoutMainWindow(HWND hWnd, int cx, int cy) {
    if (!g_hWndSrcEdit || cx <= 0 || cy <= 0) {
        return;
    }

    const int m = UiTheme::Margin;
    const int w = std::max(320, cx - 2 * m);
    const int eh = UiTheme::EditHeight;
    const int bh = UiTheme::ButtonHeight;
    const int browseW = 110;
    const int editW = std::max(180, w - browseW - 8);

    int y = m;

    auto moveLabel = [&](HWND hwnd, int labelY, int labelW) {
        if (hwnd) {
            MoveWindow(hwnd, m, labelY, labelW, 22, TRUE);
        }
    };
    auto moveBtn = [&](HWND hwnd, int bx, int by, int bw, int bhgt) {
        if (hwnd) {
            MoveWindow(hwnd, bx, by, bw, bhgt, TRUE);
        }
    };

    moveLabel(FindWindowExW(hWnd, NULL, L"STATIC", L"Source Folder:"), y, 220);
    y += 24;
    MoveWindow(g_hWndSrcEdit, m, y, editW, eh, TRUE);
    moveBtn(g_hWndSrcBrowse, m + editW + 8, y - 1, browseW, bh);
    y += eh + UiTheme::SectionGap;

    moveLabel(FindWindowExW(hWnd, NULL, L"STATIC", L"Destination Folder:"), y, 220);
    y += 24;
    MoveWindow(g_hWndDestEdit, m, y, editW, eh, TRUE);
    moveBtn(g_hWndDestBrowse, m + editW + 8, y - 1, browseW, bh);
    y += eh + UiTheme::SectionGap;

    MoveWindow(g_hWndPruneCheck, m, y + 2, 18, 18, TRUE);
    MoveWindow(GetDlgItem(hWnd, ID_PRUNE_LABEL), m + 24, y, std::max(120, editW - 24), 22, TRUE);
    moveBtn(g_hWndUndoBtn, m + editW + 8, y - 1, browseW, bh);
    y += bh + UiTheme::SectionGap;

    moveLabel(FindWindowExW(hWnd, NULL, L"STATIC", L"Exclude filters (semicolon-separated):"), y, w);
    y += 24;
    MoveWindow(g_hWndExcludeEdit, m, y, w, eh, TRUE);
    y += eh + UiTheme::RowGap;

    moveLabel(FindWindowExW(hWnd, NULL, L"STATIC", L"Include filters (optional, empty = all):"), y, w);
    y += 24;
    MoveWindow(g_hWndIncludeEdit, m, y, w, eh, TRUE);
    y += eh + UiTheme::SectionGap;

    const int profileBtnW = (w - 24) / 4;
    moveBtn(g_hWndSaveProfileBtn, m, y, profileBtnW, bh);
    moveBtn(g_hWndLoadProfileBtn, m + profileBtnW + 8, y, profileBtnW, bh);
    moveBtn(g_hWndScheduleBtn, m + (profileBtnW + 8) * 2, y, profileBtnW, bh);
    moveBtn(g_hWndHistoryBtn, m + (profileBtnW + 8) * 3, y, profileBtnW, bh);
    y += bh + UiTheme::RowGap;

    if (g_hWndAdvancedGroup) {
        MoveWindow(g_hWndAdvancedGroup, m - 6, y, w + 12, 82, TRUE);
    }
    y += 18;
    const int optW = (w - 8) / 2;
    MoveWindow(g_hWndSha256Check, m + 8, y, optW, 22, TRUE);
    MoveWindow(g_hWndVerifyCheck, m + 8 + optW, y, optW, 22, TRUE);
    y += 26;
    MoveWindow(g_hWndVersionedBackupCheck, m + 8, y, optW, 22, TRUE);
    MoveWindow(g_hWndDeltaCopyCheck, m + 8 + optW, y, optW, 22, TRUE);
    y += 30 + UiTheme::SectionGap;

    const int actionW = (w - 16) / 3;
    moveBtn(g_hWndPreviewBtn, m, y, actionW, 38);
    moveBtn(g_hWndAnalyzeBtn, m + actionW + 8, y, actionW, 38);
    moveBtn(g_hWndSyncBtn, m + (actionW + 8) * 2, y, actionW, 38);
    y += 46 + UiTheme::SectionGap;

    moveLabel(FindWindowExW(hWnd, NULL, L"STATIC", L"Sync Queue:"), y, 160);
    y += 24;

    const int tailFixed = UiTheme::RowGap + bh + UiTheme::SectionGap + 22 + 26 + 28 + UiTheme::RowGap + 20 + 24 +
                          kLogSplitterH;
    int flexH = cy - y - tailFixed - m;
    if (flexH < kMinQueueH + kMinLogH) {
        flexH = kMinQueueH + kMinLogH;
    }

    int logH = std::clamp(120 + g_logHeightBias, kMinLogH, flexH - kMinQueueH);
    int queueH = flexH - logH;
    if (queueH < kMinQueueH) {
        queueH = kMinQueueH;
        logH = flexH - queueH;
    }

    MoveWindow(g_hWndQueueList, m, y, w, queueH, TRUE);
    y += queueH + UiTheme::RowGap;

    const int queueBtnW = (w - 32) / 5;
    moveBtn(g_hWndAddQueueBtn, m, y, queueBtnW, bh);
    moveBtn(g_hWndRunQueueBtn, m + queueBtnW + 8, y, queueBtnW, bh);
    moveBtn(g_hWndClearQueueBtn, m + (queueBtnW + 8) * 2, y, queueBtnW, bh);
    moveBtn(g_hWndSaveQueueBtn, m + (queueBtnW + 8) * 3, y, queueBtnW, bh);
    moveBtn(g_hWndLoadQueueBtn, m + (queueBtnW + 8) * 4, y, queueBtnW, bh);
    y += bh + UiTheme::SectionGap;

    MoveWindow(g_hWndStatusLabel, m, y, w, 22, TRUE);
    y += 26;

    const int riskW = static_cast<int>(w * 0.58);
    const int progW = w - riskW - 8;
    const int progBarW = (progW > 80) ? progW : 80;
    if (g_hWndRiskLabel) {
        MoveWindow(g_hWndRiskLabel, m, y, riskW, 22, TRUE);
    }
    MoveWindow(g_hWndProgressBar, m + riskW + 8, y, progBarW, 22, TRUE);
    y += 28;

    if (g_hWndDisclaimerLink) {
        MoveWindow(g_hWndDisclaimerLink, m, y, w, 18, TRUE);
    }
    y += 20 + UiTheme::RowGap;

    moveLabel(FindWindowExW(hWnd, NULL, L"STATIC", L"Operation Log:"), y, 160);
    const int copyLogBtnW = 72;
    moveBtn(g_hWndCopyLogBtn, m + w - copyLogBtnW, y - 2, copyLogBtnW, bh);
    y += 24;

    if (g_hWndLogSplitter) {
        MoveWindow(g_hWndLogSplitter, m, y, w, kLogSplitterH, TRUE);
        y += kLogSplitterH;
    }

    MoveWindow(g_hWndLogEdit, m, y, w, logH, TRUE);
}

static ChronoSync::FilterOptions GetFiltersFromUI() {
    wchar_t includeBuf[1024] = {};
    wchar_t excludeBuf[1024] = {};
    GetWindowTextW(g_hWndIncludeEdit, includeBuf, 1024);
    GetWindowTextW(g_hWndExcludeEdit, excludeBuf, 1024);
    return ChronoSync::FilterOptions::FromSemicolonList(includeBuf, excludeBuf);
}

static ChronoSync::SyncOptions GetSyncOptionsFromUI() {
    ChronoSync::SyncOptions opts;
    opts.prune = (SendMessageW(g_hWndPruneCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
    opts.filters = GetFiltersFromUI();
    opts.compareMode = (SendMessageW(g_hWndSha256Check, BM_GETCHECK, 0, 0) == BST_CHECKED)
        ? ChronoSync::CompareMode::Sha256
        : ChronoSync::CompareMode::Timestamp;
    opts.verifyAfterCopy = (SendMessageW(g_hWndVerifyCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
    opts.versionedBackups = (SendMessageW(g_hWndVersionedBackupCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
    opts.deltaBlockCopy = (SendMessageW(g_hWndDeltaCopyCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
    opts.maxBackupVersions = 5;
    return opts;
}

static void UpdateUndoButtonState(const std::wstring& destPath) {
    EnableWindow(g_hWndUndoBtn, ChronoSync::SyncEngine::HasRestorableBackups(destPath) ? TRUE : FALSE);
}

static void RefreshQueueListbox() {
    if (!g_hWndQueueList) {
        return;
    }
    SendMessageW(g_hWndQueueList, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < g_SyncJobQueue.size(); ++i) {
        const auto& job = g_SyncJobQueue[i];
        std::wstring line = std::to_wstring(i + 1) + L". " + job.name + L"  (" + job.source + L" -> " + job.destination + L")";
        SendMessageW(g_hWndQueueList, LB_ADDSTRING, 0, (LPARAM)line.c_str());
    }
}

static void ApplyProfileToUI(const ChronoSync::SyncProfile& profile) {
    SetWindowTextW(g_hWndSrcEdit, profile.source.c_str());
    SetWindowTextW(g_hWndDestEdit, profile.destination.c_str());
    SendMessageW(g_hWndPruneCheck, BM_SETCHECK, profile.options.prune ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(g_hWndIncludeEdit, profile.options.filters.IncludeToSemicolonList().c_str());
    SetWindowTextW(g_hWndExcludeEdit, profile.options.filters.ExcludeToSemicolonList().c_str());
    SendMessageW(g_hWndSha256Check, BM_SETCHECK,
                 profile.options.compareMode == ChronoSync::CompareMode::Sha256 ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_hWndVerifyCheck, BM_SETCHECK, profile.options.verifyAfterCopy ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_hWndVersionedBackupCheck, BM_SETCHECK, profile.options.versionedBackups ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_hWndDeltaCopyCheck, BM_SETCHECK, profile.options.deltaBlockCopy ? BST_CHECKED : BST_UNCHECKED, 0);
    UpdateUndoButtonState(profile.destination);
}

static ChronoSync::SyncProfile BuildProfileFromUI() {
    wchar_t src[MAX_PATH] = {};
    wchar_t dest[MAX_PATH] = {};
    GetWindowTextW(g_hWndSrcEdit, src, MAX_PATH);
    GetWindowTextW(g_hWndDestEdit, dest, MAX_PATH);

    ChronoSync::SyncProfile profile;
    profile.name = L"ChronoSync Profile";
    profile.source = src;
    profile.destination = dest;
    profile.options = GetSyncOptionsFromUI();
    return profile;
}

static ChronoSync::SyncJob BuildJobFromUI() {
    wchar_t src[MAX_PATH] = {};
    wchar_t dest[MAX_PATH] = {};
    GetWindowTextW(g_hWndSrcEdit, src, MAX_PATH);
    GetWindowTextW(g_hWndDestEdit, dest, MAX_PATH);

    ChronoSync::SyncJob job;
    job.name = L"Sync Job " + std::to_wstring(g_SyncJobQueue.size() + 1);
    job.source = src;
    job.destination = dest;
    job.options = GetSyncOptionsFromUI();
    return job;
}

static void CreateControls(HWND hWnd, HINSTANCE hInstance) {
    const int w = UiTheme::ContentWidth;
    const int m = UiTheme::Margin;
    const int eh = UiTheme::EditHeight;
    const int bh = UiTheme::ButtonHeight;
    const int browseW = 110;
    const int editW = w - browseW - 8;
    int y = m;

    HWND lblSrc = CreateWindowExW(0, L"STATIC", L"Source Folder:", WS_CHILD | WS_VISIBLE,
                                  m, y, 200, 22, hWnd, NULL, hInstance, NULL);
    y += 24;
    g_hWndSrcEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                                    m, y, editW, eh, hWnd, (HMENU)ID_SRC_EDIT, hInstance, NULL);
    g_hWndSrcBrowse = CreateWindowExW(0, L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                      m + editW + 8, y - 1, browseW, bh, hWnd, (HMENU)ID_SRC_BROWSE, hInstance, NULL);
    y += eh + UiTheme::SectionGap;

    HWND lblDest = CreateWindowExW(0, L"STATIC", L"Destination Folder:", WS_CHILD | WS_VISIBLE,
                                   m, y, 200, 22, hWnd, NULL, hInstance, NULL);
    y += 24;
    g_hWndDestEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                                     m, y, editW, eh, hWnd, (HMENU)ID_DEST_EDIT, hInstance, NULL);
    g_hWndDestBrowse = CreateWindowExW(0, L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       m + editW + 8, y - 1, browseW, bh, hWnd, (HMENU)ID_DEST_BROWSE, hInstance, NULL);
    y += eh + UiTheme::SectionGap;

    g_hWndPruneCheck = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                       m, y + 2, 18, 18, hWnd, (HMENU)ID_PRUNE_CHECKBOX, hInstance, NULL);
    HWND lblPrune = CreateWindowExW(0, L"STATIC", L"Prune destination (delete files not in source)",
                                    WS_CHILD | WS_VISIBLE | SS_NOTIFY,
                                    m + 24, y, editW - 24, 22, hWnd, (HMENU)ID_PRUNE_LABEL, hInstance, NULL);
    g_hWndUndoBtn = CreateWindowExW(0, L"BUTTON", L"Undo Pruning", WS_CHILD | WS_VISIBLE | WS_DISABLED | BS_PUSHBUTTON,
                                    m + editW + 8, y - 1, browseW, bh, hWnd, (HMENU)ID_UNDO_BUTTON, hInstance, NULL);
    y += bh + UiTheme::SectionGap;

    HWND lblExclude = CreateWindowExW(0, L"STATIC", L"Exclude filters (semicolon-separated):",
                                      WS_CHILD | WS_VISIBLE, m, y, 400, 22, hWnd, NULL, hInstance, NULL);
    y += 24;
    g_hWndExcludeEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"*.pkl;node_modules;*.zip",
                                        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                        m, y, w, eh, hWnd, (HMENU)ID_EXCLUDE_FILTER_EDIT, hInstance, NULL);
    y += eh + UiTheme::RowGap;

    HWND lblInclude = CreateWindowExW(0, L"STATIC", L"Include filters (optional, empty = all):",
                                      WS_CHILD | WS_VISIBLE, m, y, 400, 22, hWnd, NULL, hInstance, NULL);
    y += 24;
    g_hWndIncludeEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                        m, y, w, eh, hWnd, (HMENU)ID_INCLUDE_FILTER_EDIT, hInstance, NULL);
    y += eh + UiTheme::SectionGap;

    const int profileBtnW = (w - 24) / 4;
    g_hWndSaveProfileBtn = CreateWindowExW(0, L"BUTTON", L"Save Profile...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                           m, y, profileBtnW, bh, hWnd, (HMENU)ID_SAVE_PROFILE_BUTTON, hInstance, NULL);
    g_hWndLoadProfileBtn = CreateWindowExW(0, L"BUTTON", L"Load Profile...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                           m + profileBtnW + 8, y, profileBtnW, bh, hWnd, (HMENU)ID_LOAD_PROFILE_BUTTON, hInstance, NULL);
    g_hWndScheduleBtn = CreateWindowExW(0, L"BUTTON", L"Schedule...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                        m + (profileBtnW + 8) * 2, y, profileBtnW, bh, hWnd, (HMENU)ID_SCHEDULE_BUTTON, hInstance, NULL);
    g_hWndHistoryBtn = CreateWindowExW(0, L"BUTTON", L"History...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       m + (profileBtnW + 8) * 3, y, profileBtnW, bh, hWnd, (HMENU)ID_HISTORY_BUTTON, hInstance, NULL);
    y += bh + UiTheme::RowGap;

    g_hWndAdvancedGroup = CreateWindowExW(0, L"BUTTON", L"Advanced Options",
                                          WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                          m - 6, y, w + 12, 82, hWnd, NULL, hInstance, NULL);
    y += 18;

    g_hWndSha256Check = CreateWindowExW(0, L"BUTTON", L"SHA256 compare", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                        m + 8, y, 180, 22, hWnd, (HMENU)ID_SHA256_CHECKBOX, hInstance, NULL);
    g_hWndVerifyCheck = CreateWindowExW(0, L"BUTTON", L"Verify after copy", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                        m + 195, y, 180, 22, hWnd, (HMENU)ID_VERIFY_COPY_CHECKBOX, hInstance, NULL);
    y += 26;
    g_hWndVersionedBackupCheck = CreateWindowExW(0, L"BUTTON", L"Versioned backups", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                                 m + 8, y, 180, 22, hWnd, (HMENU)ID_VERSIONED_BACKUP_CHECKBOX, hInstance, NULL);
    SendMessageW(g_hWndVersionedBackupCheck, BM_SETCHECK, BST_CHECKED, 0);
    g_hWndDeltaCopyCheck = CreateWindowExW(0, L"BUTTON", L"Smart block compare", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                           m + 195, y, 200, 22, hWnd, (HMENU)ID_DELTA_COPY_CHECKBOX, hInstance, NULL);
    y += 30 + UiTheme::SectionGap;

    const int actionW = (w - 16) / 3;
    g_hWndPreviewBtn = CreateWindowExW(0, L"BUTTON", L"Preview Changes", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       m, y, actionW, 38, hWnd, (HMENU)ID_PREVIEW_BUTTON, hInstance, NULL);
    g_hWndAnalyzeBtn = CreateWindowExW(0, L"BUTTON", L"Analyze Plan", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       m + actionW + 8, y, actionW, 38, hWnd, (HMENU)ID_ANALYZE_BUTTON, hInstance, NULL);
    g_hWndSyncBtn = CreateWindowExW(0, L"BUTTON", L"Sync Now", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                    m + (actionW + 8) * 2, y, actionW, 38, hWnd, (HMENU)ID_SYNC_BUTTON, hInstance, NULL);
    y += 46 + UiTheme::SectionGap;

    HWND lblQueue = CreateWindowExW(0, L"STATIC", L"Sync Queue:", WS_CHILD | WS_VISIBLE,
                                    m, y, 160, 22, hWnd, NULL, hInstance, NULL);
    y += 24;
    g_hWndQueueList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                      WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL,
                                      m, y, w, 80, hWnd, (HMENU)ID_QUEUE_LISTBOX, hInstance, NULL);
    y += 88 + UiTheme::RowGap;

    const int queueBtnW = (w - 32) / 5;
    g_hWndAddQueueBtn = CreateWindowExW(0, L"BUTTON", L"Add to Queue", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                        m, y, queueBtnW, bh, hWnd, (HMENU)ID_ADD_QUEUE_BUTTON, hInstance, NULL);
    g_hWndRunQueueBtn = CreateWindowExW(0, L"BUTTON", L"Run Queue", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                        m + queueBtnW + 8, y, queueBtnW, bh, hWnd, (HMENU)ID_RUN_QUEUE_BUTTON, hInstance, NULL);
    g_hWndClearQueueBtn = CreateWindowExW(0, L"BUTTON", L"Clear Queue", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                          m + (queueBtnW + 8) * 2, y, queueBtnW, bh, hWnd, (HMENU)ID_CLEAR_QUEUE_BUTTON, hInstance, NULL);
    g_hWndSaveQueueBtn = CreateWindowExW(0, L"BUTTON", L"Save Queue...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                         m + (queueBtnW + 8) * 3, y, queueBtnW, bh, hWnd, (HMENU)ID_SAVE_QUEUE_BUTTON, hInstance, NULL);
    g_hWndLoadQueueBtn = CreateWindowExW(0, L"BUTTON", L"Load Queue...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                         m + (queueBtnW + 8) * 4, y, queueBtnW, bh, hWnd, (HMENU)ID_LOAD_QUEUE_BUTTON, hInstance, NULL);
    y += bh + UiTheme::SectionGap;

    g_hWndStatusLabel = CreateWindowExW(0, L"STATIC", L"Ready", WS_CHILD | WS_VISIBLE,
                                        m, y, w, 22, hWnd, (HMENU)ID_STATUS_LABEL, hInstance, NULL);
    y += 26;
    g_hWndRiskLabel = CreateWindowExW(0, L"STATIC",
                                      L"Risk: \u2014  |  Run Analyze Plan for impact summary",
                                      WS_CHILD | WS_VISIBLE,
                                      m, y, w / 2, 22, hWnd, (HMENU)ID_RISK_LABEL, hInstance, NULL);
    g_hWndProgressBar = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE,
                                        m + w / 2 + 8, y, w / 2 - 8, 22, hWnd, (HMENU)ID_PROGRESS_BAR, hInstance, NULL);
    y += 28;

    g_hWndDisclaimerLink = CreateWindowExW(0, L"STATIC",
        L"Use at your own risk. Read disclaimer before syncing.",
        WS_CHILD | WS_VISIBLE | SS_NOTIFY,
        m, y, w, 18, hWnd, (HMENU)ID_DISCLAIMER_LINK, hInstance, NULL);
    y += 20 + UiTheme::RowGap;

    HWND lblLog = CreateWindowExW(0, L"STATIC", L"Operation Log:", WS_CHILD | WS_VISIBLE,
                                  m, y, 160, 22, hWnd, NULL, hInstance, NULL);
    const int copyLogBtnW = 72;
    g_hWndCopyLogBtn = CreateWindowExW(0, L"BUTTON", L"Copy", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       m + w - copyLogBtnW, y - 2, copyLogBtnW, bh,
                                       hWnd, (HMENU)ID_COPY_LOG_BUTTON, hInstance, NULL);
    y += 24;
    g_hWndLogSplitter = CreateWindowExW(0, L"STATIC", NULL,
                                        WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
                                        m, y, w, kLogSplitterH, hWnd, (HMENU)ID_LOG_SPLITTER, hInstance, NULL);
    y += kLogSplitterH;
    g_hWndLogEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                    WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
                                    m, y, w, 120, hWnd, (HMENU)ID_LOG_EDIT, hInstance, NULL);
    SendMessageW(g_hWndLogEdit, EM_SETLIMITTEXT, 200000, 0);
    g_logEditOrigProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        g_hWndLogEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(LogEditSubclassProc)));

    auto setLabelFont = [&](HWND hwnd) {
        if (hwnd) {
            SendMessageW(hwnd, WM_SETFONT, (WPARAM)g_hFontLabel, TRUE);
        }
    };
    auto setUIFont = [&](HWND hwnd) {
        if (hwnd) {
            SendMessageW(hwnd, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
        }
    };

    setLabelFont(lblSrc);
    setLabelFont(lblDest);
    setLabelFont(lblExclude);
    setLabelFont(lblInclude);
    setLabelFont(lblQueue);
    setLabelFont(lblLog);

    setUIFont(g_hWndSrcEdit);
    setUIFont(g_hWndSrcBrowse);
    setUIFont(g_hWndDestEdit);
    setUIFont(g_hWndDestBrowse);
    setUIFont(g_hWndPruneCheck);
    setUIFont(lblPrune);
    setUIFont(g_hWndUndoBtn);
    setUIFont(g_hWndExcludeEdit);
    setUIFont(g_hWndIncludeEdit);
    setUIFont(g_hWndSaveProfileBtn);
    setUIFont(g_hWndLoadProfileBtn);
    setUIFont(g_hWndScheduleBtn);
    setUIFont(g_hWndHistoryBtn);
    setUIFont(g_hWndAdvancedGroup);
    setUIFont(g_hWndSha256Check);
    setUIFont(g_hWndVerifyCheck);
    setUIFont(g_hWndVersionedBackupCheck);
    setUIFont(g_hWndDeltaCopyCheck);
    setUIFont(g_hWndPreviewBtn);
    setUIFont(g_hWndAnalyzeBtn);
    setUIFont(g_hWndSyncBtn);
    setUIFont(g_hWndQueueList);
    setUIFont(g_hWndAddQueueBtn);
    setUIFont(g_hWndRunQueueBtn);
    setUIFont(g_hWndClearQueueBtn);
    setUIFont(g_hWndSaveQueueBtn);
    setUIFont(g_hWndLoadQueueBtn);
    setUIFont(g_hWndCopyLogBtn);
    setUIFont(g_hWndStatusLabel);
    setUIFont(g_hWndRiskLabel);
    setUIFont(g_hWndDisclaimerLink);
    SendMessageW(g_hWndLogEdit, WM_SETFONT, (WPARAM)g_hFontLog, TRUE);

    ApplyReadableTheme(hWnd);

    RECT clientRect = {};
    GetClientRect(hWnd, &clientRect);
    LayoutMainWindow(hWnd, clientRect.right, clientRect.bottom);
}

LRESULT CALLBACK MainWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            g_hWndMain = hWnd;

            g_hbrBackground = CreateSolidBrush(UiTheme::WindowBg);
            g_hbrEditBackground = CreateSolidBrush(UiTheme::InputBg);
            g_hbrInputBackground = CreateSolidBrush(UiTheme::InputBg);
            g_hbrLogBackground = CreateSolidBrush(UiTheme::LogBg);

            g_hFontNormal = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                       DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontLabel = CreateFontW(-17, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                      DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontLog = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                    DEFAULT_PITCH | FF_DONTCARE, L"Consolas");

            CreateControls(hWnd, ((LPCREATESTRUCT)lParam)->hInstance);
            ClearRiskIndicator();
            if (!ChronoSync::PromptDisclaimerAcceptance(hWnd)) {
                DestroyWindow(hWnd);
            }
            break;
        }
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            mmi->ptMinTrackSize.x = MainLayout::MinClientW;
            mmi->ptMinTrackSize.y = MainLayout::MinClientH;
            RECT rc = {0, 0, MainLayout::MinClientW, MainLayout::MinClientH};
            AdjustWindowRectEx(&rc, WS_OVERLAPPEDWINDOW, FALSE, 0);
            mmi->ptMinTrackSize.x = rc.right - rc.left;
            mmi->ptMinTrackSize.y = rc.bottom - rc.top;
            break;
        }
        case WM_SIZE: {
            if (wParam != SIZE_MINIMIZED) {
                LayoutMainWindow(hWnd, LOWORD(lParam), HIWORD(lParam));
            }
            break;
        }
        case WM_SETCURSOR: {
            if ((HWND)wParam == g_hWndLogSplitter && LOWORD(lParam) == HTCLIENT) {
                SetCursor(LoadCursor(NULL, IDC_SIZENS));
                return TRUE;
            }
            break;
        }
        case WM_LBUTTONDOWN: {
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            HWND hit = ChildWindowFromPointEx(hWnd, pt, CWP_SKIPINVISIBLE);
            if (hit == g_hWndLogSplitter) {
                g_logSplitterDragging = true;
                g_logSplitterDragStartY = pt.y;
                g_logSplitterBiasStart = g_logHeightBias;
                SetCapture(hWnd);
                return 0;
            }
            break;
        }
        case WM_MOUSEMOVE: {
            if (g_logSplitterDragging) {
                int dy = GET_Y_LPARAM(lParam) - g_logSplitterDragStartY;
                g_logHeightBias = std::clamp(g_logSplitterBiasStart + dy, 0, kMaxLogHeightBias);
                RECT rc = {};
                GetClientRect(hWnd, &rc);
                LayoutMainWindow(hWnd, rc.right, rc.bottom);
                return 0;
            }
            break;
        }
        case WM_MOUSEWHEEL: {
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            HWND wheelTarget = WindowFromPoint(pt);
            if (wheelTarget == g_hWndLogEdit || IsChild(g_hWndLogEdit, wheelTarget)) {
                LRESULT wheelResult = DefWindowProcW(hWnd, message, wParam, lParam);
                g_logFollowTail = IsLogScrolledToBottom(g_hWndLogEdit);
                return wheelResult;
            }
            break;
        }
        case WM_LBUTTONUP: {
            if (g_logSplitterDragging) {
                g_logSplitterDragging = false;
                ReleaseCapture();
                return 0;
            }
            break;
        }
        case WM_COMMAND: {
            int wmId = LOWORD(wParam);

            if (wmId == ID_PRUNE_LABEL) {
                LRESULT state = SendMessageW(g_hWndPruneCheck, BM_GETCHECK, 0, 0);
                SendMessageW(g_hWndPruneCheck, BM_SETCHECK, state == BST_CHECKED ? BST_UNCHECKED : BST_CHECKED, 0);
                break;
            }

            if (HIWORD(wParam) == STN_CLICKED && wmId == ID_DISCLAIMER_LINK) {
                ChronoSync::ShowDisclaimerDialog(hWnd);
                break;
            }

            if (wmId == ID_COPY_LOG_BUTTON) {
                CopyEditContentToClipboard(g_hWndLogEdit);
                break;
            }

            if (wmId == ID_SRC_BROWSE) {
                std::wstring path = BrowseForFolder(hWnd, L"Select Source Folder");
                if (!path.empty()) {
                    SetWindowTextW(g_hWndSrcEdit, path.c_str());
                    ClearRiskIndicator();
                }
            } else if (wmId == ID_DEST_BROWSE) {
                std::wstring path = BrowseForFolder(hWnd, L"Select Destination Folder");
                if (!path.empty()) {
                    SetWindowTextW(g_hWndDestEdit, path.c_str());
                    UpdateUndoButtonState(path);
                    ClearRiskIndicator();
                }
            } else if (wmId == ID_UNDO_BUTTON) {
                if (!EnsureDisclaimerAccepted(hWnd)) {
                    break;
                }
                wchar_t dest[MAX_PATH];
                GetWindowTextW(g_hWndDestEdit, dest, MAX_PATH);
                std::wstring destPath(dest);

                if (destPath.empty()) {
                    break;
                }

                SetControlsState(FALSE);
                EnableWindow(g_hWndUndoBtn, FALSE);
                ClearOperationLog();
                g_MsgRegistry.ResetForNewRun();

                g_SyncRunning = true;
                std::thread t(UndoThreadProc, destPath);
                t.detach();
            } else if (wmId == ID_PREVIEW_BUTTON) {
                wchar_t src[MAX_PATH];
                wchar_t dest[MAX_PATH];
                GetWindowTextW(g_hWndSrcEdit, src, MAX_PATH);
                GetWindowTextW(g_hWndDestEdit, dest, MAX_PATH);

                std::wstring sourcePath(src);
                std::wstring destPath(dest);

                if (sourcePath.empty() || destPath.empty()) {
                    MessageBoxW(hWnd, L"Please select both source and destination folders.", L"ChronoSync Verification", MB_OK | MB_ICONWARNING);
                    break;
                }
                if (sourcePath == destPath) {
                    MessageBoxW(hWnd, L"Source and destination folders cannot be the same.", L"ChronoSync Verification", MB_OK | MB_ICONWARNING);
                    break;
                }

                ChronoSync::SyncOptions options = GetSyncOptionsFromUI();

                SetControlsState(FALSE);
                ClearOperationLog();
                g_MsgRegistry.ResetForNewRun();

                g_SyncRunning = true;
                std::thread t(PreviewThreadProc, sourcePath, destPath, options);
                t.detach();
            } else if (wmId == ID_ANALYZE_BUTTON) {
                wchar_t src[MAX_PATH];
                wchar_t dest[MAX_PATH];
                GetWindowTextW(g_hWndSrcEdit, src, MAX_PATH);
                GetWindowTextW(g_hWndDestEdit, dest, MAX_PATH);

                std::wstring sourcePath(src);
                std::wstring destPath(dest);

                if (sourcePath.empty() || destPath.empty()) {
                    MessageBoxW(hWnd, L"Please select both source and destination folders.", L"ChronoSync Analyze", MB_OK | MB_ICONWARNING);
                    break;
                }
                if (sourcePath == destPath) {
                    MessageBoxW(hWnd, L"Source and destination folders cannot be the same.", L"ChronoSync Analyze", MB_OK | MB_ICONWARNING);
                    break;
                }

                ChronoSync::SyncOptions options = GetSyncOptionsFromUI();
                SetControlsState(FALSE);
                ClearOperationLog();
                g_MsgRegistry.ResetForNewRun();
                g_SyncRunning = true;
                std::thread t(AnalyzeThreadProc, sourcePath, destPath, options);
                t.detach();
            } else if (wmId == ID_ADD_QUEUE_BUTTON) {
                ChronoSync::SyncJob job = BuildJobFromUI();
                if (job.source.empty() || job.destination.empty()) {
                    MessageBoxW(hWnd, L"Please select both source and destination folders before adding to the queue.",
                                L"ChronoSync Queue", MB_OK | MB_ICONWARNING);
                    break;
                }
                if (job.source == job.destination) {
                    MessageBoxW(hWnd, L"Source and destination folders cannot be the same.", L"ChronoSync Queue", MB_OK | MB_ICONWARNING);
                    break;
                }
                g_SyncJobQueue.push_back(std::move(job));
                RefreshQueueListbox();
            } else if (wmId == ID_CLEAR_QUEUE_BUTTON) {
                g_SyncJobQueue.clear();
                RefreshQueueListbox();
            } else if (wmId == ID_RUN_QUEUE_BUTTON) {
                if (!EnsureDisclaimerAccepted(hWnd)) {
                    break;
                }
                if (g_SyncJobQueue.empty()) {
                    MessageBoxW(hWnd, L"The sync queue is empty.", L"ChronoSync Queue", MB_OK | MB_ICONINFORMATION);
                    break;
                }
                SetControlsState(FALSE);
                ClearOperationLog();
                g_MsgRegistry.ResetForNewRun();
                g_SyncRunning = true;
                std::thread t(QueueThreadProc, g_SyncJobQueue);
                t.detach();
            } else if (wmId == ID_SAVE_QUEUE_BUTTON) {
                if (g_SyncJobQueue.empty()) {
                    MessageBoxW(hWnd, L"No jobs in the queue to save.", L"ChronoSync Queue", MB_OK | MB_ICONWARNING);
                    break;
                }
                std::wstring path = SaveQueueDialog(hWnd);
                if (!path.empty()) {
                    std::wstring error;
                    if (ChronoSync::SyncJobQueueIO::SaveToFile(g_SyncJobQueue, path, error)) {
                        MessageBoxW(hWnd, L"Queue saved successfully.", L"ChronoSync Queue", MB_OK | MB_ICONINFORMATION);
                    } else {
                        MessageBoxW(hWnd, (L"Failed to save queue: " + error).c_str(), L"ChronoSync Queue", MB_OK | MB_ICONERROR);
                    }
                }
            } else if (wmId == ID_LOAD_QUEUE_BUTTON) {
                std::wstring path = OpenQueueDialog(hWnd);
                if (!path.empty()) {
                    std::vector<ChronoSync::SyncJob> loaded;
                    std::wstring error;
                    if (ChronoSync::SyncJobQueueIO::LoadFromFile(path, loaded, error)) {
                        g_SyncJobQueue = std::move(loaded);
                        RefreshQueueListbox();
                        MessageBoxW(hWnd, L"Queue loaded successfully.", L"ChronoSync Queue", MB_OK | MB_ICONINFORMATION);
                    } else {
                        MessageBoxW(hWnd, (L"Failed to load queue: " + error).c_str(), L"ChronoSync Queue", MB_OK | MB_ICONERROR);
                    }
                }
            } else if (wmId == ID_HISTORY_BUTTON) {
                wchar_t dest[MAX_PATH] = {};
                GetWindowTextW(g_hWndDestEdit, dest, MAX_PATH);
                if (dest[0] == L'\0') {
                    MessageBoxW(hWnd, L"Select a destination folder to view sync history.",
                                L"ChronoSync History", MB_OK | MB_ICONWARNING);
                    break;
                }
                ShowHistoryWindow(hWnd, dest);
            } else if (wmId == ID_SCHEDULE_BUTTON) {
                ChronoSync::SyncProfile profile = BuildProfileFromUI();
                if (profile.source.empty() || profile.destination.empty()) {
                    MessageBoxW(hWnd, L"Please configure source and destination before scheduling.", L"ChronoSync Schedule", MB_OK | MB_ICONWARNING);
                    break;
                }

                std::wstring profilePath = SaveProfileDialog(hWnd);
                if (profilePath.empty()) {
                    break;
                }

                std::wstring saveError;
                if (!ChronoSync::SyncProfileIO::SaveToFile(profile, profilePath, saveError)) {
                    MessageBoxW(hWnd, (L"Failed to save profile for scheduling: " + saveError).c_str(), L"ChronoSync Schedule", MB_OK | MB_ICONERROR);
                    break;
                }

                ScheduleDialogState scheduleState;
                scheduleState.profilePath = profilePath;
                if (!RunScheduleDialog(hWnd, scheduleState)) {
                    break;
                }

                wchar_t exePath[MAX_PATH] = {};
                GetModuleFileNameW(NULL, exePath, MAX_PATH);
                std::wstring taskName = ChronoSync::TaskScheduler::SanitizeTaskName(profile.name);
                std::wstring scheduleError;
                bool scheduled = scheduleState.weekly
                    ? ChronoSync::TaskScheduler::CreateWeeklyTask(taskName, exePath, profilePath, scheduleState.hour, scheduleState.minute, scheduleState.dayName, scheduleError)
                    : ChronoSync::TaskScheduler::CreateDailyTask(taskName, exePath, profilePath, scheduleState.hour, scheduleState.minute, scheduleError);

                if (scheduled) {
                    wchar_t timeMsg[32] = {};
                    swprintf_s(timeMsg, L"%02d:%02d", scheduleState.hour, scheduleState.minute);
                    std::wstring msg = L"Scheduled task created:\n" + taskName + L"\nTime: " + timeMsg;
                    if (scheduleState.weekly) {
                        msg += L"\nDay: " + scheduleState.dayName;
                    }
                    MessageBoxW(hWnd, msg.c_str(), L"ChronoSync Schedule", MB_OK | MB_ICONINFORMATION);
                } else {
                    MessageBoxW(hWnd, (L"Failed to create scheduled task: " + scheduleError).c_str(), L"ChronoSync Schedule", MB_OK | MB_ICONERROR);
                }
            } else if (wmId == ID_SAVE_PROFILE_BUTTON) {
                ChronoSync::SyncProfile profile = BuildProfileFromUI();
                if (profile.source.empty() || profile.destination.empty()) {
                    MessageBoxW(hWnd, L"Please select both source and destination folders before saving a profile.", L"ChronoSync Profile", MB_OK | MB_ICONWARNING);
                    break;
                }
                std::wstring path = SaveProfileDialog(hWnd);
                if (!path.empty()) {
                    std::wstring error;
                    if (ChronoSync::SyncProfileIO::SaveToFile(profile, path, error)) {
                        MessageBoxW(hWnd, L"Profile saved successfully.", L"ChronoSync Profile", MB_OK | MB_ICONINFORMATION);
                    } else {
                        MessageBoxW(hWnd, (L"Failed to save profile: " + error).c_str(), L"ChronoSync Profile", MB_OK | MB_ICONERROR);
                    }
                }
            } else if (wmId == ID_LOAD_PROFILE_BUTTON) {
                std::wstring path = OpenProfileDialog(hWnd);
                if (!path.empty()) {
                    ChronoSync::SyncProfile profile;
                    std::wstring error;
                    if (ChronoSync::SyncProfileIO::LoadFromFile(path, profile, error)) {
                        ApplyProfileToUI(profile);
                        MessageBoxW(hWnd, (L"Loaded profile: " + profile.name).c_str(), L"ChronoSync Profile", MB_OK | MB_ICONINFORMATION);
                    } else {
                        MessageBoxW(hWnd, (L"Failed to load profile: " + error).c_str(), L"ChronoSync Profile", MB_OK | MB_ICONERROR);
                    }
                }
            } else if (wmId == ID_SYNC_BUTTON) {
                if (!EnsureDisclaimerAccepted(hWnd)) {
                    break;
                }
                wchar_t src[MAX_PATH];
                wchar_t dest[MAX_PATH];
                GetWindowTextW(g_hWndSrcEdit, src, MAX_PATH);
                GetWindowTextW(g_hWndDestEdit, dest, MAX_PATH);

                std::wstring sourcePath(src);
                std::wstring destPath(dest);

                if (sourcePath.empty() || destPath.empty()) {
                    MessageBoxW(hWnd, L"Please select both source and destination folders.", L"ChronoSync Verification", MB_OK | MB_ICONWARNING);
                    break;
                }
                if (sourcePath == destPath) {
                    MessageBoxW(hWnd, L"Source and destination folders cannot be the same.", L"ChronoSync Verification", MB_OK | MB_ICONWARNING);
                    break;
                }

                ChronoSync::SyncOptions options = GetSyncOptionsFromUI();

                SetControlsState(FALSE);
                ClearOperationLog();
                g_MsgRegistry.ResetForNewRun();

                g_SyncRunning = true;
                std::thread t(SyncThreadProc, sourcePath, destPath, options);
                t.detach();
            }
            break;
        }
        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wParam;
            HWND hwndCtrl = (HWND)lParam;
            if (hwndCtrl == g_hWndRiskLabel) {
                SetTextColor(hdcStatic, UiTheme::LabelText);
                SetBkMode(hdcStatic, TRANSPARENT);
                return (INT_PTR)g_hbrBackground;
            }
            if (hwndCtrl == g_hWndDisclaimerLink) {
                SetTextColor(hdcStatic, UiTheme::MutedText);
                SetBkMode(hdcStatic, TRANSPARENT);
                return (INT_PTR)g_hbrBackground;
            }
            if (IsEditControl(hwndCtrl)) {
                if (hwndCtrl == g_hWndLogEdit) {
                    SetTextColor(hdcStatic, UiTheme::LogText);
                    SetBkColor(hdcStatic, UiTheme::LogBg);
                    return (INT_PTR)g_hbrLogBackground;
                }
                SetTextColor(hdcStatic, UiTheme::InputText);
                SetBkColor(hdcStatic, UiTheme::InputBg);
                return (INT_PTR)g_hbrInputBackground;
            }
            SetTextColor(hdcStatic, UiTheme::LabelText);
            SetBkMode(hdcStatic, TRANSPARENT);
            return (INT_PTR)g_hbrBackground;
        }
        case WM_CTLCOLOREDIT: {
            HDC hdcEdit = (HDC)wParam;
            HWND hwndCtrl = (HWND)lParam;
            if (hwndCtrl == g_hWndLogEdit) {
                SetTextColor(hdcEdit, UiTheme::LogText);
                SetBkColor(hdcEdit, UiTheme::LogBg);
                return (INT_PTR)g_hbrLogBackground;
            }
            SetTextColor(hdcEdit, UiTheme::InputText);
            SetBkColor(hdcEdit, UiTheme::InputBg);
            return (INT_PTR)g_hbrInputBackground;
        }
        case WM_CTLCOLORLISTBOX: {
            HDC hdcList = (HDC)wParam;
            SetTextColor(hdcList, UiTheme::InputText);
            SetBkColor(hdcList, UiTheme::InputBg);
            return (INT_PTR)g_hbrInputBackground;
        }
        case WM_CTLCOLORBTN: {
            HDC hdcBtn = (HDC)wParam;
            SetTextColor(hdcBtn, UiTheme::LabelText);
            SetBkMode(hdcBtn, TRANSPARENT);
            return (INT_PTR)g_hbrBackground;
        }
        case WM_SYNC_EVENT: {
            BeginSyncUiDrain();

            for (;;) {
                std::vector<std::wstring> drainedLogs;
                std::wstring drainedStatus;
                int drainedProgress = 0;
                bool statusChanged = false;
                bool progressChanged = false;

                if (!g_MsgRegistry.Drain(drainedLogs, drainedStatus, drainedProgress, statusChanged, progressChanged)) {
                    break;
                }

                if (!drainedLogs.empty()) {
                    SendMessageW(g_hWndLogEdit, WM_SETREDRAW, FALSE, 0);
                    for (const auto& log : drainedLogs) {
                        int len = GetWindowTextLengthW(g_hWndLogEdit);
                        if (len > 120000) {
                            SendMessageW(g_hWndLogEdit, EM_SETSEL, 0, 60000);
                            SendMessageW(g_hWndLogEdit, EM_REPLACESEL, FALSE, (LPARAM)L"");
                        }
                        SendMessageW(g_hWndLogEdit, EM_SETSEL, len, len);
                        SendMessageW(g_hWndLogEdit, EM_REPLACESEL, FALSE, (LPARAM)log.c_str());
                        SendMessageW(g_hWndLogEdit, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n");
                    }
                    SendMessageW(g_hWndLogEdit, WM_SETREDRAW, TRUE, 0);
                    InvalidateRect(g_hWndLogEdit, NULL, TRUE);
                    if (g_logFollowTail) {
                        ScrollLogToBottom(g_hWndLogEdit);
                    }
                }

                if (statusChanged) {
                    SetWindowTextW(g_hWndStatusLabel, drainedStatus.c_str());
                }

                if (progressChanged) {
                    SendMessageW(g_hWndProgressBar, PBM_SETPOS, drainedProgress, 0);
                }
            }

            if (g_MsgRegistry.HasPending()) {
                RequestSyncUiEvent();
            }
            break;
        }
        case WM_SYNC_COMPLETE: {
            g_SyncRunning = false;
            SetControlsState(TRUE);

            SendMessageW(g_hWndProgressBar, PBM_SETPOS, 0, 0);
            SetWindowTextW(g_hWndStatusLabel, L"Ready");

            std::unique_ptr<ChronoSync::SyncStats> pStats((ChronoSync::SyncStats*)lParam);
            if (pStats) {
                std::wstringstream ss;
                ss << L"Synchronization Completed successfully!\n\n"
                   << L"Directories Created: " << pStats->dirsCreated << L"\n"
                   << L"Files Transferred:   " << pStats->filesCopied << L"\n"
                   << L"Files Skipped:       " << pStats->filesSkipped << L"\n"
                   << L"Items Pruned/Backed: " << pStats->itemsDeleted << L"\n"
                   << L"Files Verified:      " << pStats->filesVerified << L"\n"
                   << L"Verify Failures:     " << pStats->verifyFailures << L"\n"
                   << L"Total Bytes Written: " << (pStats->totalBytesCopied / (1024.0 * 1024.0)) << L" MB (" << pStats->totalBytesCopied << L" bytes)\n\n"
                   << L"Time Taken: " << std::fixed << std::setprecision(2) << (pStats->totalTimeMs / 1000.0) << L" seconds.";

                wchar_t dest[MAX_PATH];
                GetWindowTextW(g_hWndDestEdit, dest, MAX_PATH);
                if (pStats->itemsDeleted > 0) {
                    EnableWindow(g_hWndUndoBtn, TRUE);
                    ss << L"\n\nNote: Pruned items were backed up under '.chrono_backups' (or '.chrono_trash'). Use 'Undo Pruning' to restore the latest backup.";
                } else {
                    UpdateUndoButtonState(dest);
                }

                MessageBoxW(hWnd, ss.str().c_str(), L"ChronoSync Run Summary", MB_OK | MB_ICONINFORMATION);
            }
            break;
        }
        case WM_SYNC_PREVIEW_COMPLETE: {
            g_SyncRunning = false;
            SetControlsState(TRUE);
            SendMessageW(g_hWndProgressBar, PBM_SETPOS, 0, 0);
            SetWindowTextW(g_hWndStatusLabel, L"Ready");

            std::unique_ptr<PreviewLaunchData> launchBundle(reinterpret_cast<PreviewLaunchData*>(lParam));

            wchar_t dest[MAX_PATH];
            GetWindowTextW(g_hWndDestEdit, dest, MAX_PATH);
            UpdateUndoButtonState(dest);

            if (!launchBundle || !launchBundle->pList) {
                break;
            }

            if (launchBundle->hasAnalysis) {
                SetRiskIndicator(launchBundle->analysis);
            }

            if (launchBundle->pList->empty()) {
                MessageBoxW(hWnd, L"No modifications needed. Folders are already in sync.", L"ChronoSync Preview", MB_OK | MB_ICONINFORMATION);
                break;
            }

            RECT rect = {0, 0, 680, 440};
            AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0);

            PreviewLaunchData* rawLaunch = launchBundle.release();
            HWND hwndPreview = CreateWindowExW(
                0, L"ChronoSyncPreviewWindow", L"Sync Preview - ChronoSync",
                WS_OVERLAPPEDWINDOW,
                CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
                hWnd, NULL, GetModuleHandleW(NULL), rawLaunch
            );

            if (hwndPreview) {
                BOOL useDarkMode = TRUE;
                DwmSetWindowAttribute(hwndPreview, 19, &useDarkMode, sizeof(useDarkMode));
                DwmSetWindowAttribute(hwndPreview, 20, &useDarkMode, sizeof(useDarkMode));
                ShowWindow(hwndPreview, SW_SHOW);
            } else {
                delete rawLaunch->pList;
                delete rawLaunch;
                MessageBoxW(hWnd, L"Failed to create Preview window.", L"Error", MB_OK | MB_ICONERROR);
            }
            break;
        }
        case WM_SYNC_ANALYZE_COMPLETE: {
            g_SyncRunning = false;
            SetControlsState(TRUE);
            SendMessageW(g_hWndProgressBar, PBM_SETPOS, 0, 0);
            SetWindowTextW(g_hWndStatusLabel, L"Ready");

            std::unique_ptr<AnalyzeCompleteData> data(reinterpret_cast<AnalyzeCompleteData*>(lParam));
            if (!data) {
                break;
            }

            if (data->hasAnalysis) {
                SetRiskIndicator(data->analysis);
            }

            ShowAnalysisWindow(hWnd, data->report);
            break;
        }
        case WM_SYNC_UNDO_COMPLETE: {
            g_SyncRunning = false;
            SetControlsState(TRUE);
            EnableWindow(g_hWndUndoBtn, FALSE);
            SetWindowTextW(g_hWndStatusLabel, L"Ready");
            SendMessageW(g_hWndProgressBar, PBM_SETPOS, 0, 0);
            MessageBoxW(hWnd, L"Undo complete. Pruned items have been restored successfully.", L"ChronoSync Undo", MB_OK | MB_ICONINFORMATION);
            break;
        }
        case WM_SYNC_QUEUE_COMPLETE: {
            g_SyncRunning = false;
            SetControlsState(TRUE);
            SendMessageW(g_hWndProgressBar, PBM_SETPOS, 0, 0);
            SetWindowTextW(g_hWndStatusLabel, L"Ready");

            size_t totalJobs = static_cast<size_t>(wParam);
            std::unique_ptr<size_t> pCompleted(reinterpret_cast<size_t*>(lParam));
            size_t completed = pCompleted ? *pCompleted : 0;

            std::wstringstream ss;
            ss << L"Queue finished.\n\nJobs run: " << totalJobs << L"\nJobs with changes: " << completed;
            MessageBoxW(hWnd, ss.str().c_str(), L"ChronoSync Queue", MB_OK | MB_ICONINFORMATION);
            break;
        }
        case WM_CLOSE: {
            if (g_SyncRunning) {
                int res = MessageBoxW(hWnd, L"An operation is currently in progress.\nAre you sure you want to close and abort the process?",
                                      L"ChronoSync Warning", MB_YESNO | MB_ICONWARNING);
                if (res != IDYES) {
                    return 0;
                }
            }
            DestroyWindow(hWnd);
            break;
        }
        case WM_DESTROY: {
            if (g_hbrBackground) DeleteObject(g_hbrBackground);
            if (g_hbrEditBackground) DeleteObject(g_hbrEditBackground);
            if (g_hbrInputBackground) DeleteObject(g_hbrInputBackground);
            if (g_hbrLogBackground) DeleteObject(g_hbrLogBackground);
            if (g_hFontNormal) DeleteObject(g_hFontNormal);
            if (g_hFontLabel) DeleteObject(g_hFontLabel);
            if (g_hFontLog) DeleteObject(g_hFontLog);
            PostQuitMessage(0);
            break;
        }
        default:
            return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

bool RegisterMainWindowClass(HINSTANCE hInstance) {
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(UiTheme::WindowBg);
    wc.lpszClassName = L"ChronoSyncMainWindow";
    ApplyAppWindowIcons(wc, hInstance);
    return RegisterClassExW(&wc) != 0;
}

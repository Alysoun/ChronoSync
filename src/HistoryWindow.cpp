#include "HistoryWindow.h"
#include "GuiCommon.h"
#include "SyncHistory.h"
#include "AnalysisWindow.h"

#include <uxtheme.h>
#include <dwmapi.h>
#include <vector>
#include <algorithm>

namespace {
    constexpr int ID_HISTORY_REPORT = 9101;
    constexpr int ID_HISTORY_OLDER_COMBO = 9102;
    constexpr int ID_HISTORY_NEWER_COMBO = 9103;
    constexpr int ID_HISTORY_COMPARE_BTN = 9104;
    constexpr int ID_HISTORY_CLOSE_BTN = 9105;
    constexpr int ID_HISTORY_COPY_BTN = 9106;
    constexpr int ID_HISTORY_SNAPSHOT_NOTE = 9107;
    constexpr int ID_HISTORY_COMPARE_LABEL = 9108;

    constexpr int kMargin = 12;
    constexpr int kBottomPanelH = 88;
    constexpr int kSnapshotNoteH = 44;

    struct HistoryDialogState {
        std::wstring destination;
        std::vector<PrevueSync::SyncHistoryEntry> entries;
        std::vector<size_t> comparableIndices;
    };

    static void UpdateSnapshotCompareUi(HWND hWnd, const std::vector<PrevueSync::SyncHistoryEntry>& entries,
                                      const std::vector<size_t>& comparableIndices) {
        HWND hwndNote = GetDlgItem(hWnd, ID_HISTORY_SNAPSHOT_NOTE);
        const size_t withSnapshot = comparableIndices.size();
        const size_t total = entries.size();
        const size_t cap = PrevueSync::SyncHistoryIO::MaxSnapshotEntries;

        std::wstring note;
        if (total == 0) {
            note = L"Run history is saved under .prevue_history after each sync.";
        } else if (withSnapshot >= 2) {
            note = L"Compare uses per-file snapshots from runs where the destination had at most " +
                   std::to_wstring(cap) + L" items.";
            if (withSnapshot < total) {
                note += L" " + std::to_wstring(total - withSnapshot) +
                        L" run(s) on this folder are stats-only (no snapshot).";
            }
        } else if (withSnapshot == 1) {
            note = L"Compare needs two snapshot runs. Only 1 of " + std::to_wstring(total) +
                   L" run(s) has a snapshot; sync again after the destination is under " +
                   std::to_wstring(cap) + L" items, or use a smaller folder.";
        } else {
            note = L"Compare is unavailable for this destination: all " + std::to_wstring(total) +
                   L" logged run(s) are stats-only because the folder exceeds " + std::to_wstring(cap) +
                   L" items. History still records copies, skips, and timing for each sync.";
        }

        if (hwndNote) {
            SetWindowTextW(hwndNote, note.c_str());
        }

        const BOOL compareReady = withSnapshot >= 2 ? TRUE : FALSE;
        EnableWindow(GetDlgItem(hWnd, ID_HISTORY_COMPARE_BTN), compareReady);
        EnableWindow(GetDlgItem(hWnd, ID_HISTORY_OLDER_COMBO), compareReady);
        EnableWindow(GetDlgItem(hWnd, ID_HISTORY_NEWER_COMBO), compareReady);
    }

    static void LayoutHistoryWindow(HWND hWnd, int cx, int cy) {
        const int contentW = (std::max)(200, cx - 2 * kMargin);
        const int reportH = (std::max)(100, cy - kBottomPanelH - 52 - kSnapshotNoteH - 8);
        const int noteY = kMargin + 22 + reportH + 6;
        const int compareY = noteY + kSnapshotNoteH + 8;
        const int comboY = compareY + 24;
        const int btnY = cy - kMargin - 30;
        const int comboW = (std::max)(120, (contentW - 90) / 2);

        HWND hwndReport = GetDlgItem(hWnd, ID_HISTORY_REPORT);
        if (hwndReport) {
            MoveWindow(hwndReport, kMargin, 32, contentW, reportH, TRUE);
        }

        HWND hwndNote = GetDlgItem(hWnd, ID_HISTORY_SNAPSHOT_NOTE);
        if (hwndNote) {
            MoveWindow(hwndNote, kMargin, noteY, contentW, kSnapshotNoteH, TRUE);
        }

        HWND hwndCompareLabel = GetDlgItem(hWnd, ID_HISTORY_COMPARE_LABEL);
        if (hwndCompareLabel) {
            MoveWindow(hwndCompareLabel, kMargin, compareY, 200, 20, TRUE);
        }

        HWND hwndOlderLabel = FindWindowExW(hWnd, NULL, L"STATIC", L"Older:");
        if (hwndOlderLabel) {
            MoveWindow(hwndOlderLabel, kMargin, comboY + 4, 50, 20, TRUE);
        }
        HWND hwndOlder = GetDlgItem(hWnd, ID_HISTORY_OLDER_COMBO);
        if (hwndOlder) {
            MoveWindow(hwndOlder, kMargin + 48, comboY, comboW, 200, TRUE);
        }

        HWND hwndNewerLabel = FindWindowExW(hWnd, NULL, L"STATIC", L"Newer:");
        if (hwndNewerLabel) {
            MoveWindow(hwndNewerLabel, kMargin + 58 + comboW, comboY + 4, 50, 20, TRUE);
        }
        HWND hwndNewer = GetDlgItem(hWnd, ID_HISTORY_NEWER_COMBO);
        if (hwndNewer) {
            MoveWindow(hwndNewer, kMargin + 108 + comboW, comboY, comboW, 200, TRUE);
        }

        HWND hwndCompare = GetDlgItem(hWnd, ID_HISTORY_COMPARE_BTN);
        if (hwndCompare) {
            MoveWindow(hwndCompare, kMargin, btnY, 100, 30, TRUE);
        }
        HWND hwndCopy = GetDlgItem(hWnd, ID_HISTORY_COPY_BTN);
        if (hwndCopy) {
            MoveWindow(hwndCopy, cx - kMargin - 245, btnY, 100, 30, TRUE);
        }
        HWND hwndClose = GetDlgItem(hWnd, ID_HISTORY_CLOSE_BTN);
        if (hwndClose) {
            MoveWindow(hwndClose, cx - kMargin - 130, btnY, 115, 30, TRUE);
        }
    }

    static void PopulateSnapshotCombos(HWND hWnd, const std::vector<PrevueSync::SyncHistoryEntry>& entries,
                                       std::vector<size_t>& comparableIndices) {
        HWND older = GetDlgItem(hWnd, ID_HISTORY_OLDER_COMBO);
        HWND newer = GetDlgItem(hWnd, ID_HISTORY_NEWER_COMBO);
        SendMessageW(older, CB_RESETCONTENT, 0, 0);
        SendMessageW(newer, CB_RESETCONTENT, 0, 0);
        comparableIndices.clear();

        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].snapshotId.empty()) {
                continue;
            }
            comparableIndices.push_back(i);
            std::wstring label = entries[i].timestampUtc + L" (" + std::to_wstring(entries[i].filesCopied) + L" copied)";
            SendMessageW(older, CB_ADDSTRING, 0, (LPARAM)label.c_str());
            SendMessageW(newer, CB_ADDSTRING, 0, (LPARAM)label.c_str());
        }

        if (!comparableIndices.empty()) {
            SendMessageW(older, CB_SETCURSEL, 0, 0);
            SendMessageW(newer, CB_SETCURSEL, comparableIndices.size() > 1 ? 1 : 0, 0);
        }

        UpdateSnapshotCompareUi(hWnd, entries, comparableIndices);
    }

    static LRESULT CALLBACK HistoryWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
        HistoryDialogState* state = reinterpret_cast<HistoryDialogState*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

        switch (message) {
            case WM_CREATE: {
                state = reinterpret_cast<HistoryDialogState*>(
                    reinterpret_cast<LPCREATESTRUCTW>(lParam)->lpCreateParams);
                SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

                CreateWindowExW(0, L"STATIC", L"Recent sync activity (last 7 days):",
                                WS_CHILD | WS_VISIBLE, kMargin, 10, 400, 20, hWnd, NULL, NULL, NULL);

                HWND hwndReport = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL,
                    kMargin, 32, 560, 260, hWnd, (HMENU)(INT_PTR)ID_HISTORY_REPORT, NULL, NULL);
                SendMessageW(hwndReport, WM_SETFONT, (WPARAM)g_hFontLog, TRUE);
                SetWindowTheme(hwndReport, L"Explorer", nullptr);
                AttachReadOnlyEditCopySupport(hwndReport);

                auto recent = PrevueSync::SyncHistoryIO::QuerySinceDays(state->destination, 7);
                state->entries = recent;
                std::wstring report = PrevueSync::SyncHistoryIO::FormatHistoryReport(recent, 7);
                SetWindowTextW(hwndReport, report.c_str());

                CreateWindowExW(0, L"STATIC", L"",
                                WS_CHILD | WS_VISIBLE,
                                kMargin, 302, 560, kSnapshotNoteH, hWnd,
                                (HMENU)(INT_PTR)ID_HISTORY_SNAPSHOT_NOTE, NULL, NULL);
                SendMessageW(GetDlgItem(hWnd, ID_HISTORY_SNAPSHOT_NOTE), WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

                CreateWindowExW(0, L"STATIC", L"Compare snapshots:",
                                WS_CHILD | WS_VISIBLE, kMargin, 302, 160, 20, hWnd,
                                (HMENU)(INT_PTR)ID_HISTORY_COMPARE_LABEL, NULL, NULL);
                CreateWindowExW(0, L"STATIC", L"Older:",
                                WS_CHILD | WS_VISIBLE, kMargin, 326, 50, 20, hWnd, NULL, NULL, NULL);
                CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                60, 322, 230, 200, hWnd, (HMENU)(INT_PTR)ID_HISTORY_OLDER_COMBO, NULL, NULL);
                CreateWindowExW(0, L"STATIC", L"Newer:",
                                WS_CHILD | WS_VISIBLE, 300, 326, 50, 20, hWnd, NULL, NULL, NULL);
                CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                350, 322, 220, 200, hWnd, (HMENU)(INT_PTR)ID_HISTORY_NEWER_COMBO, NULL, NULL);

                std::wstring error;
                std::vector<PrevueSync::SyncHistoryEntry> allEntries;
                PrevueSync::SyncHistoryIO::LoadEntries(state->destination, allEntries, error);
                if (allEntries.size() < 2) {
                    allEntries = state->entries;
                }
                state->entries = allEntries;
                PopulateSnapshotCombos(hWnd, state->entries, state->comparableIndices);

                CreateWindowExW(0, L"BUTTON", L"Compare", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                kMargin, 358, 100, 30, hWnd, (HMENU)(INT_PTR)ID_HISTORY_COMPARE_BTN, NULL, NULL);
                CreateWindowExW(0, L"BUTTON", L"Copy", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                355, 358, 100, 30, hWnd, (HMENU)(INT_PTR)ID_HISTORY_COPY_BTN, NULL, NULL);
                CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                472, 358, 115, 30, hWnd, (HMENU)(INT_PTR)ID_HISTORY_CLOSE_BTN, NULL, NULL);

                RECT rc = {};
                GetClientRect(hWnd, &rc);
                LayoutHistoryWindow(hWnd, rc.right, rc.bottom);
                break;
            }
            case WM_GETMINMAXINFO: {
                auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
                mmi->ptMinTrackSize.x = 420;
                mmi->ptMinTrackSize.y = 360;
                break;
            }
            case WM_SIZE: {
                if (wParam != SIZE_MINIMIZED) {
                    LayoutHistoryWindow(hWnd, LOWORD(lParam), HIWORD(lParam));
                }
                break;
            }
            case WM_COMMAND: {
                if (!state) {
                    break;
                }
                int wmId = LOWORD(wParam);
                if (wmId == ID_HISTORY_CLOSE_BTN) {
                    DestroyWindow(hWnd);
                } else if (wmId == ID_HISTORY_COPY_BTN) {
                    CopyEditContentToClipboard(GetDlgItem(hWnd, ID_HISTORY_REPORT));
                } else if (wmId == ID_HISTORY_COMPARE_BTN) {
                    if (state->comparableIndices.size() < 2) {
                        MessageBoxW(hWnd,
                                    (L"Compare needs two runs that saved full snapshots.\n\n"
                                     L"This destination has more than " +
                                     std::to_wstring(PrevueSync::SyncHistoryIO::MaxSnapshotEntries) +
                                     L" items, so recent runs are logged with stats only "
                                     L"(copies, skips, bytes, timing) but without per-file snapshot files.")
                                        .c_str(),
                                    L"PrevueSync History", MB_OK | MB_ICONINFORMATION);
                        break;
                    }

                    HWND olderCombo = GetDlgItem(hWnd, ID_HISTORY_OLDER_COMBO);
                    HWND newerCombo = GetDlgItem(hWnd, ID_HISTORY_NEWER_COMBO);
                    int olderIdx = static_cast<int>(SendMessageW(olderCombo, CB_GETCURSEL, 0, 0));
                    int newerIdx = static_cast<int>(SendMessageW(newerCombo, CB_GETCURSEL, 0, 0));
                    if (olderIdx < 0 || newerIdx < 0 ||
                        olderIdx >= static_cast<int>(state->comparableIndices.size()) ||
                        newerIdx >= static_cast<int>(state->comparableIndices.size())) {
                        MessageBoxW(hWnd, L"Select two snapshots to compare.", L"PrevueSync History", MB_OK | MB_ICONINFORMATION);
                        break;
                    }
                    if (olderIdx == newerIdx) {
                        MessageBoxW(hWnd, L"Choose two different snapshots.", L"PrevueSync History", MB_OK | MB_ICONWARNING);
                        break;
                    }
                    if (olderIdx > newerIdx) {
                        std::swap(olderIdx, newerIdx);
                    }

                    const auto& olderEntry = state->entries[state->comparableIndices[static_cast<size_t>(olderIdx)]];
                    const auto& newerEntry = state->entries[state->comparableIndices[static_cast<size_t>(newerIdx)]];
                    std::wstring error;
                    PrevueSync::DestinationSnapshot olderSnap;
                    PrevueSync::DestinationSnapshot newerSnap;
                    if (!PrevueSync::SyncHistoryIO::LoadSnapshot(state->destination, olderEntry.snapshotId, olderSnap, error) ||
                        !PrevueSync::SyncHistoryIO::LoadSnapshot(state->destination, newerEntry.snapshotId, newerSnap, error)) {
                        MessageBoxW(hWnd, error.c_str(), L"PrevueSync History", MB_OK | MB_ICONERROR);
                        break;
                    }

                    auto diff = PrevueSync::SyncHistoryIO::DiffSnapshots(olderSnap, newerSnap);
                    std::wstring report = PrevueSync::SyncHistoryIO::FormatSnapshotDiffReport(
                        diff, olderEntry.timestampUtc, newerEntry.timestampUtc);
                    ShowAnalysisWindow(hWnd, report);
                }
                break;
            }
            case WM_CTLCOLORSTATIC:
            case WM_CTLCOLOREDIT: {
                HDC hdc = (HDC)wParam;
                HWND hwndCtrl = (HWND)lParam;
                if (hwndCtrl == GetDlgItem(hWnd, ID_HISTORY_REPORT)) {
                    SetTextColor(hdc, UiTheme::LogText);
                    SetBkColor(hdc, UiTheme::LogBg);
                    return (INT_PTR)g_hbrLogBackground;
                }
                if (hwndCtrl == GetDlgItem(hWnd, ID_HISTORY_SNAPSHOT_NOTE)) {
                    SetTextColor(hdc, UiTheme::MutedText);
                    SetBkMode(hdc, TRANSPARENT);
                    return (INT_PTR)g_hbrBackground;
                }
                SetTextColor(hdc, UiTheme::LabelText);
                SetBkMode(hdc, TRANSPARENT);
                return (INT_PTR)g_hbrBackground;
            }
            case WM_CLOSE:
                DestroyWindow(hWnd);
                break;
            case WM_DESTROY: {
                HistoryDialogState* data = reinterpret_cast<HistoryDialogState*>(
                    GetWindowLongPtrW(hWnd, GWLP_USERDATA));
                delete data;
                SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
                break;
            }
            default:
                return DefWindowProcW(hWnd, message, wParam, lParam);
        }
        return 0;
    }
}

bool ShowHistoryWindow(HWND parent, const std::wstring& destination) {
    if (destination.empty()) {
        MessageBoxW(parent, L"Select a destination folder first.", L"PrevueSync History", MB_OK | MB_ICONWARNING);
        return false;
    }

    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = HistoryWndProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = g_hbrBackground ? g_hbrBackground : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"PrevueSyncHistoryWindow";
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    auto* state = new HistoryDialogState();
    state->destination = destination;

    EnableWindow(parent, FALSE);
    HWND hwndDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"PrevueSyncHistoryWindow",
        L"Sync History - PrevueSync",
        WindowStyle::ResizableDialog,
        CW_USEDEFAULT, CW_USEDEFAULT, 620, 480,
        parent, NULL, GetModuleHandleW(NULL), state);

    if (!hwndDlg) {
        delete state;
        EnableWindow(parent, TRUE);
        return false;
    }

    BOOL useDarkMode = TRUE;
    DwmSetWindowAttribute(hwndDlg, 19, &useDarkMode, sizeof(useDarkMode));
    DwmSetWindowAttribute(hwndDlg, 20, &useDarkMode, sizeof(useDarkMode));
    ShowWindow(hwndDlg, SW_SHOW);

    MSG msg;
    while (IsWindow(hwndDlg) && GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageW(hwndDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    return true;
}

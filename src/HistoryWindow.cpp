#include "HistoryWindow.h"
#include "GuiCommon.h"
#include "SyncHistory.h"
#include "AnalysisWindow.h"

#include <uxtheme.h>
#include <dwmapi.h>
#include <vector>

namespace {
    constexpr int ID_HISTORY_REPORT = 9101;
    constexpr int ID_HISTORY_OLDER_COMBO = 9102;
    constexpr int ID_HISTORY_NEWER_COMBO = 9103;
    constexpr int ID_HISTORY_COMPARE_BTN = 9104;
    constexpr int ID_HISTORY_CLOSE_BTN = 9105;

    struct HistoryDialogState {
        std::wstring destination;
        std::vector<ChronoSync::SyncHistoryEntry> entries;
    };

    static void PopulateSnapshotCombos(HWND hWnd, const std::vector<ChronoSync::SyncHistoryEntry>& entries) {
        HWND older = GetDlgItem(hWnd, ID_HISTORY_OLDER_COMBO);
        HWND newer = GetDlgItem(hWnd, ID_HISTORY_NEWER_COMBO);
        SendMessageW(older, CB_RESETCONTENT, 0, 0);
        SendMessageW(newer, CB_RESETCONTENT, 0, 0);

        for (const auto& entry : entries) {
            std::wstring label = entry.timestampUtc + L" (" + std::to_wstring(entry.filesCopied) + L" copied)";
            SendMessageW(older, CB_ADDSTRING, 0, (LPARAM)label.c_str());
            SendMessageW(newer, CB_ADDSTRING, 0, (LPARAM)label.c_str());
        }

        if (!entries.empty()) {
            SendMessageW(older, CB_SETCURSEL, 0, 0);
            SendMessageW(newer, CB_SETCURSEL, entries.size() > 1 ? 1 : 0, 0);
        }
    }

    static LRESULT CALLBACK HistoryWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
        HistoryDialogState* state = reinterpret_cast<HistoryDialogState*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

        switch (message) {
            case WM_CREATE: {
                state = reinterpret_cast<HistoryDialogState*>(
                    reinterpret_cast<LPCREATESTRUCTW>(lParam)->lpCreateParams);
                SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

                CreateWindowExW(0, L"STATIC", L"Recent sync activity (last 7 days):",
                                WS_CHILD | WS_VISIBLE, 12, 10, 400, 20, hWnd, NULL, NULL, NULL);

                HWND hwndReport = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL,
                    12, 32, 560, 260, hWnd, (HMENU)(INT_PTR)ID_HISTORY_REPORT, NULL, NULL);
                SendMessageW(hwndReport, WM_SETFONT, (WPARAM)g_hFontLog, TRUE);
                SetWindowTheme(hwndReport, L"Explorer", nullptr);

                auto recent = ChronoSync::SyncHistoryIO::QuerySinceDays(state->destination, 7);
                state->entries = recent;
                std::wstring report = ChronoSync::SyncHistoryIO::FormatHistoryReport(recent, 7);
                SetWindowTextW(hwndReport, report.c_str());

                CreateWindowExW(0, L"STATIC", L"Compare snapshots:",
                                WS_CHILD | WS_VISIBLE, 12, 302, 160, 20, hWnd, NULL, NULL, NULL);
                CreateWindowExW(0, L"STATIC", L"Older:",
                                WS_CHILD | WS_VISIBLE, 12, 326, 50, 20, hWnd, NULL, NULL, NULL);
                CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                60, 322, 230, 200, hWnd, (HMENU)(INT_PTR)ID_HISTORY_OLDER_COMBO, NULL, NULL);
                CreateWindowExW(0, L"STATIC", L"Newer:",
                                WS_CHILD | WS_VISIBLE, 300, 326, 50, 20, hWnd, NULL, NULL, NULL);
                CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                350, 322, 220, 200, hWnd, (HMENU)(INT_PTR)ID_HISTORY_NEWER_COMBO, NULL, NULL);

                std::wstring error;
                std::vector<ChronoSync::SyncHistoryEntry> allEntries;
                ChronoSync::SyncHistoryIO::LoadEntries(state->destination, allEntries, error);
                if (allEntries.size() < 2) {
                    allEntries = state->entries;
                }
                PopulateSnapshotCombos(hWnd, allEntries);
                state->entries = allEntries;

                CreateWindowExW(0, L"BUTTON", L"Compare", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                12, 358, 100, 30, hWnd, (HMENU)(INT_PTR)ID_HISTORY_COMPARE_BTN, NULL, NULL);
                CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                472, 358, 100, 30, hWnd, (HMENU)(INT_PTR)ID_HISTORY_CLOSE_BTN, NULL, NULL);
                break;
            }
            case WM_COMMAND: {
                if (!state) {
                    break;
                }
                int wmId = LOWORD(wParam);
                if (wmId == ID_HISTORY_CLOSE_BTN) {
                    DestroyWindow(hWnd);
                } else if (wmId == ID_HISTORY_COMPARE_BTN) {
                    HWND olderCombo = GetDlgItem(hWnd, ID_HISTORY_OLDER_COMBO);
                    HWND newerCombo = GetDlgItem(hWnd, ID_HISTORY_NEWER_COMBO);
                    int olderIdx = static_cast<int>(SendMessageW(olderCombo, CB_GETCURSEL, 0, 0));
                    int newerIdx = static_cast<int>(SendMessageW(newerCombo, CB_GETCURSEL, 0, 0));
                    if (olderIdx < 0 || newerIdx < 0 ||
                        olderIdx >= static_cast<int>(state->entries.size()) ||
                        newerIdx >= static_cast<int>(state->entries.size())) {
                        MessageBoxW(hWnd, L"Select two snapshots to compare.", L"ChronoSync History", MB_OK | MB_ICONINFORMATION);
                        break;
                    }
                    if (olderIdx == newerIdx) {
                        MessageBoxW(hWnd, L"Choose two different snapshots.", L"ChronoSync History", MB_OK | MB_ICONWARNING);
                        break;
                    }
                    if (olderIdx > newerIdx) {
                        std::swap(olderIdx, newerIdx);
                    }

                    const auto& olderEntry = state->entries[static_cast<size_t>(olderIdx)];
                    const auto& newerEntry = state->entries[static_cast<size_t>(newerIdx)];
                    std::wstring error;
                    ChronoSync::DestinationSnapshot olderSnap;
                    ChronoSync::DestinationSnapshot newerSnap;
                    if (!ChronoSync::SyncHistoryIO::LoadSnapshot(state->destination, olderEntry.snapshotId, olderSnap, error) ||
                        !ChronoSync::SyncHistoryIO::LoadSnapshot(state->destination, newerEntry.snapshotId, newerSnap, error)) {
                        MessageBoxW(hWnd, (L"Failed to load snapshot: " + error).c_str(), L"ChronoSync History", MB_OK | MB_ICONERROR);
                        break;
                    }

                    auto diff = ChronoSync::SyncHistoryIO::DiffSnapshots(olderSnap, newerSnap);
                    std::wstring report = ChronoSync::SyncHistoryIO::FormatSnapshotDiffReport(
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
        MessageBoxW(parent, L"Select a destination folder first.", L"ChronoSync History", MB_OK | MB_ICONWARNING);
        return false;
    }

    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = HistoryWndProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = g_hbrBackground ? g_hbrBackground : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"ChronoSyncHistoryWindow";
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    auto* state = new HistoryDialogState();
    state->destination = destination;

    EnableWindow(parent, FALSE);
    HWND hwndDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"ChronoSyncHistoryWindow",
        L"Sync History - ChronoSync",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 440,
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

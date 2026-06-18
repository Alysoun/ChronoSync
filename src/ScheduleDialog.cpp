#include "ScheduleDialog.h"

#include <dwmapi.h>
#include <windowsx.h>
#include <algorithm>

namespace {
    constexpr int ID_SCHEDULE_PROFILE_EDIT = 9201;
    constexpr int ID_SCHEDULE_COPY_BTN = 9202;

    constexpr int kMargin = 15;

    static void LayoutScheduleWindow(HWND hWnd, int cx, int cy) {
        const int contentW = (std::max)(200, cx - 2 * kMargin);
        const int profileW = (std::max)(120, contentW - 65);
        const int btnY = (std::max)(kMargin, cy - kMargin - 30);

        HWND hwndProfile = GetDlgItem(hWnd, ID_SCHEDULE_PROFILE_EDIT);
        if (hwndProfile) {
            MoveWindow(hwndProfile, 80, 12, profileW, 24, TRUE);
        }
        HWND hwndCopy = GetDlgItem(hWnd, ID_SCHEDULE_COPY_BTN);
        if (hwndCopy) {
            MoveWindow(hwndCopy, cx - kMargin - 70, 10, 70, 28, TRUE);
        }

        HWND hwndType = GetDlgItem(hWnd, ID_SCHEDULE_TYPE_COMBO);
        if (hwndType) {
            MoveWindow(hwndType, 60, 46, 120, 120, TRUE);
        }
        HWND hwndDay = GetDlgItem(hWnd, ID_SCHEDULE_DAY_COMBO);
        if (hwndDay) {
            MoveWindow(hwndDay, 225, 46, (std::max)(80, contentW - 210), 160, TRUE);
        }
        HWND hwndTime = GetDlgItem(hWnd, ID_SCHEDULE_TIME_EDIT);
        if (hwndTime) {
            MoveWindow(hwndTime, 120, 82, 80, 24, TRUE);
        }

        HWND hwndOk = GetDlgItem(hWnd, ID_SCHEDULE_OK);
        if (hwndOk) {
            MoveWindow(hwndOk, cx / 2 - 115, btnY, 100, 30, TRUE);
        }
        HWND hwndCancel = GetDlgItem(hWnd, ID_SCHEDULE_CANCEL);
        if (hwndCancel) {
            MoveWindow(hwndCancel, cx / 2 + 15, btnY, 100, 30, TRUE);
        }
    }
}

static LRESULT CALLBACK ScheduleWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    ScheduleDialogState* state = reinterpret_cast<ScheduleDialogState*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    switch (message) {
        case WM_CREATE: {
            state = reinterpret_cast<ScheduleDialogState*>(
                reinterpret_cast<LPCREATESTRUCTW>(lParam)->lpCreateParams);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

            CreateWindowExW(0, L"STATIC", L"Profile:", WS_CHILD | WS_VISIBLE, kMargin, 15, 60, 20, hWnd, NULL, NULL, NULL);
            HWND hwndProfile = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", state->profilePath.c_str(),
                            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                            80, 12, 250, 24, hWnd, (HMENU)(INT_PTR)ID_SCHEDULE_PROFILE_EDIT, NULL, NULL);
            AttachReadOnlyEditCopySupport(hwndProfile);

            CreateWindowExW(0, L"BUTTON", L"Copy", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                            285, 10, 70, 28, hWnd, (HMENU)(INT_PTR)ID_SCHEDULE_COPY_BTN, NULL, NULL);

            CreateWindowExW(0, L"STATIC", L"Run:", WS_CHILD | WS_VISIBLE, kMargin, 50, 40, 20, hWnd, NULL, NULL, NULL);
            HWND hwndType = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                              60, 46, 120, 120, hWnd, (HMENU)ID_SCHEDULE_TYPE_COMBO, NULL, NULL);
            SendMessageW(hwndType, CB_ADDSTRING, 0, (LPARAM)L"Daily");
            SendMessageW(hwndType, CB_ADDSTRING, 0, (LPARAM)L"Weekly");
            SendMessageW(hwndType, CB_SETCURSEL, 0, 0);

            CreateWindowExW(0, L"STATIC", L"Day:", WS_CHILD | WS_VISIBLE, 190, 50, 35, 20, hWnd, NULL, NULL, NULL);
            HWND hwndDay = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                           225, 46, 100, 160, hWnd, (HMENU)ID_SCHEDULE_DAY_COMBO, NULL, NULL);
            const wchar_t* days[] = { L"MON", L"TUE", L"WED", L"THU", L"FRI", L"SAT", L"SUN" };
            for (const wchar_t* day : days) {
                SendMessageW(hwndDay, CB_ADDSTRING, 0, (LPARAM)day);
            }
            SendMessageW(hwndDay, CB_SETCURSEL, 0, 0);

            CreateWindowExW(0, L"STATIC", L"Time (HH:MM):", WS_CHILD | WS_VISIBLE, kMargin, 85, 100, 20, hWnd, NULL, NULL, NULL);
            CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"02:00",
                            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                            120, 82, 80, 24, hWnd, (HMENU)ID_SCHEDULE_TIME_EDIT, NULL, NULL);

            CreateWindowExW(0, L"BUTTON", L"Create Task", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                            70, 125, 100, 30, hWnd, (HMENU)ID_SCHEDULE_OK, NULL, NULL);
            CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                            185, 125, 100, 30, hWnd, (HMENU)ID_SCHEDULE_CANCEL, NULL, NULL);

            RECT rc = {};
            GetClientRect(hWnd, &rc);
            LayoutScheduleWindow(hWnd, rc.right, rc.bottom);
            break;
        }
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            mmi->ptMinTrackSize.x = 320;
            mmi->ptMinTrackSize.y = 200;
            break;
        }
        case WM_SIZE: {
            if (wParam != SIZE_MINIMIZED) {
                LayoutScheduleWindow(hWnd, LOWORD(lParam), HIWORD(lParam));
            }
            break;
        }
        case WM_COMMAND: {
            if (!state) {
                break;
            }
            int wmId = LOWORD(wParam);
            if (wmId == ID_SCHEDULE_COPY_BTN) {
                CopyEditContentToClipboard(GetDlgItem(hWnd, ID_SCHEDULE_PROFILE_EDIT));
            } else if (wmId == ID_SCHEDULE_OK) {
                HWND hwndType = GetDlgItem(hWnd, ID_SCHEDULE_TYPE_COMBO);
                HWND hwndDay = GetDlgItem(hWnd, ID_SCHEDULE_DAY_COMBO);
                HWND hwndTime = GetDlgItem(hWnd, ID_SCHEDULE_TIME_EDIT);
                state->weekly = (SendMessageW(hwndType, CB_GETCURSEL, 0, 0) == 1);

                wchar_t dayBuf[8] = L"MON";
                SendMessageW(hwndDay, CB_GETLBTEXT, SendMessageW(hwndDay, CB_GETCURSEL, 0, 0), (LPARAM)dayBuf);
                state->dayName = dayBuf;

                wchar_t timeBuf[16] = L"02:00";
                GetWindowTextW(hwndTime, timeBuf, 16);
                std::wstring timeText = timeBuf;
                size_t colon = timeText.find(L':');
                if (colon != std::wstring::npos) {
                    state->hour = _wtoi(timeText.substr(0, colon).c_str());
                    state->minute = _wtoi(timeText.substr(colon + 1).c_str());
                }

                state->confirmed = true;
                DestroyWindow(hWnd);
            } else if (wmId == ID_SCHEDULE_CANCEL) {
                DestroyWindow(hWnd);
            }
            break;
        }
        case WM_CLOSE:
            DestroyWindow(hWnd);
            break;
        case WM_DESTROY:
            break;
        default:
            return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

bool RunScheduleDialog(HWND parent, ScheduleDialogState& state) {
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = ScheduleWndProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = g_hbrBackground ? g_hbrBackground : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"ChronoSyncScheduleWindow";
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    EnableWindow(parent, FALSE);
    HWND hwndDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"ChronoSyncScheduleWindow",
        L"Schedule Sync",
        WindowStyle::ResizableDialog,
        CW_USEDEFAULT, CW_USEDEFAULT, 380, 240,
        parent, NULL, GetModuleHandleW(NULL), &state);

    if (!hwndDlg) {
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
    return state.confirmed;
}

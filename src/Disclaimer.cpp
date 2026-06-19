#include "Disclaimer.h"
#include "GuiCommon.h"

#include <uxtheme.h>
#include <dwmapi.h>
#include <iostream>

#ifdef _MSC_VER
#pragma comment(lib, "advapi32.lib")
#endif

namespace ChronoSync {

namespace {
    constexpr wchar_t kRegKeyPath[] = L"Software\\ChronoSync";
    constexpr wchar_t kRegValueName[] = L"DisclaimerVersionAccepted";

    constexpr int ID_DISCLAIMER_EDIT = 9301;
    constexpr int ID_DISCLAIMER_ACCEPT_BTN = 9302;
    constexpr int ID_DISCLAIMER_EXIT_BTN = 9303;
    constexpr int ID_DISCLAIMER_COPY_BTN = 9304;
    constexpr int ID_DISCLAIMER_CLOSE_BTN = 9305;

    struct DisclaimerDialogMode {
        bool requireAcceptance = false;
        bool accepted = false;
    };

    static std::wstring BuildDisclaimerText() {
        return L"ChronoSync Disclaimer\r\n"
               L"====================\r\n\r\n"
               L"ChronoSync copies, overwrites, and may permanently delete files on your computer, "
               L"especially when options such as \"Prune destination\" are enabled.\r\n\r\n"
               L"ChronoSync is provided as-is, without warranty. The authors and contributors of ChronoSync "
               L"are not responsible for any lost, corrupted, overwritten, or deleted data, or for "
               L"any other damage arising from your use of this software.\r\n\r\n"
               L"ChronoSync is designed to help prevent mistakes by providing Preview, Analyze Plan, "
               L"History, and Versioned Backups.\r\n\r\n"
               L"However, no software can guarantee protection from user error. Always maintain "
               L"independent backups of important data.\r\n\r\n"
               L"You are solely responsible for:\r\n"
               L"  - Backing up important data before syncing\r\n"
               L"  - Verifying source and destination folders\r\n"
               L"  - Reviewing Preview and Analyze Plan before destructive operations\r\n"
               L"  - Understanding sync options you enable\r\n\r\n"
               L"By clicking \"I Understand\" or continuing to use ChronoSync, you agree to these terms.";
    }

    static LRESULT CALLBACK DisclaimerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
        DisclaimerDialogMode* mode = reinterpret_cast<DisclaimerDialogMode*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

        switch (message) {
            case WM_CREATE: {
                mode = reinterpret_cast<DisclaimerDialogMode*>(
                    reinterpret_cast<LPCREATESTRUCTW>(lParam)->lpCreateParams);
                SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(mode));

                HWND hwndEdit = CreateWindowExW(
                    WS_EX_CLIENTEDGE, L"EDIT", BuildDisclaimerText().c_str(),
                    WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL,
                    15, 15, 520, 300, hWnd, (HMENU)(INT_PTR)ID_DISCLAIMER_EDIT, GetModuleHandleW(NULL), NULL);
                SendMessageW(hwndEdit, WM_SETFONT, (WPARAM)g_hFontLog, TRUE);
                SetWindowTheme(hwndEdit, L"Explorer", nullptr);
                AttachReadOnlyEditCopySupport(hwndEdit);

                CreateWindowExW(0, L"BUTTON", L"Copy", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                15, 330, 100, 30, hWnd, (HMENU)(INT_PTR)ID_DISCLAIMER_COPY_BTN,
                                GetModuleHandleW(NULL), NULL);

                if (mode && mode->requireAcceptance) {
                    CreateWindowExW(0, L"BUTTON", L"I Understand", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                    280, 330, 120, 30, hWnd, (HMENU)(INT_PTR)ID_DISCLAIMER_ACCEPT_BTN,
                                    GetModuleHandleW(NULL), NULL);
                    CreateWindowExW(0, L"BUTTON", L"Exit", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                    410, 330, 100, 30, hWnd, (HMENU)(INT_PTR)ID_DISCLAIMER_EXIT_BTN,
                                    GetModuleHandleW(NULL), NULL);
                } else {
                    CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                    410, 330, 100, 30, hWnd, (HMENU)(INT_PTR)ID_DISCLAIMER_CLOSE_BTN,
                                    GetModuleHandleW(NULL), NULL);
                }
                break;
            }
            case WM_SIZE: {
                int cx = LOWORD(lParam);
                int cy = HIWORD(lParam);
                HWND hwndEdit = GetDlgItem(hWnd, ID_DISCLAIMER_EDIT);
                if (hwndEdit) {
                    MoveWindow(hwndEdit, 15, 15, cx - 30, cy - 70, TRUE);
                }
                HWND hwndCopy = GetDlgItem(hWnd, ID_DISCLAIMER_COPY_BTN);
                if (hwndCopy) {
                    MoveWindow(hwndCopy, 15, cy - 45, 100, 30, TRUE);
                }
                HWND hwndAccept = GetDlgItem(hWnd, ID_DISCLAIMER_ACCEPT_BTN);
                if (hwndAccept) {
                    MoveWindow(hwndAccept, cx - 245, cy - 45, 120, 30, TRUE);
                }
                HWND hwndExit = GetDlgItem(hWnd, ID_DISCLAIMER_EXIT_BTN);
                if (hwndExit) {
                    MoveWindow(hwndExit, cx - 115, cy - 45, 100, 30, TRUE);
                }
                HWND hwndClose = GetDlgItem(hWnd, ID_DISCLAIMER_CLOSE_BTN);
                if (hwndClose) {
                    MoveWindow(hwndClose, cx - 115, cy - 45, 100, 30, TRUE);
                }
                break;
            }
            case WM_GETMINMAXINFO: {
                auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
                mmi->ptMinTrackSize.x = 400;
                mmi->ptMinTrackSize.y = 280;
                break;
            }
            case WM_COMMAND: {
                int wmId = LOWORD(wParam);
                if (wmId == ID_DISCLAIMER_COPY_BTN) {
                    CopyEditContentToClipboard(GetDlgItem(hWnd, ID_DISCLAIMER_EDIT));
                } else if (wmId == ID_DISCLAIMER_ACCEPT_BTN) {
                    if (mode) {
                        mode->accepted = true;
                    }
                    DestroyWindow(hWnd);
                } else if (wmId == ID_DISCLAIMER_EXIT_BTN || wmId == ID_DISCLAIMER_CLOSE_BTN) {
                    DestroyWindow(hWnd);
                }
                break;
            }
            case WM_CTLCOLORSTATIC:
            case WM_CTLCOLOREDIT: {
                HDC hdc = (HDC)wParam;
                SetTextColor(hdc, UiTheme::LogText);
                SetBkColor(hdc, UiTheme::LogBg);
                return (INT_PTR)g_hbrLogBackground;
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

    static bool RegisterDisclaimerWindowClass() {
        static bool registered = false;
        if (registered) {
            return true;
        }

        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = DisclaimerWndProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = g_hbrBackground ? g_hbrBackground : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"ChronoSyncDisclaimerWindow";
        if (!RegisterClassExW(&wc)) {
            return false;
        }
        registered = true;
        return true;
    }

    static bool RunDisclaimerDialog(HWND parent, bool requireAcceptance) {
        if (!RegisterDisclaimerWindowClass()) {
            return false;
        }

        DisclaimerDialogMode mode;
        mode.requireAcceptance = requireAcceptance;

        EnableWindow(parent, FALSE);
        HWND hwndDlg = CreateWindowExW(
            WS_EX_DLGMODALFRAME,
            L"ChronoSyncDisclaimerWindow",
            requireAcceptance ? L"ChronoSync - Disclaimer (required)" : L"ChronoSync - Disclaimer",
            WindowStyle::ResizableDialog,
            CW_USEDEFAULT, CW_USEDEFAULT, 560, 420,
            parent, NULL, GetModuleHandleW(NULL), &mode);

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
        return mode.accepted;
    }
}

std::wstring GetDisclaimerText() {
    return BuildDisclaimerText();
}

bool IsDisclaimerAccepted() {
    DWORD version = 0;
    DWORD size = sizeof(version);
    HKEY hKey = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegKeyPath, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, kRegValueName, nullptr, nullptr, reinterpret_cast<LPBYTE>(&version), &size);
        RegCloseKey(hKey);
    }
    return version >= static_cast<DWORD>(DisclaimerVersion);
}

void SetDisclaimerAccepted() {
    HKEY hKey = NULL;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKeyPath, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) ==
        ERROR_SUCCESS) {
        DWORD version = static_cast<DWORD>(DisclaimerVersion);
        RegSetValueExW(hKey, kRegValueName, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&version), sizeof(version));
        RegCloseKey(hKey);
    }
}

bool PromptDisclaimerAcceptance(HWND parent) {
    if (IsDisclaimerAccepted()) {
        return true;
    }
    if (RunDisclaimerDialog(parent, true)) {
        SetDisclaimerAccepted();
        return true;
    }
    return false;
}

void ShowDisclaimerDialog(HWND parent) {
    RunDisclaimerDialog(parent, false);
}

void PrintCliDisclaimer() {
    std::wcerr << L"DISCLAIMER: ChronoSync can overwrite or delete files. You use this software at your own "
                  L"risk; the authors are not responsible for lost or destroyed data."
               << std::endl;
}

} // namespace ChronoSync

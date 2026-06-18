#include "AnalysisWindow.h"

#include <uxtheme.h>
#include <dwmapi.h>

namespace {
    constexpr int ID_ANALYSIS_EDIT = 3001;
    constexpr int ID_ANALYSIS_COPY = 3003;
}

static LRESULT CALLBACK AnalysisWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            auto* report = reinterpret_cast<std::wstring*>(
                reinterpret_cast<LPCREATESTRUCTW>(lParam)->lpCreateParams);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(report));

            HWND hwndEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", report ? report->c_str() : L"",
                                            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL,
                                            15, 15, 600, 400, hWnd, (HMENU)(INT_PTR)ID_ANALYSIS_EDIT, GetModuleHandleW(NULL), NULL);
            SendMessageW(hwndEdit, WM_SETFONT, (WPARAM)g_hFontLog, TRUE);
            SetWindowTheme(hwndEdit, L"Explorer", nullptr);
            AttachReadOnlyEditCopySupport(hwndEdit);

            CreateWindowExW(0, L"BUTTON", L"Copy", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                            370, 425, 115, 30, hWnd, (HMENU)(INT_PTR)ID_ANALYSIS_COPY, GetModuleHandleW(NULL), NULL);
            CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                            500, 425, 115, 30, hWnd, (HMENU)IDCANCEL, GetModuleHandleW(NULL), NULL);
            break;
        }
        case WM_SIZE: {
            int cx = LOWORD(lParam);
            int cy = HIWORD(lParam);
            HWND hwndEdit = GetDlgItem(hWnd, ID_ANALYSIS_EDIT);
            if (hwndEdit) {
                MoveWindow(hwndEdit, 15, 15, cx - 30, cy - 70, TRUE);
            }
            HWND hwndCopy = GetDlgItem(hWnd, ID_ANALYSIS_COPY);
            if (hwndCopy) {
                MoveWindow(hwndCopy, cx - 245, cy - 45, 115, 30, TRUE);
            }
            HWND hwndClose = GetDlgItem(hWnd, IDCANCEL);
            if (hwndClose) {
                MoveWindow(hwndClose, cx - 130, cy - 45, 115, 30, TRUE);
            }
            break;
        }
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            mmi->ptMinTrackSize.x = 360;
            mmi->ptMinTrackSize.y = 240;
            break;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == ID_ANALYSIS_COPY) {
                CopyEditContentToClipboard(GetDlgItem(hWnd, ID_ANALYSIS_EDIT));
            } else if (LOWORD(wParam) == IDCANCEL) {
                DestroyWindow(hWnd);
            }
            break;
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
        case WM_DESTROY: {
            auto* report = reinterpret_cast<std::wstring*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
            delete report;
            break;
        }
        default:
            return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

bool RegisterAnalysisWindowClass(HINSTANCE hInstance) {
    static bool registered = false;
    if (registered) {
        return true;
    }

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = AnalysisWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = g_hbrBackground ? g_hbrBackground : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"ChronoSyncAnalysisWindow";
    if (!RegisterClassExW(&wc)) {
        return false;
    }
    registered = true;
    return true;
}

void ShowAnalysisWindow(HWND parent, const std::wstring& report) {
    HINSTANCE hInstance = GetModuleHandleW(NULL);
    if (!RegisterAnalysisWindowClass(hInstance)) {
        return;
    }

    auto* reportCopy = new std::wstring(report);
    HWND hwndAnalysis = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"ChronoSyncAnalysisWindow",
        L"Plan Analysis - ChronoSync",
        WindowStyle::ResizableDialog,
        CW_USEDEFAULT, CW_USEDEFAULT, 640, 520,
        parent, NULL, hInstance, reportCopy);

    if (hwndAnalysis) {
        BOOL useDarkMode = TRUE;
        DwmSetWindowAttribute(hwndAnalysis, 19, &useDarkMode, sizeof(useDarkMode));
        DwmSetWindowAttribute(hwndAnalysis, 20, &useDarkMode, sizeof(useDarkMode));
        ShowWindow(hwndAnalysis, SW_SHOW);
        (void)parent;
    } else {
        delete reportCopy;
    }
}

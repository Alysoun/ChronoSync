#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>

#include "CliRunner.h"
#include "GuiCommon.h"
#include "MainWindow.h"
#include "PreviewWindow.h"

#ifdef _MSC_VER
#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#endif

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;

    int cliExitCode = 0;
    if (PrevueSync::CliRunner::TryRun(cliExitCode)) {
        return cliExitCode;
    }

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_PROGRESS_CLASS | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icex);

    if (!RegisterMainWindowClass(hInstance)) {
        MessageBoxW(NULL, L"Window Registration Failed!", L"Error!", MB_ICONEXCLAMATION | MB_OK);
        CoUninitialize();
        return 0;
    }

    if (!RegisterPreviewWindowClass(hInstance)) {
        MessageBoxW(NULL, L"Preview Window Registration Failed!", L"Error!", MB_ICONEXCLAMATION | MB_OK);
        CoUninitialize();
        return 0;
    }

    RECT rect = {0, 0, MainLayout::DefaultClientW, MainLayout::DefaultClientH};
    AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0);

    HWND hWnd = CreateWindowExW(
        0,
        L"PrevueSyncMainWindow",
        L"PrevueSync folder synchronizer",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
        NULL, NULL, hInstance, NULL
    );

    if (hWnd == NULL) {
        MessageBoxW(NULL, L"Window Creation Failed!", L"Error!", MB_ICONEXCLAMATION | MB_OK);
        CoUninitialize();
        return 0;
    }

    BOOL useDarkMode = TRUE;
    DwmSetWindowAttribute(hWnd, 19, &useDarkMode, sizeof(useDarkMode));
    DwmSetWindowAttribute(hWnd, 20, &useDarkMode, sizeof(useDarkMode));

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CoUninitialize();
    return static_cast<int>(msg.wParam);
}

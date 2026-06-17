#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <string>
#include <thread>
#include <vector>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <mutex>
#include <memory>
#include <fstream>
#include "SyncEngine.h"

// Embed modern visual styles manifest when compiling with MSVC
#ifdef _MSC_VER
#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#endif

// Define control identifiers
enum ControlIds {
    ID_SRC_EDIT = 101,
    ID_SRC_BROWSE,
    ID_DEST_EDIT,
    ID_DEST_BROWSE,
    ID_PRUNE_CHECKBOX,
    ID_PRUNE_LABEL,
    ID_PREVIEW_BUTTON,
    ID_SYNC_BUTTON,
    ID_PROGRESS_BAR,
    ID_STATUS_LABEL,
    ID_LOG_EDIT,
    ID_UNDO_BUTTON,
    ID_EXPORT_CSV_BUTTON
};

// Define thread communications message IDs
#define WM_SYNC_EVENT               (WM_USER + 10)
#define WM_SYNC_COMPLETE            (WM_USER + 11)
#define WM_SYNC_PREVIEW_COMPLETE    (WM_USER + 12)
#define WM_SYNC_UNDO_COMPLETE       (WM_USER + 13)

// Control handles
HWND g_hWndMain = NULL;
HWND g_hWndSrcEdit = NULL;
HWND g_hWndSrcBrowse = NULL;
HWND g_hWndDestEdit = NULL;
HWND g_hWndDestBrowse = NULL;
HWND g_hWndPruneCheck = NULL;
HWND g_hWndUndoBtn = NULL;
HWND g_hWndPreviewBtn = NULL;
HWND g_hWndSyncBtn = NULL;
HWND g_hWndProgressBar = NULL;
HWND g_hWndStatusLabel = NULL;
HWND g_hWndLogEdit = NULL;

// Brushes for custom dark styling (VS Dark theme)
HBRUSH g_hbrBackground = NULL;
HBRUSH g_hbrEditBackground = NULL;

// Fonts
HFONT g_hFontNormal = NULL;
HFONT g_hFontLog = NULL;

// Execution status
bool g_SyncRunning = false;

// Thread-safe progress and log queue (zero heap allocations in PostMessageW)
struct SyncMessageRegistry {
    std::mutex mtx;
    std::vector<std::wstring> logs;
    std::wstring status;
    int progressPct = 0;
    bool progressChanged = false;
    bool statusChanged = false;

    void PushLog(const std::wstring& line) {
        std::lock_guard<std::mutex> lock(mtx);
        logs.push_back(line);
    }

    void SetStatus(const std::wstring& stat) {
        std::lock_guard<std::mutex> lock(mtx);
        status = stat;
        statusChanged = true;
    }

    void SetProgress(int pct) {
        std::lock_guard<std::mutex> lock(mtx);
        progressPct = pct;
        progressChanged = true;
    }

    bool Drain(std::vector<std::wstring>& outLogs, std::wstring& outStatus, int& outProgress, bool& outStatusChanged, bool& outProgressChanged) {
        std::lock_guard<std::mutex> lock(mtx);
        
        outStatusChanged = statusChanged;
        outProgressChanged = progressChanged;
        
        if (logs.empty() && !statusChanged && !progressChanged) {
            return false; // Queue is empty, exit early to avoid flicker
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
};

SyncMessageRegistry g_MsgRegistry;

// Context structure for resizable and sortable Preview Window
struct PreviewWindowContext {
    std::vector<ChronoSync::PreviewItem>* pList = nullptr;
    HWND hwndLV = NULL;
    HWND lblSummary = NULL;
    HWND btnExport = NULL;
    HWND btnClose = NULL;
    int sortColumn = -1;
    bool sortAscending = true;
};

// Forward declarations
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK PreviewWndProc(HWND, UINT, WPARAM, LPARAM);

// Helper to format bytes to string
std::wstring FormatBytes(unsigned long long bytes) {
    double size = static_cast<double>(bytes);
    int unitIndex = 0;
    const wchar_t* units[] = { L"Bytes", L"KB", L"MB", L"GB", L"TB" };
    while (size >= 1024.0 && unitIndex < 4) {
        size /= 1024.0;
        unitIndex++;
    }
    wchar_t buf[64];
    swprintf_s(buf, L"%.2f %s", size, units[unitIndex]);
    return std::wstring(buf);
}

// Convert wide string to UTF-8
std::string WideToUTF8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], sizeNeeded, NULL, NULL);
    return strTo;
}

// CSV escaping following RFC 4180
std::wstring EscapeCSV(const std::wstring& field) {
    bool needsQuotes = false;
    for (wchar_t c : field) {
        if (c == L',' || c == L'"' || c == L'\n' || c == L'\r') {
            needsQuotes = true;
            break;
        }
    }
    if (!needsQuotes) {
        return field;
    }
    std::wstring escaped = L"\"";
    for (wchar_t c : field) {
        if (c == L'"') {
            escaped += L"\"\"";
        } else {
            escaped += c;
        }
    }
    escaped += L"\"";
    return escaped;
}

// Modern COM file save dialog helper
std::wstring SaveCSVDialog(HWND hWndParent, const wchar_t* title) {
    std::wstring resultPath = L"";
    IFileSaveDialog* pFileSave = nullptr;
    
    HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_ALL, 
                                  IID_IFileSaveDialog, reinterpret_cast<void**>(&pFileSave));
    if (SUCCEEDED(hr)) {
        COMDLG_FILTERSPEC fileTypes[] = {
            { L"CSV Files (*.csv)", L"*.csv" },
            { L"All Files (*.*)", L"*.*" }
        };
        pFileSave->SetFileTypes(2, fileTypes);
        pFileSave->SetDefaultExtension(L"csv");
        pFileSave->SetTitle(title);
        
        hr = pFileSave->Show(hWndParent);
        if (SUCCEEDED(hr)) {
            IShellItem* pItem = nullptr;
            hr = pFileSave->GetResult(&pItem);
            if (SUCCEEDED(hr)) {
                wchar_t* pszFilePath = nullptr;
                hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
                if (SUCCEEDED(hr)) {
                    resultPath = pszFilePath;
                    CoTaskMemFree(pszFilePath);
                }
                pItem->Release();
            }
        }
        pFileSave->Release();
    }
    return resultPath;
}

// Modern COM file browser dialog helper
std::wstring BrowseForFolder(HWND hWndParent, const wchar_t* title) {
    std::wstring resultPath = L"";
    IFileOpenDialog* pFileOpen = nullptr;
    
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, 
                                  IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
    if (SUCCEEDED(hr)) {
        FILEOPENDIALOGOPTIONS options;
        hr = pFileOpen->GetOptions(&options);
        if (SUCCEEDED(hr)) {
            pFileOpen->SetOptions(options | FOS_PICKFOLDERS);
        }
        pFileOpen->SetTitle(title);
        
        hr = pFileOpen->Show(hWndParent);
        if (SUCCEEDED(hr)) {
            IShellItem* pItem = nullptr;
            hr = pFileOpen->GetResult(&pItem);
            if (SUCCEEDED(hr)) {
                wchar_t* pszFilePath = nullptr;
                hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
                if (SUCCEEDED(hr)) {
                    resultPath = pszFilePath;
                    CoTaskMemFree(pszFilePath);
                }
                pItem->Release();
            }
        }
        pFileOpen->Release();
    }
    return resultPath;
}

// Thread state controller
void SetControlsState(BOOL enabled) {
    EnableWindow(g_hWndSrcBrowse, enabled);
    EnableWindow(g_hWndDestBrowse, enabled);
    EnableWindow(g_hWndPruneCheck, enabled);
    EnableWindow(GetDlgItem(g_hWndMain, ID_PRUNE_LABEL), enabled);
    EnableWindow(g_hWndPreviewBtn, enabled);
    EnableWindow(g_hWndSyncBtn, enabled);
    if (!enabled) {
        EnableWindow(g_hWndUndoBtn, FALSE);
    }
}

// Background thread preview runner
void PreviewThreadProc(std::wstring src, std::wstring dest, bool prune) {
    ChronoSync::SyncCallbacks callbacks;
    
    callbacks.onScanStart = [](const std::wstring& rootDir) {
        g_MsgRegistry.SetStatus(L"Scanning: " + rootDir);
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };
    callbacks.onScanComplete = [](size_t totalItems) {
        g_MsgRegistry.PushLog(L"Scan complete. Found " + std::to_wstring(totalItems) + L" items.");
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };
    callbacks.onCompareStart = []() {
        g_MsgRegistry.SetStatus(L"Comparing structures for preview...");
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };
    callbacks.onCompareComplete = [](size_t dirsToCreate, size_t filesToCopy, size_t itemsToDelete) {
        std::wstring plan = L"Plan: Create " + std::to_wstring(dirsToCreate) + L" dirs, Copy " + std::to_wstring(filesToCopy) + L" files, Prune " + std::to_wstring(itemsToDelete) + L" items.";
        g_MsgRegistry.PushLog(plan);
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };
    callbacks.onLog = [](const std::wstring& message, bool isError) {
        std::wstring prefix = isError ? L"[ERROR] " : L"[INFO] ";
        g_MsgRegistry.PushLog(prefix + message);
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };

    auto list = ChronoSync::SyncEngine::Preview(src, dest, prune, callbacks);
    
    std::vector<ChronoSync::PreviewItem>* pList = new std::vector<ChronoSync::PreviewItem>(std::move(list));
    PostMessageW(g_hWndMain, WM_SYNC_PREVIEW_COMPLETE, 0, (LPARAM)pList);
}

// Background thread sync runner
void SyncThreadProc(std::wstring src, std::wstring dest, bool prune) {
    ChronoSync::SyncCallbacks callbacks;
    
    callbacks.onScanStart = [](const std::wstring& rootDir) {
        g_MsgRegistry.SetStatus(L"Scanning: " + rootDir);
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };

    callbacks.onScanDir = [](const std::wstring& subDir) {
        (void)subDir;
    };

    callbacks.onScanComplete = [](size_t totalItems) {
        g_MsgRegistry.PushLog(L"Scan complete. Found " + std::to_wstring(totalItems) + L" items.");
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };

    callbacks.onCompareStart = []() {
        g_MsgRegistry.SetStatus(L"Comparing directory structures...");
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };

    callbacks.onCompareComplete = [](size_t dirsToCreate, size_t filesToCopy, size_t itemsToDelete) {
        std::wstring plan = L"Plan: Create " + std::to_wstring(dirsToCreate) + L" dirs, Copy " + std::to_wstring(filesToCopy) + L" files, Prune " + std::to_wstring(itemsToDelete) + L" items.";
        g_MsgRegistry.PushLog(plan);
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };

    callbacks.onCopyStart = [](const std::wstring& relPath, unsigned long long fileSizeBytes, size_t fileIndex, size_t totalFiles) {
        g_MsgRegistry.SetStatus(L"[" + std::to_wstring(fileIndex) + L"/" + std::to_wstring(totalFiles) + L"] Syncing: " + relPath);
        g_MsgRegistry.PushLog(L"Syncing: " + relPath + L" (" + std::to_wstring(fileSizeBytes / 1024) + L" KB)");
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };

    callbacks.onCopyProgress = [](unsigned long long bytesCopied, unsigned long long fileSizeBytes) {
        if (fileSizeBytes > 0) {
            int pct = static_cast<int>(bytesCopied * 100 / fileSizeBytes);
            g_MsgRegistry.SetProgress(pct);
            PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
        }
    };

    callbacks.onCopyComplete = [](const std::wstring& relPath, bool success, const std::wstring& errorMessage) {
        if (success) {
            g_MsgRegistry.PushLog(L"  ✔ Success: " + relPath);
        } else {
            g_MsgRegistry.PushLog(L"  ✘ Failed: " + relPath + L" - " + errorMessage);
        }
        g_MsgRegistry.SetProgress(0);
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };

    callbacks.onDeleteItem = [](const std::wstring& relPath, bool isDirectory) {
        std::wstring typeStr = isDirectory ? L"directory" : L"file";
        g_MsgRegistry.PushLog(L"[PRUNE] Archiving " + typeStr + L" to trash: " + relPath);
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };

    callbacks.onDeleteFailed = [](const std::wstring& relPath, const std::wstring& errorMessage) {
        g_MsgRegistry.PushLog(L"[PRUNE] Archive failed: " + relPath + L" (" + errorMessage + L")");
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };

    callbacks.onLog = [](const std::wstring& message, bool isError) {
        std::wstring prefix = isError ? L"[ERROR] " : L"[INFO] ";
        g_MsgRegistry.PushLog(prefix + message);
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };

    // Run synchronization
    ChronoSync::SyncStats* pStats = new ChronoSync::SyncStats();
    *pStats = ChronoSync::SyncEngine::Sync(src, dest, prune, callbacks);

    PostMessageW(g_hWndMain, WM_SYNC_COMPLETE, 1, (LPARAM)pStats);
}

// Background thread undo runner
void UndoThreadProc(std::wstring dest) {
    ChronoSync::SyncCallbacks callbacks;
    callbacks.onLog = [](const std::wstring& message, bool isError) {
        std::wstring prefix = isError ? L"[ERROR] " : L"[INFO] ";
        g_MsgRegistry.PushLog(prefix + message);
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };
    callbacks.onScanStart = [](const std::wstring& rootDir) {
        g_MsgRegistry.SetStatus(L"Scanning trash: " + rootDir);
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };

    ChronoSync::SyncEngine::UndoPruning(dest, callbacks);
    PostMessageW(g_hWndMain, WM_SYNC_UNDO_COMPLETE, 0, 0);
}

// Child control creator helper
void CreateControls(HWND hWnd, HINSTANCE hInstance) {
    // Labels (Using bright white text color configured in WM_CTLCOLORSTATIC)
    HWND lblSrc = CreateWindowExW(0, L"STATIC", L"Source Folder:", WS_CHILD | WS_VISIBLE, 
                                  20, 15, 120, 20, hWnd, NULL, hInstance, NULL);
    g_hWndSrcEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY, 
                                    20, 38, 470, 24, hWnd, (HMENU)ID_SRC_EDIT, hInstance, NULL);
    g_hWndSrcBrowse = CreateWindowExW(0, L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 
                                      500, 37, 100, 26, hWnd, (HMENU)ID_SRC_BROWSE, hInstance, NULL);

    HWND lblDest = CreateWindowExW(0, L"STATIC", L"Destination Folder:", WS_CHILD | WS_VISIBLE, 
                                   20, 75, 150, 20, hWnd, NULL, hInstance, NULL);
    g_hWndDestEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY, 
                                     20, 98, 470, 24, hWnd, (HMENU)ID_DEST_EDIT, hInstance, NULL);
    g_hWndDestBrowse = CreateWindowExW(0, L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 
                                       500, 97, 100, 26, hWnd, (HMENU)ID_DEST_BROWSE, hInstance, NULL);

    // Checkbox and static label overrides
    g_hWndPruneCheck = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 
                                       20, 135, 20, 20, hWnd, (HMENU)ID_PRUNE_CHECKBOX, hInstance, NULL);
    HWND lblPrune = CreateWindowExW(0, L"STATIC", L"Prune destination (delete files not in source)", 
                                    WS_CHILD | WS_VISIBLE | SS_NOTIFY, 
                                    45, 136, 440, 20, hWnd, (HMENU)ID_PRUNE_LABEL, hInstance, NULL);

    g_hWndUndoBtn = CreateWindowExW(0, L"BUTTON", L"Undo Pruning", WS_CHILD | WS_VISIBLE | WS_DISABLED | BS_PUSHBUTTON,
                                    500, 131, 100, 26, hWnd, (HMENU)ID_UNDO_BUTTON, hInstance, NULL);

    // Split buttons layout
    g_hWndPreviewBtn = CreateWindowExW(0, L"BUTTON", L"Preview Changes", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 
                                       20, 168, 280, 36, hWnd, (HMENU)ID_PREVIEW_BUTTON, hInstance, NULL);
    g_hWndSyncBtn = CreateWindowExW(0, L"BUTTON", L"Sync Now", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 
                                    320, 168, 280, 36, hWnd, (HMENU)ID_SYNC_BUTTON, hInstance, NULL);

    g_hWndStatusLabel = CreateWindowExW(0, L"STATIC", L"Ready", WS_CHILD | WS_VISIBLE, 
                                        20, 218, 580, 20, hWnd, (HMENU)ID_STATUS_LABEL, hInstance, NULL);

    g_hWndProgressBar = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE, 
                                        20, 238, 580, 20, hWnd, (HMENU)ID_PROGRESS_BAR, hInstance, NULL);

    HWND lblLog = CreateWindowExW(0, L"STATIC", L"Operation Log:", WS_CHILD | WS_VISIBLE, 
                                  20, 275, 120, 20, hWnd, NULL, hInstance, NULL);

    g_hWndLogEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", 
                                    WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL, 
                                    20, 298, 580, 190, hWnd, (HMENU)ID_LOG_EDIT, hInstance, NULL);

    // Apply fonts
    SendMessageW(lblSrc, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
    SendMessageW(g_hWndSrcEdit, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
    SendMessageW(g_hWndSrcBrowse, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
    SendMessageW(lblDest, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
    SendMessageW(g_hWndDestEdit, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
    SendMessageW(g_hWndDestBrowse, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
    SendMessageW(g_hWndPruneCheck, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
    SendMessageW(lblPrune, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
    SendMessageW(g_hWndUndoBtn, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
    SendMessageW(g_hWndPreviewBtn, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
    SendMessageW(g_hWndSyncBtn, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
    SendMessageW(g_hWndStatusLabel, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
    SendMessageW(lblLog, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
    SendMessageW(g_hWndLogEdit, WM_SETFONT, (WPARAM)g_hFontLog, TRUE);
}

// Window procedure message handler
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            g_hWndMain = hWnd;
            
            // Background styling brushes (VS Dark Panel style)
            g_hbrBackground = CreateSolidBrush(RGB(45, 45, 48));
            g_hbrEditBackground = CreateSolidBrush(RGB(30, 30, 30));

            // Create modern text fonts
            g_hFontNormal = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
                                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                       DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontLog = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
                                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                    DEFAULT_PITCH | FF_DONTCARE, L"Consolas");

            CreateControls(hWnd, ((LPCREATESTRUCT)lParam)->hInstance);
            break;
        }
        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            
            // Checkbox label click trigger
            if (wmId == ID_PRUNE_LABEL) {
                LRESULT state = SendMessageW(g_hWndPruneCheck, BM_GETCHECK, 0, 0);
                SendMessageW(g_hWndPruneCheck, BM_SETCHECK, state == BST_CHECKED ? BST_UNCHECKED : BST_CHECKED, 0);
                break;
            }

            if (wmId == ID_SRC_BROWSE) {
                std::wstring path = BrowseForFolder(hWnd, L"Select Source Folder");
                if (!path.empty()) {
                    SetWindowTextW(g_hWndSrcEdit, path.c_str());
                }
            } else if (wmId == ID_DEST_BROWSE) {
                std::wstring path = BrowseForFolder(hWnd, L"Select Destination Folder");
                if (!path.empty()) {
                    SetWindowTextW(g_hWndDestEdit, path.c_str());
                    
                    // Check if .chrono_trash exists to enable the Undo button
                    std::wstring trashPath = path + L"\\.chrono_trash";
                    if (std::filesystem::exists(trashPath)) {
                        EnableWindow(g_hWndUndoBtn, TRUE);
                    } else {
                        EnableWindow(g_hWndUndoBtn, FALSE);
                    }
                }
            } else if (wmId == ID_UNDO_BUTTON) {
                wchar_t dest[MAX_PATH];
                GetWindowTextW(g_hWndDestEdit, dest, MAX_PATH);
                std::wstring destPath(dest);

                if (destPath.empty()) {
                    break;
                }

                SetControlsState(FALSE);
                EnableWindow(g_hWndUndoBtn, FALSE);
                SetWindowTextW(g_hWndLogEdit, L"");

                // Start Undo on background thread
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

                bool prune = (SendMessageW(g_hWndPruneCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);

                SetControlsState(FALSE);
                SetWindowTextW(g_hWndLogEdit, L"");

                // Spawn background preview thread
                g_SyncRunning = true;
                std::thread t(PreviewThreadProc, sourcePath, destPath, prune);
                t.detach();
            } else if (wmId == ID_SYNC_BUTTON) {
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

                bool prune = (SendMessageW(g_hWndPruneCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);

                SetControlsState(FALSE);
                SetWindowTextW(g_hWndLogEdit, L"");

                // Start sync on background thread
                g_SyncRunning = true;
                std::thread t(SyncThreadProc, sourcePath, destPath, prune);
                t.detach();
            }
            break;
        }
        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wParam;
            SetTextColor(hdcStatic, RGB(255, 255, 255)); // High contrast bright white text
            SetBkMode(hdcStatic, TRANSPARENT);
            return (INT_PTR)g_hbrBackground;
        }
        case WM_CTLCOLOREDIT: {
            HDC hdcEdit = (HDC)wParam;
            SetTextColor(hdcEdit, RGB(240, 240, 240));
            SetBkColor(hdcEdit, RGB(30, 30, 30));
            return (INT_PTR)g_hbrEditBackground;
        }
        case WM_SYNC_EVENT: {
            std::vector<std::wstring> drainedLogs;
            std::wstring drainedStatus;
            int drainedProgress = 0;
            bool statusChanged = false;
            bool progressChanged = false;

            // Drain message registry safely (Locks and exits early if empty)
            if (!g_MsgRegistry.Drain(drainedLogs, drainedStatus, drainedProgress, statusChanged, progressChanged)) {
                break;
            }

            // Append logs under a temporary redrawing lock to prevent flickering
            if (!drainedLogs.empty()) {
                SendMessageW(g_hWndLogEdit, WM_SETREDRAW, FALSE, 0);
                for (const auto& log : drainedLogs) {
                    int len = GetWindowTextLengthW(g_hWndLogEdit);
                    SendMessageW(g_hWndLogEdit, EM_SETSEL, len, len);
                    SendMessageW(g_hWndLogEdit, EM_REPLACESEL, FALSE, (LPARAM)log.c_str());
                    SendMessageW(g_hWndLogEdit, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n");
                }
                SendMessageW(g_hWndLogEdit, WM_SETREDRAW, TRUE, 0);
                InvalidateRect(g_hWndLogEdit, NULL, TRUE);
            }

            if (statusChanged) {
                SetWindowTextW(g_hWndStatusLabel, drainedStatus.c_str());
            }

            if (progressChanged) {
                SendMessageW(g_hWndProgressBar, PBM_SETPOS, drainedProgress, 0);
            }
            break;
        }
        case WM_SYNC_COMPLETE: {
            g_SyncRunning = false;
            SetControlsState(TRUE); // Re-enable action buttons
            
            SendMessageW(g_hWndProgressBar, PBM_SETPOS, 0, 0);
            SetWindowTextW(g_hWndStatusLabel, L"Ready");

            // Wrap in unique_ptr to guarantee safe memory release
            std::unique_ptr<ChronoSync::SyncStats> pStats((ChronoSync::SyncStats*)lParam);
            if (pStats) {
                std::wstringstream ss;
                ss << L"Synchronization Completed successfully!\n\n"
                   << L"Directories Created: " << pStats->dirsCreated << L"\n"
                   << L"Files Transferred:   " << pStats->filesCopied << L"\n"
                   << L"Files Skipped:       " << pStats->filesSkipped << L"\n"
                   << L"Items Pruned/Backed: " << pStats->itemsDeleted << L"\n"
                   << L"Total Bytes Written: " << (pStats->totalBytesCopied / (1024.0 * 1024.0)) << L" MB (" << pStats->totalBytesCopied << L" bytes)\n\n"
                   << L"Time Taken: " << std::fixed << std::setprecision(2) << (pStats->totalTimeMs / 1000.0) << L" seconds.";
                
                if (pStats->itemsDeleted > 0) {
                    EnableWindow(g_hWndUndoBtn, TRUE);
                    ss << L"\n\nNote: Deleted items have been backed up in '.chrono_trash'. You can restore them by clicking 'Undo Pruning'.";
                } else {
                    // Check if trash still exists in case itemsDeleted == 0 but prior folder resides
                    wchar_t dest[MAX_PATH];
                    GetWindowTextW(g_hWndDestEdit, dest, MAX_PATH);
                    std::wstring trashPath = std::wstring(dest) + L"\\.chrono_trash";
                    EnableWindow(g_hWndUndoBtn, std::filesystem::exists(trashPath) ? TRUE : FALSE);
                }

                MessageBoxW(hWnd, ss.str().c_str(), L"ChronoSync Run Summary", MB_OK | MB_ICONINFORMATION);
            }
            break;
        }
        case WM_SYNC_PREVIEW_COMPLETE: {
            g_SyncRunning = false;
            SetControlsState(TRUE); // Re-enable action buttons
            SendMessageW(g_hWndProgressBar, PBM_SETPOS, 0, 0);
            SetWindowTextW(g_hWndStatusLabel, L"Ready");

            // Wrap in unique_ptr to prevent leaks if CreateWindowExW fails
            std::unique_ptr<std::vector<ChronoSync::PreviewItem>> pList((std::vector<ChronoSync::PreviewItem>*)lParam);

            // Re-evaluate undo button availability
            wchar_t dest[MAX_PATH];
            GetWindowTextW(g_hWndDestEdit, dest, MAX_PATH);
            std::wstring trashPath = std::wstring(dest) + L"\\.chrono_trash";
            EnableWindow(g_hWndUndoBtn, std::filesystem::exists(trashPath) ? TRUE : FALSE);

            if (pList) {
                if (pList->empty()) {
                    MessageBoxW(hWnd, L"No modifications needed. Folders are already in sync.", L"ChronoSync Preview", MB_OK | MB_ICONINFORMATION);
                } else {
                    RECT rect = {0, 0, 680, 440};
                    AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0);

                    // Pass raw pointer but retain unique_ptr ownership checks
                    HWND hwndPreview = CreateWindowExW(
                        0, L"ChronoSyncPreviewWindow", L"Sync Preview - ChronoSync",
                        WS_OVERLAPPEDWINDOW,
                        CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
                        hWnd, NULL, GetModuleHandleW(NULL), pList.get()
                    );
                    
                    if (hwndPreview) {
                        // Successful window creation, release unique_ptr control
                        pList.release();

                        // Style secondary window with DWM Immersive Dark Mode
                        BOOL useDarkMode = TRUE;
                        DwmSetWindowAttribute(hwndPreview, 19, &useDarkMode, sizeof(useDarkMode));
                        DwmSetWindowAttribute(hwndPreview, 20, &useDarkMode, sizeof(useDarkMode));

                        ShowWindow(hwndPreview, SW_SHOW);
                    } else {
                        // Automatically cleans up pList if CreateWindowExW failed
                        MessageBoxW(hWnd, L"Failed to create Preview window.", L"Error", MB_OK | MB_ICONERROR);
                    }
                }
            }
            break;
        }
        case WM_SYNC_UNDO_COMPLETE: {
            g_SyncRunning = false;
            SetControlsState(TRUE);
            EnableWindow(g_hWndUndoBtn, FALSE); // Disabled since trash is now empty
            SetWindowTextW(g_hWndStatusLabel, L"Ready");
            SendMessageW(g_hWndProgressBar, PBM_SETPOS, 0, 0);
            MessageBoxW(hWnd, L"Undo complete. Pruned items have been restored successfully.", L"ChronoSync Undo", MB_OK | MB_ICONINFORMATION);
            break;
        }
        case WM_CLOSE: {
            if (g_SyncRunning) {
                int res = MessageBoxW(hWnd, L"An operation is currently in progress.\nAre you sure you want to close and abort the process?", 
                                      L"ChronoSync Warning", MB_YESNO | MB_ICONWARNING);
                if (res != IDYES) {
                    return 0; // Abort close
                }
            }
            DestroyWindow(hWnd);
            break;
        }
        case WM_DESTROY: {
            if (g_hbrBackground) DeleteObject(g_hbrBackground);
            if (g_hbrEditBackground) DeleteObject(g_hbrEditBackground);
            if (g_hFontNormal) DeleteObject(g_hFontNormal);
            if (g_hFontLog) DeleteObject(g_hFontLog);
            PostQuitMessage(0);
            break;
        }
        default:
            return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Preview Window Procedure (High performance Virtual ListView LVS_OWNERDATA)
LRESULT CALLBACK PreviewWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            std::vector<ChronoSync::PreviewItem>* pList = 
                (std::vector<ChronoSync::PreviewItem>*)((LPCREATESTRUCTW)lParam)->lpCreateParams;

            PreviewWindowContext* ctx = new PreviewWindowContext();
            ctx->pList = pList;
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)ctx);

            // Create list view child control using LVS_OWNERDATA for instant loading
            ctx->hwndLV = CreateWindowExW(
                0, WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_OWNERDATA | WS_BORDER | WS_VSCROLL,
                15, 15, 650, 360,
                hWnd, (HMENU)201, GetModuleHandleW(NULL), NULL
            );

            // Apply font
            SendMessageW(ctx->hwndLV, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

            // Add columns
            LVCOLUMNW col = {0};
            col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            
            col.pszText = const_cast<LPWSTR>(L"Relative Path");
            col.cx = 360;
            col.iSubItem = 0;
            ListView_InsertColumn(ctx->hwndLV, 0, &col);

            col.pszText = const_cast<LPWSTR>(L"Planned Action");
            col.cx = 140;
            col.iSubItem = 1;
            ListView_InsertColumn(ctx->hwndLV, 1, &col);

            col.pszText = const_cast<LPWSTR>(L"Size");
            col.cx = 120;
            col.iSubItem = 2;
            ListView_InsertColumn(ctx->hwndLV, 2, &col);

            // Set grid styles and LVS_EX_DOUBLEBUFFER to prevent flickering
            SendMessageW(ctx->hwndLV, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

            // Style ListView Dark Theme
            ListView_SetBkColor(ctx->hwndLV, RGB(30, 30, 30));
            ListView_SetTextBkColor(ctx->hwndLV, RGB(30, 30, 30));
            ListView_SetTextColor(ctx->hwndLV, RGB(240, 240, 240));

            // Summary label
            ctx->lblSummary = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                              15, 395, 450, 20, hWnd, (HMENU)202, GetModuleHandleW(NULL), NULL);
            SendMessageW(ctx->lblSummary, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

            if (pList) {
                std::wstring summaryText = L"Total planned changes: " + std::to_wstring(pList->size()) + L" items.";
                SetWindowTextW(ctx->lblSummary, summaryText.c_str());

                // Set item count wrapped in WM_SETREDRAW safety blocks to avoid flicker
                SendMessageW(ctx->hwndLV, WM_SETREDRAW, FALSE, 0);
                SendMessageW(ctx->hwndLV, LVM_SETITEMCOUNT, (WPARAM)pList->size(), (LPARAM)LVSICF_NOINVALIDATEALL);
                SendMessageW(ctx->hwndLV, WM_SETREDRAW, TRUE, 0);
                InvalidateRect(ctx->hwndLV, NULL, TRUE);
            }

            // Export CSV button (placed symmetrically to the left of the Close button with 10px gap)
            ctx->btnExport = CreateWindowExW(0, L"BUTTON", L"Export CSV...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                             425, 390, 115, 30, hWnd, (HMENU)ID_EXPORT_CSV_BUTTON, GetModuleHandleW(NULL), NULL);
            SendMessageW(ctx->btnExport, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

            // Close button
            ctx->btnClose = CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                             550, 390, 115, 30, hWnd, (HMENU)IDCANCEL, GetModuleHandleW(NULL), NULL);
            SendMessageW(ctx->btnClose, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
            break;
        }
        case WM_SIZE: {
            PreviewWindowContext* ctx = 
                (PreviewWindowContext*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
            if (ctx) {
                int cx = LOWORD(lParam);
                int cy = HIWORD(lParam);

                // Resize ListView
                if (ctx->hwndLV) {
                    MoveWindow(ctx->hwndLV, 15, 15, cx - 30, cy - 70, TRUE);
                }
                // Reposition Summary label
                if (ctx->lblSummary) {
                    MoveWindow(ctx->lblSummary, 15, cy - 40, cx - 270, 20, TRUE);
                }
                // Reposition Export CSV button
                if (ctx->btnExport) {
                    MoveWindow(ctx->btnExport, cx - 255, cy - 45, 115, 30, TRUE);
                }
                // Reposition Close button
                if (ctx->btnClose) {
                    MoveWindow(ctx->btnClose, cx - 130, cy - 45, 115, 30, TRUE);
                }
            }
            break;
        }
        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wParam;
            SetTextColor(hdcStatic, RGB(255, 255, 255));
            SetBkMode(hdcStatic, TRANSPARENT);
            return (INT_PTR)g_hbrBackground;
        }
        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            if (wmId == IDCANCEL) {
                DestroyWindow(hWnd);
            } else if (wmId == ID_EXPORT_CSV_BUTTON) {
                PreviewWindowContext* ctx = 
                    (PreviewWindowContext*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
                if (ctx && ctx->pList && !ctx->pList->empty()) {
                    std::wstring selectedPath = SaveCSVDialog(hWnd, L"Export Preview to CSV");
                    if (!selectedPath.empty()) {
                        std::ofstream out(selectedPath, std::ios::binary);
                        if (out) {
                            // Write UTF-8 BOM for seamless Excel compatibility
                            out << "\xEF\xBB\xBF";
                            
                            // Write CSV header (Relative Path, Planned Action, Size (Bytes), Formatted Size)
                            out << "Relative Path,Planned Action,Size (Bytes),Formatted Size\n";
                            
                            for (const auto& item : *ctx->pList) {
                                std::wstring escapedPath = EscapeCSV(item.relativePath);
                                std::wstring escapedAction = EscapeCSV(item.action);
                                std::wstring rawSizeStr = std::to_wstring(item.fileSize);
                                std::wstring escapedSize = EscapeCSV(item.sizeStr);
                                
                                std::string utf8Line = WideToUTF8(escapedPath + L"," + escapedAction + L"," + rawSizeStr + L"," + escapedSize + L"\n");
                                out.write(utf8Line.data(), utf8Line.size());
                            }
                            out.close();
                            MessageBoxW(hWnd, L"Preview items exported successfully!", L"Export Complete", MB_OK | MB_ICONINFORMATION);
                        } else {
                            MessageBoxW(hWnd, L"Failed to create CSV file.", L"Error", MB_OK | MB_ICONERROR);
                        }
                    }
                } else {
                    MessageBoxW(hWnd, L"No preview items to export.", L"Export Preview", MB_OK | MB_ICONWARNING);
                }
            }
            break;
        }
        case WM_NOTIFY: {
            LPNMHDR pnmh = (LPNMHDR)lParam;
            PreviewWindowContext* ctx = 
                (PreviewWindowContext*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

            if (pnmh->code == LVN_GETDISPINFOW) {
                NMLVDISPINFOW* plvdi = (NMLVDISPINFOW*)lParam;
                if (ctx && ctx->pList && plvdi->item.iItem < (int)ctx->pList->size()) {
                    const auto& item = (*ctx->pList)[plvdi->item.iItem];
                    if (plvdi->item.mask & LVIF_TEXT) {
                        if (plvdi->item.iSubItem == 0) {
                            plvdi->item.pszText = const_cast<wchar_t*>(item.relativePath.c_str());
                        } else if (plvdi->item.iSubItem == 1) {
                            plvdi->item.pszText = const_cast<wchar_t*>(item.action.c_str());
                        } else if (plvdi->item.iSubItem == 2) {
                            plvdi->item.pszText = const_cast<wchar_t*>(item.sizeStr.c_str());
                        }
                    }
                }
            } else if (pnmh->code == LVN_COLUMNCLICK) {
                LPNMLISTVIEW pnmlv = (LPNMLISTVIEW)lParam;
                if (ctx && ctx->pList && !ctx->pList->empty()) {
                    int col = pnmlv->iSubItem;
                    if (ctx->sortColumn == col) {
                        ctx->sortAscending = !ctx->sortAscending;
                    } else {
                        ctx->sortColumn = col;
                        ctx->sortAscending = true;
                    }

                    bool asc = ctx->sortAscending;
                    if (col == 0) {
                        // Sort by Relative Path (alphabetical)
                        std::sort(ctx->pList->begin(), ctx->pList->end(), [asc](const ChronoSync::PreviewItem& a, const ChronoSync::PreviewItem& b) {
                            return asc ? (a.relativePath < b.relativePath) : (a.relativePath > b.relativePath);
                        });
                    } else if (col == 1) {
                        // Sort by Planned Action (alphabetical)
                        std::sort(ctx->pList->begin(), ctx->pList->end(), [asc](const ChronoSync::PreviewItem& a, const ChronoSync::PreviewItem& b) {
                            if (a.action != b.action) {
                                return asc ? (a.action < b.action) : (a.action > b.action);
                            }
                            return asc ? (a.relativePath < b.relativePath) : (a.relativePath > b.relativePath);
                        });
                    } else if (col == 2) {
                        // Sort by Size (numeric fileSize)
                        std::sort(ctx->pList->begin(), ctx->pList->end(), [asc](const ChronoSync::PreviewItem& a, const ChronoSync::PreviewItem& b) {
                            if (a.fileSize != b.fileSize) {
                                return asc ? (a.fileSize < b.fileSize) : (a.fileSize > b.fileSize);
                            }
                            return asc ? (a.relativePath < b.relativePath) : (a.relativePath > b.relativePath);
                        });
                    }

                    // Force ListView to refresh and redraw sorted items
                    InvalidateRect(ctx->hwndLV, NULL, TRUE);
                }
            }
            break;
        }
        case WM_DESTROY: {
            PreviewWindowContext* ctx = 
                (PreviewWindowContext*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
            if (ctx) {
                SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0); // Clear window long pointer first
                if (ctx->pList) {
                    delete ctx->pList;
                }
                delete ctx;
            }
            break;
        }
        default:
            return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Entry Point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;

    // Initialize COM Apartment Model (needed for modern folder browse picker)
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    // Initialize Win32 Common Controls (modern progress bar & listview)
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_PROGRESS_CLASS | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icex);

    // Register Main Window Class
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(45, 45, 48)); // VS Dark gray
    wc.lpszClassName = L"ChronoSyncMainWindow";
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    
    if (!RegisterClassExW(&wc)) {
        MessageBoxW(NULL, L"Window Registration Failed!", L"Error!", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    // Register Preview Window Class
    WNDCLASSEXW wcp = {0};
    wcp.cbSize = sizeof(WNDCLASSEXW);
    wcp.style = CS_HREDRAW | CS_VREDRAW;
    wcp.lpfnWndProc = PreviewWndProc;
    wcp.hInstance = hInstance;
    wcp.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcp.hbrBackground = CreateSolidBrush(RGB(45, 45, 48)); // VS Dark gray
    wcp.lpszClassName = L"ChronoSyncPreviewWindow";
    wcp.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    
    if (!RegisterClassExW(&wcp)) {
        MessageBoxW(NULL, L"Preview Window Registration Failed!", L"Error!", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    // Adjust size so client area is 620x500
    RECT rect = {0, 0, 620, 500};
    AdjustWindowRectEx(&rect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);

    // Create Main GUI Window
    HWND hWnd = CreateWindowExW(
        0,
        L"ChronoSyncMainWindow",
        L"ChronoSync folder synchronizer",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
        NULL, NULL, hInstance, NULL
    );

    if (hWnd == NULL) {
        MessageBoxW(NULL, L"Window Creation Failed!", L"Error!", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    // Apply Immersive Dark Mode title bars
    BOOL useDarkMode = TRUE;
    DwmSetWindowAttribute(hWnd, 19, &useDarkMode, sizeof(useDarkMode));
    DwmSetWindowAttribute(hWnd, 20, &useDarkMode, sizeof(useDarkMode));

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CoUninitialize();
    return (int)msg.wParam;
}

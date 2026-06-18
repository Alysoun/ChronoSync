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
#include <algorithm>
#include "SyncEngine.h"
#include "SyncProfile.h"
#include "SyncOptions.h"
#include "SyncJob.h"

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
    ID_QUEUE_LISTBOX
};

// Define thread communications message IDs
#define WM_SYNC_EVENT               (WM_USER + 10)
#define WM_SYNC_COMPLETE            (WM_USER + 11)
#define WM_SYNC_PREVIEW_COMPLETE    (WM_USER + 12)
#define WM_SYNC_UNDO_COMPLETE       (WM_USER + 13)
#define WM_SYNC_QUEUE_COMPLETE      (WM_USER + 14)

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

std::vector<ChronoSync::SyncJob> g_SyncJobQueue;

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

struct PreviewLaunchData {
    std::vector<ChronoSync::PreviewItem>* pList = nullptr;
    std::wstring sourceRoot;
    std::wstring destRoot;
};

// Context structure for resizable and sortable Preview Window
struct PreviewWindowContext {
    std::vector<ChronoSync::PreviewItem>* pList = nullptr;
    std::vector<int> displayedIndices;
    std::wstring sourceRoot;
    std::wstring destRoot;
    int contextMenuItem = -1;
    HWND hwndLV = NULL;
    HWND hwndFilter = NULL;
    HWND lblSummary = NULL;
    HWND btnExport = NULL;
    HWND btnClose = NULL;
    int sortColumn = -1;
    bool sortAscending = true;
};

// Forward declarations
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK PreviewWndProc(HWND, UINT, WPARAM, LPARAM);

ChronoSync::FilterOptions GetFiltersFromUI() {
    wchar_t includeBuf[1024] = {};
    wchar_t excludeBuf[1024] = {};
    GetWindowTextW(g_hWndIncludeEdit, includeBuf, 1024);
    GetWindowTextW(g_hWndExcludeEdit, excludeBuf, 1024);
    return ChronoSync::FilterOptions::FromSemicolonList(includeBuf, excludeBuf);
}

ChronoSync::SyncOptions GetSyncOptionsFromUI() {
    ChronoSync::SyncOptions opts;
    opts.prune = (SendMessageW(g_hWndPruneCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
    opts.filters = GetFiltersFromUI();
    opts.compareMode = (SendMessageW(g_hWndSha256Check, BM_GETCHECK, 0, 0) == BST_CHECKED)
        ? ChronoSync::CompareMode::Sha256
        : ChronoSync::CompareMode::Timestamp;
    opts.verifyAfterCopy = (SendMessageW(g_hWndVerifyCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
    opts.versionedBackups = (SendMessageW(g_hWndVersionedBackupCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
    opts.maxBackupVersions = 5;
    return opts;
}

void UpdateUndoButtonState(const std::wstring& destPath) {
    EnableWindow(g_hWndUndoBtn, ChronoSync::SyncEngine::HasRestorableBackups(destPath) ? TRUE : FALSE);
}

void RefreshQueueListbox() {
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

void ApplyProfileToUI(const ChronoSync::SyncProfile& profile) {
    SetWindowTextW(g_hWndSrcEdit, profile.source.c_str());
    SetWindowTextW(g_hWndDestEdit, profile.destination.c_str());
    SendMessageW(g_hWndPruneCheck, BM_SETCHECK, profile.options.prune ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(g_hWndIncludeEdit, profile.options.filters.IncludeToSemicolonList().c_str());
    SetWindowTextW(g_hWndExcludeEdit, profile.options.filters.ExcludeToSemicolonList().c_str());
    SendMessageW(g_hWndSha256Check, BM_SETCHECK,
                 profile.options.compareMode == ChronoSync::CompareMode::Sha256 ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_hWndVerifyCheck, BM_SETCHECK, profile.options.verifyAfterCopy ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_hWndVersionedBackupCheck, BM_SETCHECK, profile.options.versionedBackups ? BST_CHECKED : BST_UNCHECKED, 0);
    UpdateUndoButtonState(profile.destination);
}

ChronoSync::SyncProfile BuildProfileFromUI() {
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

ChronoSync::SyncJob BuildJobFromUI() {
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

static bool ContainsInsensitive(const std::wstring& haystack, const std::wstring& needle) {
    if (needle.empty()) {
        return true;
    }
    std::wstring lowerHay = haystack;
    std::wstring lowerNeedle = needle;
    std::transform(lowerHay.begin(), lowerHay.end(), lowerHay.begin(), towlower);
    std::transform(lowerNeedle.begin(), lowerNeedle.end(), lowerNeedle.begin(), towlower);
    return lowerHay.find(lowerNeedle) != std::wstring::npos;
}

void RebuildPreviewFilter(PreviewWindowContext* ctx) {
    if (!ctx || !ctx->pList) {
        return;
    }

    wchar_t filterBuf[256] = {};
    if (ctx->hwndFilter) {
        GetWindowTextW(ctx->hwndFilter, filterBuf, 256);
    }

    ctx->displayedIndices.clear();
    for (int i = 0; i < static_cast<int>(ctx->pList->size()); ++i) {
        const auto& item = (*ctx->pList)[static_cast<size_t>(i)];
        if (ContainsInsensitive(item.relativePath, filterBuf) ||
            ContainsInsensitive(item.action, filterBuf) ||
            ContainsInsensitive(item.sizeStr, filterBuf)) {
            ctx->displayedIndices.push_back(i);
        }
    }

    if (ctx->hwndLV) {
        SendMessageW(ctx->hwndLV, WM_SETREDRAW, FALSE, 0);
        SendMessageW(ctx->hwndLV, LVM_SETITEMCOUNT, (WPARAM)ctx->displayedIndices.size(), (LPARAM)LVSICF_NOINVALIDATEALL);
        SendMessageW(ctx->hwndLV, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(ctx->hwndLV, NULL, TRUE);
    }

    if (ctx->lblSummary) {
        std::wstring summary = L"Showing " + std::to_wstring(ctx->displayedIndices.size()) +
                               L" of " + std::to_wstring(ctx->pList->size()) + L" planned changes.";
        SetWindowTextW(ctx->lblSummary, summary.c_str());
    }
}

std::wstring ResolvePreviewExplorerPath(const ChronoSync::PreviewItem& item,
                                        const std::wstring& sourceRoot,
                                        const std::wstring& destRoot) {
    const std::wstring& root = (item.action == L"Delete") ? destRoot : sourceRoot;
    return root + L"\\" + item.relativePath;
}

bool RevealInExplorer(HWND owner, const std::wstring& fullPath) {
    std::error_code ec;
    std::filesystem::path revealPath(fullPath);

    if (!std::filesystem::exists(revealPath, ec)) {
        revealPath = revealPath.parent_path();
        if (revealPath.empty() || !std::filesystem::exists(revealPath, ec)) {
            MessageBoxW(owner, (L"Path does not exist:\n" + fullPath).c_str(),
                        L"ChronoSync", MB_OK | MB_ICONWARNING);
            return false;
        }
    }

    PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(revealPath.c_str());
    if (!pidl) {
        std::wstring args = L"/select,\"" + revealPath.wstring() + L"\"";
        HINSTANCE result = ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(result) > 32;
    }

    HRESULT hr = SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
    ILFree(pidl);
    if (FAILED(hr)) {
        std::wstring args = L"/select,\"" + revealPath.wstring() + L"\"";
        HINSTANCE result = ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(result) > 32;
    }
    return true;
}

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

std::wstring OpenProfileDialog(HWND hWndParent) {
    std::wstring resultPath;
    IFileOpenDialog* pFileOpen = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                                  IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
    if (SUCCEEDED(hr)) {
        COMDLG_FILTERSPEC fileTypes[] = {
            { L"ChronoSync Profiles (*.chronosync)", L"*.chronosync" },
            { L"JSON Files (*.json)", L"*.json" },
            { L"All Files (*.*)", L"*.*" }
        };
        pFileOpen->SetFileTypes(3, fileTypes);
        pFileOpen->SetDefaultExtension(L"chronosync");
        pFileOpen->SetTitle(L"Load Sync Profile");
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

std::wstring SaveProfileDialog(HWND hWndParent) {
    std::wstring resultPath;
    IFileSaveDialog* pFileSave = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_ALL,
                                  IID_IFileSaveDialog, reinterpret_cast<void**>(&pFileSave));
    if (SUCCEEDED(hr)) {
        COMDLG_FILTERSPEC fileTypes[] = {
            { L"ChronoSync Profiles (*.chronosync)", L"*.chronosync" },
            { L"JSON Files (*.json)", L"*.json" }
        };
        pFileSave->SetFileTypes(2, fileTypes);
        pFileSave->SetDefaultExtension(L"chronosync");
        pFileSave->SetTitle(L"Save Sync Profile");
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

std::wstring OpenQueueDialog(HWND hWndParent) {
    std::wstring resultPath;
    IFileOpenDialog* pFileOpen = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                                  IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
    if (SUCCEEDED(hr)) {
        COMDLG_FILTERSPEC fileTypes[] = {
            { L"ChronoSync Queues (*.chronoqueue)", L"*.chronoqueue" },
            { L"JSON Files (*.json)", L"*.json" },
            { L"All Files (*.*)", L"*.*" }
        };
        pFileOpen->SetFileTypes(3, fileTypes);
        pFileOpen->SetDefaultExtension(L"chronoqueue");
        pFileOpen->SetTitle(L"Load Sync Queue");
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

std::wstring SaveQueueDialog(HWND hWndParent) {
    std::wstring resultPath;
    IFileSaveDialog* pFileSave = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_ALL,
                                  IID_IFileSaveDialog, reinterpret_cast<void**>(&pFileSave));
    if (SUCCEEDED(hr)) {
        COMDLG_FILTERSPEC fileTypes[] = {
            { L"ChronoSync Queues (*.chronoqueue)", L"*.chronoqueue" },
            { L"JSON Files (*.json)", L"*.json" }
        };
        pFileSave->SetFileTypes(2, fileTypes);
        pFileSave->SetDefaultExtension(L"chronoqueue");
        pFileSave->SetTitle(L"Save Sync Queue");
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
    EnableWindow(g_hWndExcludeEdit, enabled);
    EnableWindow(g_hWndIncludeEdit, enabled);
    EnableWindow(g_hWndSaveProfileBtn, enabled);
    EnableWindow(g_hWndLoadProfileBtn, enabled);
    EnableWindow(g_hWndSha256Check, enabled);
    EnableWindow(g_hWndVerifyCheck, enabled);
    EnableWindow(g_hWndVersionedBackupCheck, enabled);
    EnableWindow(g_hWndAddQueueBtn, enabled);
    EnableWindow(g_hWndRunQueueBtn, enabled);
    EnableWindow(g_hWndClearQueueBtn, enabled);
    EnableWindow(g_hWndSaveQueueBtn, enabled);
    EnableWindow(g_hWndLoadQueueBtn, enabled);
    EnableWindow(g_hWndPreviewBtn, enabled);
    EnableWindow(g_hWndSyncBtn, enabled);
    if (!enabled) {
        EnableWindow(g_hWndUndoBtn, FALSE);
    }
}

// Background thread preview runner
void PreviewThreadProc(std::wstring src, std::wstring dest, ChronoSync::SyncOptions options) {
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

    auto list = ChronoSync::SyncEngine::Preview(src, dest, options, callbacks);
    
    std::vector<ChronoSync::PreviewItem>* pList = new std::vector<ChronoSync::PreviewItem>(std::move(list));
    PostMessageW(g_hWndMain, WM_SYNC_PREVIEW_COMPLETE, 0, (LPARAM)pList);
}

// Background thread sync runner
void SyncThreadProc(std::wstring src, std::wstring dest, ChronoSync::SyncOptions options) {
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
    *pStats = ChronoSync::SyncEngine::Sync(src, dest, options, callbacks);

    PostMessageW(g_hWndMain, WM_SYNC_COMPLETE, 1, (LPARAM)pStats);
}

void QueueThreadProc(std::vector<ChronoSync::SyncJob> jobs) {
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
        g_MsgRegistry.SetStatus(L"Comparing directory structures...");
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
    };
    callbacks.onCompareComplete = [](size_t dirsToCreate, size_t filesToCopy, size_t itemsToDelete) {
        std::wstring plan = L"Plan: Create " + std::to_wstring(dirsToCreate) + L" dirs, Copy " +
                            std::to_wstring(filesToCopy) + L" files, Prune " + std::to_wstring(itemsToDelete) + L" items.";
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
        g_MsgRegistry.PushLog(L"[PRUNE] Archiving " + typeStr + L": " + relPath);
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

    size_t completedJobs = 0;
    for (size_t i = 0; i < jobs.size(); ++i) {
        g_MsgRegistry.PushLog(L"=== Queue job " + std::to_wstring(i + 1) + L"/" + std::to_wstring(jobs.size()) +
                              L": " + jobs[i].name + L" ===");
        PostMessageW(g_hWndMain, WM_SYNC_EVENT, 0, 0);
        ChronoSync::SyncStats stats = ChronoSync::SyncEngine::Sync(jobs[i].source, jobs[i].destination, jobs[i].options, callbacks);
        if (stats.filesCopied > 0 || stats.itemsDeleted > 0 || stats.dirsCreated > 0) {
            completedJobs++;
        }
    }

    size_t* pCompleted = new size_t(completedJobs);
    PostMessageW(g_hWndMain, WM_SYNC_QUEUE_COMPLETE, static_cast<WPARAM>(jobs.size()), (LPARAM)pCompleted);
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

    HWND lblExclude = CreateWindowExW(0, L"STATIC", L"Exclude filters (semicolon-separated):",
                                      WS_CHILD | WS_VISIBLE, 20, 165, 300, 20, hWnd, NULL, hInstance, NULL);
    g_hWndExcludeEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
                                        L"*.pkl; node_modules; *.zip",
                                        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                        20, 185, 580, 24, hWnd, (HMENU)ID_EXCLUDE_FILTER_EDIT, hInstance, NULL);

    HWND lblInclude = CreateWindowExW(0, L"STATIC", L"Include filters (optional, empty = all):",
                                      WS_CHILD | WS_VISIBLE, 20, 215, 300, 20, hWnd, NULL, hInstance, NULL);
    g_hWndIncludeEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                        20, 235, 580, 24, hWnd, (HMENU)ID_INCLUDE_FILTER_EDIT, hInstance, NULL);

    g_hWndSaveProfileBtn = CreateWindowExW(0, L"BUTTON", L"Save Profile...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                           20, 268, 140, 28, hWnd, (HMENU)ID_SAVE_PROFILE_BUTTON, hInstance, NULL);
    g_hWndLoadProfileBtn = CreateWindowExW(0, L"BUTTON", L"Load Profile...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                           170, 268, 140, 28, hWnd, (HMENU)ID_LOAD_PROFILE_BUTTON, hInstance, NULL);

    g_hWndSha256Check = CreateWindowExW(0, L"BUTTON", L"SHA256 compare", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                        20, 302, 150, 20, hWnd, (HMENU)ID_SHA256_CHECKBOX, hInstance, NULL);
    g_hWndVerifyCheck = CreateWindowExW(0, L"BUTTON", L"Verify after copy", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                        180, 302, 150, 20, hWnd, (HMENU)ID_VERIFY_COPY_CHECKBOX, hInstance, NULL);
    g_hWndVersionedBackupCheck = CreateWindowExW(0, L"BUTTON", L"Versioned backups", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                                 340, 302, 170, 20, hWnd, (HMENU)ID_VERSIONED_BACKUP_CHECKBOX, hInstance, NULL);
    SendMessageW(g_hWndVersionedBackupCheck, BM_SETCHECK, BST_CHECKED, 0);

    g_hWndPreviewBtn = CreateWindowExW(0, L"BUTTON", L"Preview Changes", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       20, 332, 280, 36, hWnd, (HMENU)ID_PREVIEW_BUTTON, hInstance, NULL);
    g_hWndSyncBtn = CreateWindowExW(0, L"BUTTON", L"Sync Now", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                    320, 332, 280, 36, hWnd, (HMENU)ID_SYNC_BUTTON, hInstance, NULL);

    HWND lblQueue = CreateWindowExW(0, L"STATIC", L"Sync Queue:", WS_CHILD | WS_VISIBLE,
                                      20, 378, 120, 20, hWnd, NULL, hInstance, NULL);
    g_hWndQueueList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                      WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL,
                                      20, 398, 580, 72, hWnd, (HMENU)ID_QUEUE_LISTBOX, hInstance, NULL);

    g_hWndAddQueueBtn = CreateWindowExW(0, L"BUTTON", L"Add to Queue", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                        20, 478, 105, 28, hWnd, (HMENU)ID_ADD_QUEUE_BUTTON, hInstance, NULL);
    g_hWndRunQueueBtn = CreateWindowExW(0, L"BUTTON", L"Run Queue", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                        130, 478, 105, 28, hWnd, (HMENU)ID_RUN_QUEUE_BUTTON, hInstance, NULL);
    g_hWndClearQueueBtn = CreateWindowExW(0, L"BUTTON", L"Clear Queue", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                          240, 478, 105, 28, hWnd, (HMENU)ID_CLEAR_QUEUE_BUTTON, hInstance, NULL);
    g_hWndSaveQueueBtn = CreateWindowExW(0, L"BUTTON", L"Save Queue...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                         350, 478, 115, 28, hWnd, (HMENU)ID_SAVE_QUEUE_BUTTON, hInstance, NULL);
    g_hWndLoadQueueBtn = CreateWindowExW(0, L"BUTTON", L"Load Queue...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                         470, 478, 130, 28, hWnd, (HMENU)ID_LOAD_QUEUE_BUTTON, hInstance, NULL);

    g_hWndStatusLabel = CreateWindowExW(0, L"STATIC", L"Ready", WS_CHILD | WS_VISIBLE,
                                        20, 516, 580, 20, hWnd, (HMENU)ID_STATUS_LABEL, hInstance, NULL);

    g_hWndProgressBar = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE,
                                        20, 536, 580, 20, hWnd, (HMENU)ID_PROGRESS_BAR, hInstance, NULL);

    HWND lblLog = CreateWindowExW(0, L"STATIC", L"Operation Log:", WS_CHILD | WS_VISIBLE,
                                  20, 566, 120, 20, hWnd, NULL, hInstance, NULL);

    g_hWndLogEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                    WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
                                    20, 589, 580, 110, hWnd, (HMENU)ID_LOG_EDIT, hInstance, NULL);

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
    SendMessageW(lblExclude, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
    SendMessageW(g_hWndExcludeEdit, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
    SendMessageW(lblInclude, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
    SendMessageW(g_hWndIncludeEdit, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
    SendMessageW(g_hWndSaveProfileBtn, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
    SendMessageW(g_hWndLoadProfileBtn, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
    SendMessageW(g_hWndSha256Check, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
    SendMessageW(g_hWndVerifyCheck, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
    SendMessageW(g_hWndVersionedBackupCheck, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
    SendMessageW(g_hWndPreviewBtn, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
    SendMessageW(g_hWndSyncBtn, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
    SendMessageW(lblQueue, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
    SendMessageW(g_hWndQueueList, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
    SendMessageW(g_hWndAddQueueBtn, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
    SendMessageW(g_hWndRunQueueBtn, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
    SendMessageW(g_hWndClearQueueBtn, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
    SendMessageW(g_hWndSaveQueueBtn, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
    SendMessageW(g_hWndLoadQueueBtn, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
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
                    UpdateUndoButtonState(path);
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

                ChronoSync::SyncOptions options = GetSyncOptionsFromUI();

                SetControlsState(FALSE);
                SetWindowTextW(g_hWndLogEdit, L"");

                g_SyncRunning = true;
                std::thread t(PreviewThreadProc, sourcePath, destPath, options);
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
                if (g_SyncJobQueue.empty()) {
                    MessageBoxW(hWnd, L"The sync queue is empty.", L"ChronoSync Queue", MB_OK | MB_ICONINFORMATION);
                    break;
                }
                SetControlsState(FALSE);
                SetWindowTextW(g_hWndLogEdit, L"");
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
                SetWindowTextW(g_hWndLogEdit, L"");

                g_SyncRunning = true;
                std::thread t(SyncThreadProc, sourcePath, destPath, options);
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
            SetControlsState(TRUE); // Re-enable action buttons
            SendMessageW(g_hWndProgressBar, PBM_SETPOS, 0, 0);
            SetWindowTextW(g_hWndStatusLabel, L"Ready");

            // Wrap in unique_ptr to prevent leaks if CreateWindowExW fails
            std::unique_ptr<std::vector<ChronoSync::PreviewItem>> pList((std::vector<ChronoSync::PreviewItem>*)lParam);

            // Re-evaluate undo button availability
            wchar_t dest[MAX_PATH];
            GetWindowTextW(g_hWndDestEdit, dest, MAX_PATH);
            UpdateUndoButtonState(dest);

            if (pList) {
                if (pList->empty()) {
                    MessageBoxW(hWnd, L"No modifications needed. Folders are already in sync.", L"ChronoSync Preview", MB_OK | MB_ICONINFORMATION);
                } else {
                    RECT rect = {0, 0, 680, 440};
                    AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0);

                    wchar_t src[MAX_PATH] = {};
                    GetWindowTextW(g_hWndSrcEdit, src, MAX_PATH);
                    GetWindowTextW(g_hWndDestEdit, dest, MAX_PATH);

                    auto* launchData = new PreviewLaunchData();
                    launchData->pList = pList.release();
                    launchData->sourceRoot = src;
                    launchData->destRoot = dest;

                    HWND hwndPreview = CreateWindowExW(
                        0, L"ChronoSyncPreviewWindow", L"Sync Preview - ChronoSync",
                        WS_OVERLAPPEDWINDOW,
                        CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
                        hWnd, NULL, GetModuleHandleW(NULL), launchData
                    );
                    
                    if (hwndPreview) {

                        // Style secondary window with DWM Immersive Dark Mode
                        BOOL useDarkMode = TRUE;
                        DwmSetWindowAttribute(hwndPreview, 19, &useDarkMode, sizeof(useDarkMode));
                        DwmSetWindowAttribute(hwndPreview, 20, &useDarkMode, sizeof(useDarkMode));

                        ShowWindow(hwndPreview, SW_SHOW);
                    } else {
                        delete launchData->pList;
                        delete launchData;
                        MessageBoxW(hWnd, L"Failed to create Preview window.", L"Error", MB_OK | MB_ICONERROR);
                    }
                }
            }
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
            PreviewLaunchData* launchData =
                (PreviewLaunchData*)((LPCREATESTRUCTW)lParam)->lpCreateParams;

            PreviewWindowContext* ctx = new PreviewWindowContext();
            if (launchData) {
                ctx->pList = launchData->pList;
                ctx->sourceRoot = std::move(launchData->sourceRoot);
                ctx->destRoot = std::move(launchData->destRoot);
                delete launchData;
            }
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)ctx);

            ctx->hwndFilter = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                15, 15, 650, 24, hWnd, (HMENU)ID_PREVIEW_FILTER_EDIT, GetModuleHandleW(NULL), NULL);
            SendMessageW(ctx->hwndFilter, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

            // Create list view child control using LVS_OWNERDATA for instant loading
            ctx->hwndLV = CreateWindowExW(
                0, WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_OWNERDATA | WS_BORDER | WS_VSCROLL,
                15, 48, 650, 327,
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

            if (ctx->pList) {
                RebuildPreviewFilter(ctx);
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

                // Resize filter box
                if (ctx->hwndFilter) {
                    MoveWindow(ctx->hwndFilter, 15, 15, cx - 30, 24, TRUE);
                }
                // Resize ListView
                if (ctx->hwndLV) {
                    MoveWindow(ctx->hwndLV, 15, 48, cx - 30, cy - 103, TRUE);
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
            PreviewWindowContext* ctx =
                (PreviewWindowContext*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

            if (wmId == ID_PREVIEW_FILTER_EDIT && HIWORD(wParam) == EN_CHANGE) {
                RebuildPreviewFilter(ctx);
                break;
            }

            if (wmId == IDCANCEL) {
                DestroyWindow(hWnd);
            } else if (wmId == ID_PREVIEW_LOCATE_EXPLORER) {
                if (ctx && ctx->pList && ctx->contextMenuItem >= 0 &&
                    ctx->contextMenuItem < static_cast<int>(ctx->displayedIndices.size())) {
                    const auto& item = (*ctx->pList)[static_cast<size_t>(
                        ctx->displayedIndices[static_cast<size_t>(ctx->contextMenuItem)])];
                    std::wstring fullPath = ResolvePreviewExplorerPath(item, ctx->sourceRoot, ctx->destRoot);
                    RevealInExplorer(hWnd, fullPath);
                }
            } else if (wmId == ID_EXPORT_CSV_BUTTON) {
                if (ctx && ctx->pList && !ctx->pList->empty()) {
                    std::wstring selectedPath = SaveCSVDialog(hWnd, L"Export Preview to CSV");
                    if (!selectedPath.empty()) {
                        std::ofstream out(selectedPath, std::ios::binary);
                        if (out) {
                            // Write UTF-8 BOM for seamless Excel compatibility
                            out << "\xEF\xBB\xBF";
                            
                            // Write CSV header (Relative Path, Planned Action, Size (Bytes), Formatted Size)
                            out << "Relative Path,Planned Action,Size (Bytes),Formatted Size\n";

                            const auto& indices = (!ctx->displayedIndices.empty())
                                ? ctx->displayedIndices
                                : std::vector<int>();
                            if (!indices.empty()) {
                                for (int index : indices) {
                                    const auto& item = (*ctx->pList)[static_cast<size_t>(index)];
                                    std::wstring escapedPath = EscapeCSV(item.relativePath);
                                    std::wstring escapedAction = EscapeCSV(item.action);
                                    std::wstring rawSizeStr = std::to_wstring(item.fileSize);
                                    std::wstring escapedSize = EscapeCSV(item.sizeStr);

                                    std::string utf8Line = WideToUTF8(escapedPath + L"," + escapedAction + L"," + rawSizeStr + L"," + escapedSize + L"\n");
                                    out.write(utf8Line.data(), utf8Line.size());
                                }
                            } else {
                                for (const auto& item : *ctx->pList) {
                                    std::wstring escapedPath = EscapeCSV(item.relativePath);
                                    std::wstring escapedAction = EscapeCSV(item.action);
                                    std::wstring rawSizeStr = std::to_wstring(item.fileSize);
                                    std::wstring escapedSize = EscapeCSV(item.sizeStr);

                                    std::string utf8Line = WideToUTF8(escapedPath + L"," + escapedAction + L"," + rawSizeStr + L"," + escapedSize + L"\n");
                                    out.write(utf8Line.data(), utf8Line.size());
                                }
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
                if (ctx && ctx->pList && plvdi->item.iItem < (int)ctx->displayedIndices.size()) {
                    const auto& item = (*ctx->pList)[static_cast<size_t>(ctx->displayedIndices[static_cast<size_t>(plvdi->item.iItem)])];
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
                if (ctx && ctx->pList && !ctx->displayedIndices.empty()) {
                    int col = pnmlv->iSubItem;
                    if (ctx->sortColumn == col) {
                        ctx->sortAscending = !ctx->sortAscending;
                    } else {
                        ctx->sortColumn = col;
                        ctx->sortAscending = true;
                    }

                    bool asc = ctx->sortAscending;
                    auto compareByIndex = [&](int aIdx, int bIdx) {
                        const auto& a = (*ctx->pList)[static_cast<size_t>(aIdx)];
                        const auto& b = (*ctx->pList)[static_cast<size_t>(bIdx)];
                        if (col == 0) {
                            return asc ? (a.relativePath < b.relativePath) : (a.relativePath > b.relativePath);
                        }
                        if (col == 1) {
                            if (a.action != b.action) {
                                return asc ? (a.action < b.action) : (a.action > b.action);
                            }
                            return asc ? (a.relativePath < b.relativePath) : (a.relativePath > b.relativePath);
                        }
                        if (a.fileSize != b.fileSize) {
                            return asc ? (a.fileSize < b.fileSize) : (a.fileSize > b.fileSize);
                        }
                        return asc ? (a.relativePath < b.relativePath) : (a.relativePath > b.relativePath);
                    };

                    std::sort(ctx->displayedIndices.begin(), ctx->displayedIndices.end(), compareByIndex);

                    // Force ListView to refresh and redraw sorted items
                    InvalidateRect(ctx->hwndLV, NULL, TRUE);
                }
            } else if (pnmh->code == NM_RCLICK && pnmh->idFrom == 201) {
                LPNMITEMACTIVATE itemActivate = (LPNMITEMACTIVATE)lParam;
                int item = itemActivate->iItem;
                if (ctx && item >= 0 && item < static_cast<int>(ctx->displayedIndices.size())) {
                    ctx->contextMenuItem = item;
                    ListView_SetItemState(ctx->hwndLV, item, LVIS_SELECTED | LVIS_FOCUSED,
                                          LVIS_SELECTED | LVIS_FOCUSED);

                    HMENU hMenu = CreatePopupMenu();
                    AppendMenuW(hMenu, MF_STRING, ID_PREVIEW_LOCATE_EXPLORER, L"Show in File Explorer");

                    POINT pt = itemActivate->ptAction;
                    ClientToScreen(pnmh->hwndFrom, &pt);
                    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, NULL);
                    DestroyMenu(hMenu);
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

    // Adjust size so client area is 620x710
    RECT rect = {0, 0, 620, 710};
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

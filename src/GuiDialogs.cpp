#include "GuiDialogs.h"

#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <algorithm>

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

std::string WideToUTF8(const std::wstring& wstr) {
    if (wstr.empty()) {
        return std::string();
    }
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], static_cast<int>(wstr.size()), NULL, 0, NULL, NULL);
    std::string strTo(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], static_cast<int>(wstr.size()), &strTo[0], sizeNeeded, NULL, NULL);
    return strTo;
}

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
            { L"PrevueSync Profiles (*.prevuesync)", L"*.prevuesync" },
            { L"JSON Files (*.json)", L"*.json" },
            { L"All Files (*.*)", L"*.*" }
        };
        pFileOpen->SetFileTypes(3, fileTypes);
        pFileOpen->SetDefaultExtension(L"prevuesync");
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
            { L"PrevueSync Profiles (*.prevuesync)", L"*.prevuesync" },
            { L"JSON Files (*.json)", L"*.json" }
        };
        pFileSave->SetFileTypes(2, fileTypes);
        pFileSave->SetDefaultExtension(L"prevuesync");
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
            { L"PrevueSync Queues (*.prevuequeue)", L"*.prevuequeue" },
            { L"JSON Files (*.json)", L"*.json" },
            { L"All Files (*.*)", L"*.*" }
        };
        pFileOpen->SetFileTypes(3, fileTypes);
        pFileOpen->SetDefaultExtension(L"prevuequeue");
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
            { L"PrevueSync Queues (*.prevuequeue)", L"*.prevuequeue" },
            { L"JSON Files (*.json)", L"*.json" }
        };
        pFileSave->SetFileTypes(2, fileTypes);
        pFileSave->SetDefaultExtension(L"prevuequeue");
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

bool RevealInExplorer(HWND owner, const std::wstring& fullPath) {
    std::error_code ec;
    std::filesystem::path revealPath(fullPath);

    if (!std::filesystem::exists(revealPath, ec)) {
        revealPath = revealPath.parent_path();
        if (revealPath.empty() || !std::filesystem::exists(revealPath, ec)) {
            MessageBoxW(owner, (L"Path does not exist:\n" + fullPath).c_str(),
                        L"PrevueSync", MB_OK | MB_ICONWARNING);
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

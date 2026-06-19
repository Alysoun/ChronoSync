#include "NetworkShare.h"
#include <windows.h>
#include <winnetwk.h>
#include <algorithm>

#pragma comment(lib, "mpr.lib")

namespace PrevueSync {

    bool NetworkShare::IsUncPath(const std::wstring& path) {
        return path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\';
    }

    std::wstring NetworkShare::GetUncRoot(const std::wstring& path) {
        if (!IsUncPath(path)) {
            return {};
        }

        size_t start = 2;
        size_t firstSep = path.find(L'\\', start);
        if (firstSep == std::wstring::npos) {
            return path;
        }

        size_t secondSep = path.find(L'\\', firstSep + 1);
        if (secondSep == std::wstring::npos) {
            return path;
        }

        return path.substr(0, secondSep);
    }

    bool NetworkShare::IsPathReachable(const std::wstring& path) {
        DWORD attrs = GetFileAttributesW(path.c_str());
        return attrs != INVALID_FILE_ATTRIBUTES;
    }

    bool NetworkShare::EnsureAccessible(const std::wstring& path, std::wstring& errorMessage) {
        if (!IsUncPath(path)) {
            return true;
        }

        if (IsPathReachable(path)) {
            return true;
        }

        std::wstring root = GetUncRoot(path);
        if (root.empty()) {
            errorMessage = L"Invalid UNC path: " + path;
            return false;
        }

        NETRESOURCEW resource = {};
        resource.dwType = RESOURCETYPE_DISK;
        resource.lpRemoteName = root.data();

        DWORD result = WNetAddConnection2W(&resource, nullptr, nullptr, CONNECT_TEMPORARY);
        if (result == NO_ERROR || result == ERROR_SESSION_CREDENTIAL_CONFLICT) {
            if (IsPathReachable(path)) {
                return true;
            }
            errorMessage = L"Connected to share but path is still unreachable: " + path;
            return false;
        }

        if (result == ERROR_CANCELLED) {
            errorMessage = L"Network connection cancelled for: " + root;
            return false;
        }

        errorMessage = L"Failed to connect to network share " + root + L". Win32 Error: " + std::to_wstring(result);
        return false;
    }

    bool NetworkShare::IsRetryableNetworkError(DWORD errorCode) {
        switch (errorCode) {
            case ERROR_NETWORK_UNREACHABLE:
            case ERROR_NETNAME_DELETED:
            case ERROR_BAD_NETPATH:
            case ERROR_BAD_NET_NAME:
            case ERROR_UNEXP_NET_ERR:
            case ERROR_SEM_TIMEOUT:
            case ERROR_BUSY:
            case ERROR_SHARING_BUFFER_EXCEEDED:
            case ERROR_PATH_NOT_FOUND:
                return true;
            default:
                return false;
        }
    }

} // namespace PrevueSync

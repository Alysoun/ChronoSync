#pragma once

#include <windows.h>
#include <string>

namespace PrevueSync {

    class NetworkShare {
    public:
        static bool IsUncPath(const std::wstring& path);
        static std::wstring GetUncRoot(const std::wstring& path);
        static bool IsPathReachable(const std::wstring& path);
        static bool EnsureAccessible(const std::wstring& path, std::wstring& errorMessage);
        static bool IsRetryableNetworkError(DWORD errorCode);
    };

} // namespace PrevueSync

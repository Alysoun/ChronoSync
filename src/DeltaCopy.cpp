#include "DeltaCopy.h"
#include "WinPath.h"
#include <windows.h>
#include <vector>
#include <algorithm>
#include <cstring>

namespace PrevueSync {

    static constexpr DWORD kBlockSize = 4 * 1024 * 1024;

    DeltaCopyResult DeltaCopy::CopyFileBlocks(const std::wstring& sourcePath,
                                        const std::wstring& destinationPath,
                                        unsigned long long fileSizeBytes,
                                        const std::function<void(unsigned long long, unsigned long long)>& progressCallback) {
        DeltaCopyResult result;
        const std::wstring sourceOpen = WinPath::IsExtended(sourcePath) ? sourcePath : WinPath::ToExtended(sourcePath);
        const std::wstring destOpen = WinPath::IsExtended(destinationPath) ? destinationPath : WinPath::ToExtended(destinationPath);
        std::wstring tempPath = destOpen + L".prevue_tmp";

        HANDLE hSource = CreateFileW(sourceOpen.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                     FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (hSource == INVALID_HANDLE_VALUE) {
            result.errorMessage = L"Unable to open source file. Win32 Error: " + std::to_wstring(GetLastError());
            return result;
        }

        HANDLE hDest = CreateFileW(destOpen.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        const bool destExists = hDest != INVALID_HANDLE_VALUE;

        HANDLE hOutput = CreateFileW(tempPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                     FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (hOutput == INVALID_HANDLE_VALUE) {
            if (destExists) {
                CloseHandle(hDest);
            }
            CloseHandle(hSource);
            result.errorMessage = L"Unable to create temp file. Win32 Error: " + std::to_wstring(GetLastError());
            return result;
        }

        LARGE_INTEGER fullSize;
        fullSize.QuadPart = static_cast<LONGLONG>(fileSizeBytes);
        SetFilePointerEx(hOutput, fullSize, nullptr, FILE_BEGIN);
        if (!SetEndOfFile(hOutput)) {
            CloseHandle(hOutput);
            DeleteFileW(tempPath.c_str());
            if (destExists) {
                CloseHandle(hDest);
            }
            CloseHandle(hSource);
            result.errorMessage = L"Unable to pre-allocate temp file. Win32 Error: " + std::to_wstring(GetLastError());
            return result;
        }
        SetFilePointerEx(hOutput, {}, nullptr, FILE_BEGIN);

        std::vector<char> sourceBlock(kBlockSize);
        std::vector<char> destBlock(kBlockSize);
        unsigned long long offset = 0;

        while (offset < fileSizeBytes) {
            const DWORD bytesToRead = static_cast<DWORD>(std::min<unsigned long long>(kBlockSize, fileSizeBytes - offset));

            DWORD sourceRead = 0;
            if (!ReadFile(hSource, sourceBlock.data(), bytesToRead, &sourceRead, nullptr) || sourceRead == 0) {
                result.errorMessage = L"Failed reading source block at offset " + std::to_wstring(offset);
                CloseHandle(hOutput);
                DeleteFileW(tempPath.c_str());
                if (destExists) {
                    CloseHandle(hDest);
                }
                CloseHandle(hSource);
                return result;
            }

            bool writeSourceBlock = true;
            if (destExists) {
                LARGE_INTEGER destOffset;
                destOffset.QuadPart = static_cast<LONGLONG>(offset);
                SetFilePointerEx(hDest, destOffset, nullptr, FILE_BEGIN);

                DWORD destRead = 0;
                if (ReadFile(hDest, destBlock.data(), sourceRead, &destRead, nullptr) &&
                    destRead == sourceRead &&
                    std::memcmp(sourceBlock.data(), destBlock.data(), sourceRead) == 0) {
                    writeSourceBlock = false;
                }
            }

            LARGE_INTEGER writeOffset;
            writeOffset.QuadPart = static_cast<LONGLONG>(offset);
            SetFilePointerEx(hOutput, writeOffset, nullptr, FILE_BEGIN);

            const char* writeBuffer = writeSourceBlock ? sourceBlock.data() : destBlock.data();
            DWORD writeSize = sourceRead;
            DWORD written = 0;
            if (!WriteFile(hOutput, writeBuffer, writeSize, &written, nullptr) || written != writeSize) {
                result.errorMessage = L"Failed writing temp block at offset " + std::to_wstring(offset);
                CloseHandle(hOutput);
                DeleteFileW(tempPath.c_str());
                if (destExists) {
                    CloseHandle(hDest);
                }
                CloseHandle(hSource);
                return result;
            }

            if (writeSourceBlock) {
                result.bytesWritten += written;
            }

            offset += sourceRead;
            if (progressCallback) {
                progressCallback(offset, fileSizeBytes);
            }
        }

        CloseHandle(hOutput);
        if (destExists) {
            CloseHandle(hDest);
        }
        CloseHandle(hSource);

        if (!MoveFileExW(tempPath.c_str(), destOpen.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            result.errorMessage = L"Atomic move failed after delta copy. Win32 Error: " + std::to_wstring(GetLastError());
            DeleteFileW(tempPath.c_str());
            return result;
        }

        result.success = true;
        return result;
    }

} // namespace PrevueSync

#include "FileHash.h"
#include <windows.h>
#include <bcrypt.h>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace ChronoSync {

    bool FileHash::Sha256File(const std::wstring& path, std::array<uint8_t, 32>& outHash) {
        HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                   FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) {
            return false;
        }

        BCRYPT_ALG_HANDLE hAlg = nullptr;
        NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
        if (!BCRYPT_SUCCESS(status)) {
            CloseHandle(hFile);
            return false;
        }

        DWORD hashObjectSize = 0;
        DWORD dataSize = 0;
        status = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&hashObjectSize),
                                   sizeof(DWORD), &dataSize, 0);
        if (!BCRYPT_SUCCESS(status)) {
            BCryptCloseAlgorithmProvider(hAlg, 0);
            CloseHandle(hFile);
            return false;
        }

        std::vector<UCHAR> hashObject(hashObjectSize);
        BCRYPT_HASH_HANDLE hHash = nullptr;
        status = BCryptCreateHash(hAlg, &hHash, hashObject.data(), hashObjectSize, nullptr, 0, 0);
        if (!BCRYPT_SUCCESS(status)) {
            BCryptCloseAlgorithmProvider(hAlg, 0);
            CloseHandle(hFile);
            return false;
        }

        constexpr DWORD kBufferSize = 1024 * 1024;
        std::vector<uint8_t> buffer(kBufferSize);
        DWORD bytesRead = 0;
        BOOL readOk = TRUE;
        while (readOk) {
            readOk = ReadFile(hFile, buffer.data(), kBufferSize, &bytesRead, nullptr);
            if (!readOk || bytesRead == 0) {
                break;
            }
            status = BCryptHashData(hHash, buffer.data(), bytesRead, 0);
            if (!BCRYPT_SUCCESS(status)) {
                BCryptDestroyHash(hHash);
                BCryptCloseAlgorithmProvider(hAlg, 0);
                CloseHandle(hFile);
                return false;
            }
        }

        CloseHandle(hFile);

        DWORD hashLen = 0;
        BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLen),
                          sizeof(DWORD), &dataSize, 0);
        status = BCryptFinishHash(hHash, outHash.data(), hashLen, 0);
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return BCRYPT_SUCCESS(status);
    }

    bool FileHash::HashesEqual(const std::array<uint8_t, 32>& a, const std::array<uint8_t, 32>& b) {
        uint8_t diff = 0;
        for (size_t i = 0; i < a.size(); ++i) {
            diff |= static_cast<uint8_t>(a[i] ^ b[i]);
        }
        return diff == 0;
    }

    std::wstring FileHash::ToHex(const std::array<uint8_t, 32>& hash) {
        static const wchar_t* kHex = L"0123456789abcdef";
        std::wstring hex;
        hex.reserve(hash.size() * 2);
        for (uint8_t byte : hash) {
            hex.push_back(kHex[byte >> 4]);
            hex.push_back(kHex[byte & 0x0F]);
        }
        return hex;
    }

} // namespace ChronoSync

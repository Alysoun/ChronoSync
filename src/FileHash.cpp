#include "FileHash.h"
#include "WinPath.h"
#include <windows.h>
#include <bcrypt.h>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace PrevueSync {

    static constexpr DWORD kReadBufferSize = 4 * 1024 * 1024;
    static constexpr unsigned long long kProgressIntervalBytes = 8ULL * 1024 * 1024;

    Sha256Session::Sha256Session() {
        BCRYPT_ALG_HANDLE hAlg = nullptr;
        NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
        if (!BCRYPT_SUCCESS(status)) {
            return;
        }

        DWORD hashObjectSize = 0;
        DWORD dataSize = 0;
        status = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&hashObjectSize),
                                   sizeof(DWORD), &dataSize, 0);
        if (!BCRYPT_SUCCESS(status)) {
            BCryptCloseAlgorithmProvider(hAlg, 0);
            return;
        }

        DWORD hashLength = 0;
        status = BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLength),
                                   sizeof(DWORD), &dataSize, 0);
        if (!BCRYPT_SUCCESS(status)) {
            BCryptCloseAlgorithmProvider(hAlg, 0);
            return;
        }

        hAlg_ = hAlg;
        hashObjectSize_ = hashObjectSize;
        hashLength_ = hashLength;
        valid_ = true;
    }

    Sha256Session::~Sha256Session() {
        if (hAlg_) {
            BCryptCloseAlgorithmProvider(static_cast<BCRYPT_ALG_HANDLE>(hAlg_), 0);
            hAlg_ = nullptr;
        }
    }

    bool Sha256Session::HashFile(const std::wstring& path,
                                 std::array<uint8_t, 32>& outHash,
                                 const Sha256Progress* progress) {
        if (!valid_) {
            return false;
        }

        BCRYPT_ALG_HANDLE hAlg = static_cast<BCRYPT_ALG_HANDLE>(hAlg_);

        std::wstring openPath = path;
        if (!WinPath::IsExtended(path)) {
            std::error_code ec;
            std::filesystem::path absolute = std::filesystem::absolute(path, ec);
            if (!ec) {
                openPath = WinPath::ToExtended(absolute.wstring());
            } else {
                openPath = WinPath::ToExtended(path);
            }
        }

        HANDLE hFile = CreateFileW(openPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                   FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) {
            return false;
        }

        LARGE_INTEGER fileSize = {};
        if (!GetFileSizeEx(hFile, &fileSize)) {
            CloseHandle(hFile);
            return false;
        }

        std::vector<UCHAR> hashObject(hashObjectSize_);
        BCRYPT_HASH_HANDLE hHash = nullptr;
        NTSTATUS status = BCryptCreateHash(hAlg, &hHash, hashObject.data(), hashObjectSize_, nullptr, 0, 0);
        if (!BCRYPT_SUCCESS(status)) {
            CloseHandle(hFile);
            return false;
        }

        std::vector<uint8_t> buffer(kReadBufferSize);
        unsigned long long totalRead = 0;
        unsigned long long lastProgressAt = 0;
        DWORD bytesRead = 0;
        BOOL readOk = TRUE;

        while (readOk) {
            readOk = ReadFile(hFile, buffer.data(), kReadBufferSize, &bytesRead, nullptr);
            if (!readOk || bytesRead == 0) {
                break;
            }
            status = BCryptHashData(hHash, buffer.data(), bytesRead, 0);
            if (!BCRYPT_SUCCESS(status)) {
                BCryptDestroyHash(hHash);
                CloseHandle(hFile);
                return false;
            }

            totalRead += bytesRead;
            if (progress && progress->onProgress &&
                (totalRead - lastProgressAt >= kProgressIntervalBytes || totalRead >= static_cast<unsigned long long>(fileSize.QuadPart))) {
                progress->onProgress(totalRead, static_cast<unsigned long long>(fileSize.QuadPart));
                lastProgressAt = totalRead;
            }
        }

        CloseHandle(hFile);

        status = BCryptFinishHash(hHash, outHash.data(), hashLength_, 0);
        BCryptDestroyHash(hHash);
        return BCRYPT_SUCCESS(status);
    }

    bool FileHash::Sha256File(const std::wstring& path, std::array<uint8_t, 32>& outHash,
                              const Sha256Progress* progress) {
        Sha256Session session;
        if (!session.IsValid()) {
            return false;
        }
        return session.HashFile(path, outHash, progress);
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

} // namespace PrevueSync

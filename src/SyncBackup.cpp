#include "SyncBackup.h"
#include "FileHash.h"
#include <windows.h>
#include <array>
#include <algorithm>
#include <vector>

namespace PrevueSync {

    std::wstring MakeBackupTimestamp() {
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t buf[32];
        swprintf_s(buf, L"%04d-%02d-%02d_%02d%02d%02d",
                   st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        return buf;
    }

    void PruneOldBackupVersions(const std::filesystem::path& backupRoot, size_t maxVersions, std::error_code& ec) {
        if (!std::filesystem::exists(backupRoot, ec)) {
            return;
        }

        std::vector<std::filesystem::path> versions;
        for (const auto& entry : std::filesystem::directory_iterator(backupRoot, ec)) {
            if (entry.is_directory()) {
                versions.push_back(entry.path());
            }
        }

        std::sort(versions.begin(), versions.end(), std::greater<std::filesystem::path>());
        for (size_t i = maxVersions; i < versions.size(); ++i) {
            std::filesystem::remove_all(versions[i], ec);
        }
    }

    std::filesystem::path GetLatestBackupFolder(const std::filesystem::path& backupRoot) {
        std::error_code ec;
        std::filesystem::path latest;
        if (!std::filesystem::exists(backupRoot, ec)) {
            return latest;
        }

        for (const auto& entry : std::filesystem::directory_iterator(backupRoot, ec)) {
            if (entry.is_directory()) {
                if (latest.empty() || entry.path().filename().wstring() > latest.filename().wstring()) {
                    latest = entry.path();
                }
            }
        }
        return latest;
    }

    bool VerifyCopiedFile(const std::filesystem::path& srcPath,
                          const std::filesystem::path& destPath,
                          Sha256Session* session,
                          const SyncCallbacks* callbacks) {
        Sha256Session localSession;
        Sha256Session* activeSession = session;
        if (!activeSession || !activeSession->IsValid()) {
            if (!localSession.IsValid()) {
                return false;
            }
            activeSession = &localSession;
        }

        Sha256Progress progress;
        progress.onProgress = [&](unsigned long long bytesHashed, unsigned long long fileSize) {
            if (callbacks && callbacks->onHashProgress) {
                callbacks->onHashProgress(srcPath.filename().wstring(), bytesHashed, fileSize, true);
            }
        };

        std::array<uint8_t, 32> srcHash{};
        std::array<uint8_t, 32> destHash{};
        if (!activeSession->HashFile(srcPath.wstring(), srcHash, &progress)) {
            return false;
        }

        progress.onProgress = [&](unsigned long long bytesHashed, unsigned long long fileSize) {
            if (callbacks && callbacks->onHashProgress) {
                callbacks->onHashProgress(srcPath.filename().wstring(), bytesHashed, fileSize, false);
            }
        };

        if (!activeSession->HashFile(destPath.wstring(), destHash, &progress)) {
            return false;
        }
        return FileHash::HashesEqual(srcHash, destHash);
    }

} // namespace PrevueSync

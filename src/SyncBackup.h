#pragma once

#include "SyncEngine.h"
#include "FileHash.h"
#include <filesystem>
#include <string>

namespace ChronoSync {

    std::wstring MakeBackupTimestamp();

    void PruneOldBackupVersions(const std::filesystem::path& backupRoot, size_t maxVersions, std::error_code& ec);

    std::filesystem::path GetLatestBackupFolder(const std::filesystem::path& backupRoot);

    bool VerifyCopiedFile(const std::filesystem::path& srcPath,
                          const std::filesystem::path& destPath,
                          Sha256Session* session = nullptr,
                          const SyncCallbacks* callbacks = nullptr);

} // namespace ChronoSync

#pragma once

#include "SyncEngine.h"
#include "SyncOptions.h"
#include "FileHash.h"
#include "SyncHashCache.h"
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace PrevueSync {

    enum class DeleteReason {
        Replace, // destination removed to resolve type/link mismatch (runs even when prune is off)
        Prune    // destination item no longer exists in source
    };

    struct PlannedDelete {
        SyncItem item;
        DeleteReason reason = DeleteReason::Prune;
    };

    struct SyncPlan {
        std::vector<SyncItem> dirsToCreate;
        std::vector<SyncItem> filesToCopy;
        std::vector<SyncItem> linksToCreate;
        std::vector<PlannedDelete> itemsToDelete;
        size_t filesSkipped = 0;
    };

    bool FileContentsDiffer(const std::filesystem::path& srcPath,
                            const std::filesystem::path& destPath,
                            const SyncItem& srcItem,
                            const SyncItem& destItem,
                            CompareMode mode,
                            Sha256Session* hashSession,
                            SyncHashCache* hashCache,
                            const SyncCallbacks* callbacks);

    SyncPlan BuildSyncPlan(const std::vector<SyncItem>& srcItems,
                           const std::vector<SyncItem>& destItems,
                           const std::filesystem::path& srcRoot,
                           const std::filesystem::path& destRoot,
                           const SyncOptions& options,
                           const SyncCallbacks& callbacks = {});

    std::vector<PreviewItem> BuildPreviewList(const SyncPlan& plan,
                                              const std::unordered_map<std::wstring, SyncItem>& destMap);

} // namespace PrevueSync

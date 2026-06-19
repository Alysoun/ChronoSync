#include "SyncPlan.h"
#include "FileHash.h"
#include "WinPath.h"
#include <windows.h>
#include <array>
#include <unordered_set>
#include <algorithm>

#ifndef IO_REPARSE_TAG_MOUNT_POINT
#define IO_REPARSE_TAG_MOUNT_POINT 0xA0000003L
#endif

namespace PrevueSync {

    bool FileContentsDiffer(const std::filesystem::path& srcPath,
                            const std::filesystem::path& destPath,
                            const SyncItem& srcItem,
                            const SyncItem& destItem,
                            CompareMode mode,
                            Sha256Session* hashSession,
                            SyncHashCache* hashCache,
                            const SyncCallbacks* callbacks) {
        if (srcItem.fileSize != destItem.fileSize) {
            return true;
        }

        if (mode == CompareMode::Timestamp) {
            LONG cmp = CompareFileTime(&srcItem.lastWriteTime, &destItem.lastWriteTime);
            return cmp > 0;
        }

        if (!hashSession || !hashSession->IsValid()) {
            return true;
        }

        Sha256Progress progress;
        progress.onProgress = [&](unsigned long long bytesHashed, unsigned long long fileSize) {
            if (callbacks && callbacks->onHashProgress) {
                callbacks->onHashProgress(srcItem.relativePath, bytesHashed, fileSize, true);
            }
        };

        std::array<uint8_t, 32> srcHash{};
        std::array<uint8_t, 32> destHash{};

        bool srcOk = hashSession->HashFile(srcPath.wstring(), srcHash, &progress);
        if (!srcOk) {
            return true;
        }

        progress.onProgress = [&](unsigned long long bytesHashed, unsigned long long fileSize) {
            if (callbacks && callbacks->onHashProgress) {
                callbacks->onHashProgress(srcItem.relativePath, bytesHashed, fileSize, false);
            }
        };

        bool destOk = hashCache
            ? hashCache->GetOrCompute(srcItem.relativePath, destItem, destPath, *hashSession, destHash, &progress)
            : hashSession->HashFile(destPath.wstring(), destHash, &progress);
        if (!destOk) {
            return true;
        }
        return !FileHash::HashesEqual(srcHash, destHash);
    }

    SyncPlan BuildSyncPlan(const std::vector<SyncItem>& srcItems,
                           const std::vector<SyncItem>& destItems,
                           const std::filesystem::path& srcRoot,
                           const std::filesystem::path& destRoot,
                           const SyncOptions& options,
                           const SyncCallbacks& callbacks) {
        SyncPlan plan;
        const bool prune = options.prune;

        Sha256Session hashSession;
        SyncHashCache hashCache;
        const bool useSha256 = options.compareMode == CompareMode::Sha256 && hashSession.IsValid();
        if (useSha256 && std::filesystem::exists(destRoot)) {
            std::wstring cacheError;
            hashCache.Load(destRoot, cacheError);
        }

        std::unordered_map<std::wstring, SyncItem> destMap;
        for (const auto& item : destItems) {
            destMap[item.relativePath] = item;
        }

        std::unordered_set<std::wstring> srcRelPaths;
        for (const auto& item : srcItems) {
            srcRelPaths.insert(item.relativePath);
        }

        for (const auto& srcItem : srcItems) {
            auto it = destMap.find(srcItem.relativePath);
            if (srcItem.isReparsePoint) {
                if (it == destMap.end()) {
                    plan.linksToCreate.push_back(srcItem);
                } else {
                    const auto& destItem = it->second;
                    if (!destItem.isReparsePoint ||
                        srcItem.reparseTag != destItem.reparseTag ||
                        srcItem.reparseTarget != destItem.reparseTarget) {
                        plan.itemsToDelete.push_back({ destItem, DeleteReason::Replace });
                        plan.linksToCreate.push_back(srcItem);
                    }
                }
            } else if (srcItem.isDirectory) {
                if (it == destMap.end()) {
                    plan.dirsToCreate.push_back(srcItem);
                } else {
                    const auto& destItem = it->second;
                    if (destItem.isReparsePoint) {
                        plan.itemsToDelete.push_back({ destItem, DeleteReason::Replace });
                        plan.dirsToCreate.push_back(srcItem);
                    }
                }
            } else {
                bool needsCopy = false;
                if (it == destMap.end()) {
                    needsCopy = true;
                } else {
                    const auto& destItem = it->second;
                    if (destItem.isReparsePoint) {
                        plan.itemsToDelete.push_back({ destItem, DeleteReason::Replace });
                        needsCopy = true;
                    } else {
                        std::filesystem::path srcPath = WinPath::Join(srcRoot, srcItem.relativePath);
                        std::filesystem::path destPath = WinPath::Join(destRoot, srcItem.relativePath);
                        needsCopy = FileContentsDiffer(srcPath, destPath, srcItem, destItem, options.compareMode,
                                                       useSha256 ? &hashSession : nullptr,
                                                       useSha256 ? &hashCache : nullptr,
                                                       &callbacks);
                    }
                }

                if (needsCopy) {
                    plan.filesToCopy.push_back(srcItem);
                } else {
                    plan.filesSkipped++;
                }
            }
        }

        if (prune) {
            for (const auto& destItem : destItems) {
                if (srcRelPaths.find(destItem.relativePath) == srcRelPaths.end()) {
                    plan.itemsToDelete.push_back({ destItem, DeleteReason::Prune });
                }
            }
        }

        std::sort(plan.itemsToDelete.begin(), plan.itemsToDelete.end(), [](const PlannedDelete& a, const PlannedDelete& b) {
            return a.item.relativePath.length() > b.item.relativePath.length();
        });

        if (useSha256) {
            std::wstring cacheError;
            hashCache.Save(destRoot, cacheError);
        }

        return plan;
    }

    static std::wstring FormatBytesWide(unsigned long long bytes) {
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

    std::vector<PreviewItem> BuildPreviewList(const SyncPlan& plan,
                                              const std::unordered_map<std::wstring, SyncItem>& destMap) {
        std::vector<PreviewItem> previewList;

        for (const auto& dir : plan.dirsToCreate) {
            PreviewItem pi;
            pi.relativePath = dir.relativePath;
            pi.action = L"Create Dir";
            pi.sizeStr = L"-";
            pi.fileSize = 0;
            previewList.push_back(pi);
        }

        for (const auto& fileItem : plan.filesToCopy) {
            PreviewItem pi;
            pi.relativePath = fileItem.relativePath;
            pi.action = (destMap.find(fileItem.relativePath) == destMap.end()) ? L"Copy (New)" : L"Copy (Update)";
            pi.sizeStr = FormatBytesWide(fileItem.fileSize);
            pi.fileSize = fileItem.fileSize;
            previewList.push_back(pi);
        }

        for (const auto& linkItem : plan.linksToCreate) {
            PreviewItem pi;
            pi.relativePath = linkItem.relativePath;
            pi.action = (linkItem.reparseTag == IO_REPARSE_TAG_MOUNT_POINT) ? L"Create Junction" : L"Create Symlink";
            pi.sizeStr = L"-> " + linkItem.reparseTarget;
            pi.fileSize = 0;
            pi.isReparsePoint = true;
            pi.reparseTag = linkItem.reparseTag;
            pi.reparseTarget = linkItem.reparseTarget;
            previewList.push_back(pi);
        }

        for (const auto& plannedDelete : plan.itemsToDelete) {
            const auto& destItem = plannedDelete.item;
            PreviewItem pi;
            pi.relativePath = destItem.relativePath;
            pi.action = (plannedDelete.reason == DeleteReason::Replace) ? L"Remove (Replace)" : L"Delete (Prune)";
            pi.isReparsePoint = destItem.isReparsePoint;
            pi.reparseTag = destItem.reparseTag;
            pi.reparseTarget = destItem.reparseTarget;
            if (destItem.isReparsePoint) {
                pi.sizeStr = L"-> " + destItem.reparseTarget;
                pi.fileSize = 0;
            } else if (destItem.isDirectory) {
                pi.sizeStr = L"-";
                pi.fileSize = 0;
            } else {
                pi.sizeStr = FormatBytesWide(destItem.fileSize);
                pi.fileSize = destItem.fileSize;
            }
            previewList.push_back(pi);
        }

        return previewList;
    }

} // namespace PrevueSync

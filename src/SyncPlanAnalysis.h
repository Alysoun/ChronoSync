#pragma once

#include "SyncEngine.h"
#include "SyncPlan.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace ChronoSync {

    enum class RiskLevel {
        Low,
        Medium,
        High
    };

    struct LargestFileEntry {
        std::wstring relativePath;
        unsigned long long bytes = 0;
    };

    struct FileTypeBucket {
        std::wstring label;
        size_t fileCount = 0;
        unsigned long long totalBytes = 0;
    };

    struct PlannedRemovalEntry {
        std::wstring relativePath;
        std::wstring itemKind;
        DeleteReason reason = DeleteReason::Prune;
    };

    struct SyncPlanAnalysis {
        size_t filesToCopyNew = 0;
        size_t filesToCopyUpdate = 0;
        size_t dirsToCreate = 0;
        size_t linksToCreate = 0;
        size_t deletesPrune = 0;
        size_t deletesReplace = 0;
        size_t filesSkipped = 0;
        size_t totalActions = 0;
        unsigned long long totalBytesToTransfer = 0;
        double estimatedMinutesLow = 0.0;
        double estimatedMinutesHigh = 0.0;
        RiskLevel risk = RiskLevel::Low;
        std::vector<std::wstring> riskReasons;
        std::vector<LargestFileEntry> largestFiles;
        std::vector<PlannedRemovalEntry> plannedRemovals;
        std::vector<FileTypeBucket> fileTypeBreakdown;
        std::vector<FileTypeBucket> extensionBreakdown;
    };

    std::wstring RiskLevelToString(RiskLevel level);

    SyncPlanAnalysis AnalyzeSyncPlan(const SyncPlan& plan,
                                     const std::unordered_map<std::wstring, SyncItem>& destMap);

    std::wstring FormatSyncPlanReport(const SyncPlanAnalysis& analysis,
                                      const std::wstring& sourceLabel = L"",
                                      const std::wstring& destLabel = L"");

    struct SyncPlanReport {
        SyncPlanAnalysis analysis;
        std::vector<PreviewItem> previewItems;
    };

    SyncPlanReport BuildSyncPlanReport(const std::wstring& source,
                                       const std::wstring& destination,
                                       const SyncOptions& options,
                                       const SyncCallbacks& callbacks);

} // namespace ChronoSync

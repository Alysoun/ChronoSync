#include "SyncPlanAnalysis.h"
#include "WinPath.h"
#include <algorithm>
#include <cwctype>
#include <sstream>

#ifndef IO_REPARSE_TAG_MOUNT_POINT
#define IO_REPARSE_TAG_MOUNT_POINT 0xA0000003L
#endif

namespace ChronoSync {

    static std::wstring DescribeSyncItemKind(const SyncItem& item) {
        if (item.isReparsePoint) {
            return (item.reparseTag == IO_REPARSE_TAG_MOUNT_POINT) ? L"junction" : L"symlink";
        }
        return item.isDirectory ? L"directory" : L"file";
    }

    static void AppendRemovalPaths(std::wstringstream& out,
                                   const std::vector<PlannedRemovalEntry>& removals,
                                   DeleteReason reason,
                                   size_t maxListed) {
        size_t total = 0;
        for (const auto& entry : removals) {
            if (entry.reason == reason) {
                ++total;
            }
        }
        if (total == 0) {
            return;
        }

        size_t listed = 0;
        for (const auto& entry : removals) {
            if (entry.reason != reason) {
                continue;
            }
            if (listed >= maxListed) {
                out << L"    ... and " << (total - maxListed) << L" more\r\n";
                break;
            }
            out << L"    " << entry.relativePath << L" (" << entry.itemKind << L")\r\n";
            ++listed;
        }
    }

    static std::wstring ToLowerExt(const std::wstring& ext) {
        std::wstring lower = ext;
        for (wchar_t& ch : lower) {
            ch = static_cast<wchar_t>(towlower(ch));
        }
        return lower;
    }

    static std::wstring GetExtension(const std::wstring& relativePath) {
        size_t slash = relativePath.find_last_of(L"\\/");
        size_t dot = relativePath.find_last_of(L'.');
        if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) {
            return L"";
        }
        return ToLowerExt(relativePath.substr(dot + 1));
    }

    static std::wstring CategorizeExtension(const std::wstring& ext) {
        if (ext == L"jpg" || ext == L"jpeg" || ext == L"png" || ext == L"gif" || ext == L"webp" ||
            ext == L"bmp" || ext == L"tiff" || ext == L"svg" || ext == L"ico" || ext == L"heic") {
            return L"Images";
        }
        if (ext == L"cpp" || ext == L"c" || ext == L"h" || ext == L"hpp" || ext == L"cc" || ext == L"cxx" ||
            ext == L"cs" || ext == L"py" || ext == L"js" || ext == L"ts" || ext == L"tsx" || ext == L"jsx" ||
            ext == L"rs" || ext == L"go" || ext == L"java" || ext == L"kt" || ext == L"swift" ||
            ext == L"rb" || ext == L"php" || ext == L"sql" || ext == L"sh" || ext == L"bat" || ext == L"ps1") {
            return L"Source code";
        }
        if (ext == L"zip" || ext == L"7z" || ext == L"rar" || ext == L"tar" || ext == L"gz" || ext == L"bz2" ||
            ext == L"xz" || ext == L"pak" || ext == L"jar" || ext == L"msi") {
            return L"Archives";
        }
        if (ext == L"pkl" || ext == L"pt" || ext == L"pth" || ext == L"ckpt" || ext == L"safetensors" ||
            ext == L"onnx" || ext == L"bin" || ext == L"h5" || ext == L"pb") {
            return L"AI checkpoints";
        }
        if (ext == L"mp4" || ext == L"mkv" || ext == L"avi" || ext == L"mov" || ext == L"mp3" || ext == L"wav" ||
            ext == L"flac" || ext == L"ogg") {
            return L"Media";
        }
        if (ext == L"dll" || ext == L"exe" || ext == L"so" || ext == L"dylib") {
            return L"Binaries";
        }
        if (ext.empty()) {
            return L"No extension";
        }
        return L"Other";
    }

    static std::wstring FormatBytes(unsigned long long bytes) {
        double size = static_cast<double>(bytes);
        int unitIndex = 0;
        const wchar_t* units[] = { L"Bytes", L"KB", L"MB", L"GB", L"TB" };
        while (size >= 1024.0 && unitIndex < 4) {
            size /= 1024.0;
            unitIndex++;
        }
        wchar_t buf[64];
        swprintf_s(buf, L"%.2f %s", size, units[unitIndex]);
        return buf;
    }

    std::wstring RiskLevelToString(RiskLevel level) {
        switch (level) {
            case RiskLevel::Low: return L"LOW";
            case RiskLevel::Medium: return L"MEDIUM";
            case RiskLevel::High: return L"HIGH";
        }
        return L"LOW";
    }

    SyncPlanAnalysis AnalyzeSyncPlan(const SyncPlan& plan,
                                     const std::unordered_map<std::wstring, SyncItem>& destMap) {
        SyncPlanAnalysis analysis;
        analysis.dirsToCreate = plan.dirsToCreate.size();
        analysis.linksToCreate = plan.linksToCreate.size();
        analysis.filesSkipped = plan.filesSkipped;

        for (const auto& fileItem : plan.filesToCopy) {
            if (destMap.find(fileItem.relativePath) == destMap.end()) {
                analysis.filesToCopyNew++;
            } else {
                analysis.filesToCopyUpdate++;
            }
            analysis.totalBytesToTransfer += fileItem.fileSize;

            if (analysis.largestFiles.size() < 5) {
                analysis.largestFiles.push_back({ fileItem.relativePath, fileItem.fileSize });
                if (analysis.largestFiles.size() == 5) {
                    std::sort(analysis.largestFiles.begin(), analysis.largestFiles.end(),
                        [](const LargestFileEntry& a, const LargestFileEntry& b) { return a.bytes > b.bytes; });
                }
            } else if (fileItem.fileSize > analysis.largestFiles.back().bytes) {
                analysis.largestFiles.back() = { fileItem.relativePath, fileItem.fileSize };
                std::sort(analysis.largestFiles.begin(), analysis.largestFiles.end(),
                    [](const LargestFileEntry& a, const LargestFileEntry& b) { return a.bytes > b.bytes; });
            }

            std::wstring category = CategorizeExtension(GetExtension(fileItem.relativePath));
            auto bucketIt = std::find_if(analysis.fileTypeBreakdown.begin(), analysis.fileTypeBreakdown.end(),
                [&](const FileTypeBucket& bucket) { return bucket.label == category; });
            if (bucketIt == analysis.fileTypeBreakdown.end()) {
                analysis.fileTypeBreakdown.push_back({ category, 1, fileItem.fileSize });
            } else {
                bucketIt->fileCount++;
                bucketIt->totalBytes += fileItem.fileSize;
            }

            std::wstring ext = GetExtension(fileItem.relativePath);
            std::wstring extLabel = ext.empty() ? L"(no extension)" : (L"." + ext);
            auto extIt = std::find_if(analysis.extensionBreakdown.begin(), analysis.extensionBreakdown.end(),
                [&](const FileTypeBucket& bucket) { return bucket.label == extLabel; });
            if (extIt == analysis.extensionBreakdown.end()) {
                analysis.extensionBreakdown.push_back({ extLabel, 1, fileItem.fileSize });
            } else {
                extIt->fileCount++;
                extIt->totalBytes += fileItem.fileSize;
            }
        }

        std::sort(analysis.largestFiles.begin(), analysis.largestFiles.end(),
            [](const LargestFileEntry& a, const LargestFileEntry& b) { return a.bytes > b.bytes; });

        std::sort(analysis.fileTypeBreakdown.begin(), analysis.fileTypeBreakdown.end(),
            [](const FileTypeBucket& a, const FileTypeBucket& b) { return a.totalBytes > b.totalBytes; });

        std::sort(analysis.extensionBreakdown.begin(), analysis.extensionBreakdown.end(),
            [](const FileTypeBucket& a, const FileTypeBucket& b) { return a.totalBytes > b.totalBytes; });
        if (analysis.extensionBreakdown.size() > 12) {
            analysis.extensionBreakdown.resize(12);
        }

        for (const auto& plannedDelete : plan.itemsToDelete) {
            PlannedRemovalEntry removal;
            removal.relativePath = plannedDelete.item.relativePath;
            removal.itemKind = DescribeSyncItemKind(plannedDelete.item);
            removal.reason = plannedDelete.reason;
            analysis.plannedRemovals.push_back(removal);

            if (plannedDelete.reason == DeleteReason::Replace) {
                analysis.deletesReplace++;
            } else {
                analysis.deletesPrune++;
            }
        }

        analysis.totalActions = analysis.dirsToCreate + analysis.linksToCreate + plan.filesToCopy.size() +
                                plan.itemsToDelete.size();

        const double bytesPerSecondLow = 80.0 * 1024.0 * 1024.0;
        const double bytesPerSecondHigh = 150.0 * 1024.0 * 1024.0;
        if (analysis.totalBytesToTransfer > 0) {
            analysis.estimatedMinutesLow = (static_cast<double>(analysis.totalBytesToTransfer) / bytesPerSecondHigh) / 60.0;
            analysis.estimatedMinutesHigh = (static_cast<double>(analysis.totalBytesToTransfer) / bytesPerSecondLow) / 60.0;
            if (analysis.estimatedMinutesLow < 0.1) {
                analysis.estimatedMinutesLow = 0.1;
            }
            if (analysis.estimatedMinutesHigh < analysis.estimatedMinutesLow) {
                analysis.estimatedMinutesHigh = analysis.estimatedMinutesLow;
            }
        }

        const size_t totalDeletes = analysis.deletesPrune + analysis.deletesReplace;
        const size_t totalCopies = analysis.filesToCopyNew + analysis.filesToCopyUpdate;
        const unsigned long long gb50 = 50ULL * 1024ULL * 1024ULL * 1024ULL;
        const unsigned long long gb500 = 500ULL * 1024ULL * 1024ULL * 1024ULL;

        analysis.risk = RiskLevel::Low;

        if (totalDeletes > 100 || analysis.totalBytesToTransfer >= gb500 ||
            analysis.totalActions > 5000 || analysis.deletesReplace >= 3) {
            analysis.risk = RiskLevel::High;
        } else if (totalDeletes > 0 || analysis.totalBytesToTransfer >= gb50 ||
                   analysis.linksToCreate > 0 || totalCopies > 500 || analysis.deletesReplace > 0) {
            analysis.risk = RiskLevel::Medium;
        }

        if (totalDeletes == 0) {
            analysis.riskReasons.push_back(L"No deletions planned");
        } else {
            analysis.riskReasons.push_back(std::to_wstring(totalDeletes) + L" deletion(s) planned");
        }
        if (analysis.deletesReplace > 0) {
            analysis.riskReasons.push_back(std::to_wstring(analysis.deletesReplace) + L" destination replacement(s)");
            if (analysis.deletesReplace <= 3) {
                for (const auto& entry : analysis.plannedRemovals) {
                    if (entry.reason == DeleteReason::Replace) {
                        analysis.riskReasons.push_back(L"Replace: " + entry.relativePath + L" (" + entry.itemKind + L")");
                    }
                }
            }
        }
        if (analysis.linksToCreate > 0) {
            analysis.riskReasons.push_back(std::to_wstring(analysis.linksToCreate) + L" junction/symlink recreation(s)");
        }
        analysis.riskReasons.push_back(std::to_wstring(totalCopies) + L" file copy action(s)");
        analysis.riskReasons.push_back(L"Transfer size: " + FormatBytes(analysis.totalBytesToTransfer));

        return analysis;
    }

    std::wstring FormatSyncPlanReport(const SyncPlanAnalysis& analysis,
                                      const std::wstring& sourceLabel,
                                      const std::wstring& destLabel) {
        std::wstringstream out;
        out << L"ChronoSync Plan Analysis\r\n";
        out << L"========================\r\n\r\n";

        if (!sourceLabel.empty() || !destLabel.empty()) {
            if (!sourceLabel.empty()) {
                out << L"Source: " << sourceLabel << L"\r\n";
            }
            if (!destLabel.empty()) {
                out << L"Destination: " << destLabel << L"\r\n";
            }
            out << L"\r\n";
        }

        out << L"Risk: " << RiskLevelToString(analysis.risk) << L"\r\n";
        out << L"Reasons:\r\n";
        for (const auto& reason : analysis.riskReasons) {
            out << L"  - " << reason << L"\r\n";
        }
        out << L"\r\n";

        out << L"This sync will:\r\n";
        out << L"  Copy " << analysis.filesToCopyNew << L" new file(s)\r\n";
        out << L"  Update " << analysis.filesToCopyUpdate << L" existing file(s)\r\n";
        out << L"  Create " << analysis.dirsToCreate << L" director(ies)\r\n";
        out << L"  Recreate " << analysis.linksToCreate << L" junction(s)/symlink(s)\r\n";
        if (analysis.deletesPrune > 0) {
            out << L"  Archive/delete " << analysis.deletesPrune << L" pruned item(s)\r\n";
            AppendRemovalPaths(out, analysis.plannedRemovals, DeleteReason::Prune, 50);
        }
        if (analysis.deletesReplace > 0) {
            out << L"  Remove " << analysis.deletesReplace << L" destination item(s) to resolve type conflicts\r\n";
            AppendRemovalPaths(out, analysis.plannedRemovals, DeleteReason::Replace, 100);
        }
        out << L"  Skip " << analysis.filesSkipped << L" unchanged file(s)\r\n";
        out << L"  Transfer approximately " << FormatBytes(analysis.totalBytesToTransfer) << L"\r\n\r\n";

        if (analysis.estimatedMinutesLow > 0.0 || analysis.estimatedMinutesHigh > 0.0) {
            wchar_t timeBuf[64];
            if (analysis.estimatedMinutesHigh - analysis.estimatedMinutesLow < 0.5) {
                swprintf_s(timeBuf, L"%.1f minute(s)", analysis.estimatedMinutesHigh);
            } else {
                swprintf_s(timeBuf, L"%.0f-%.0f minutes", analysis.estimatedMinutesLow, analysis.estimatedMinutesHigh);
            }
            out << L"Estimated duration (local SSD): " << timeBuf << L"\r\n\r\n";
        }

        if (!analysis.largestFiles.empty()) {
            out << L"Largest files:\r\n";
            for (const auto& entry : analysis.largestFiles) {
                out << L"  " << entry.relativePath << L" (" << FormatBytes(entry.bytes) << L")\r\n";
            }
            out << L"\r\n";
        }

        if (!analysis.fileTypeBreakdown.empty()) {
            out << L"Categories by transfer size:\r\n";
            unsigned long long categorizedBytes = 0;
            for (const auto& bucket : analysis.fileTypeBreakdown) {
                categorizedBytes += bucket.totalBytes;
            }
            for (const auto& bucket : analysis.fileTypeBreakdown) {
                int pct = categorizedBytes > 0
                    ? static_cast<int>((bucket.totalBytes * 100) / categorizedBytes)
                    : 0;
                out << L"  " << pct << L"% " << bucket.label
                    << L" (" << bucket.fileCount << L" file(s), " << FormatBytes(bucket.totalBytes) << L")\r\n";
            }
            out << L"\r\n";
        }

        if (!analysis.extensionBreakdown.empty()) {
            out << L"Top extensions by transfer size:\r\n";
            for (const auto& bucket : analysis.extensionBreakdown) {
                out << L"  " << bucket.label
                    << L" \u2014 " << bucket.fileCount << L" file(s), " << FormatBytes(bucket.totalBytes) << L"\r\n";
            }
        }

        return out.str();
    }

    SyncPlanReport BuildSyncPlanReport(const std::wstring& source,
                                       const std::wstring& destination,
                                       const SyncOptions& options,
                                       const SyncCallbacks& callbacks) {
        SyncPlanReport report;
        std::filesystem::path srcRoot = WinPath::NormalizeRoot(source);
        std::filesystem::path destRoot = WinPath::NormalizeRoot(destination);

        if (!std::filesystem::exists(srcRoot)) {
            if (callbacks.onLog) {
                callbacks.onLog(L"Source directory does not exist: " + source, true);
            }
            return report;
        }

        std::vector<SyncItem> srcItems = SyncEngine::ScanDirectory(source, options.filters, callbacks);
        std::vector<SyncItem> destItems;
        if (std::filesystem::exists(destRoot)) {
            destItems = SyncEngine::ScanDirectory(destination, options.filters, callbacks);
        }

        if (callbacks.onCompareStart) {
            callbacks.onCompareStart();
        }

        std::unordered_map<std::wstring, SyncItem> destMap;
        for (const auto& item : destItems) {
            destMap[item.relativePath] = item;
        }

        SyncPlan plan = BuildSyncPlan(srcItems, destItems, srcRoot, destRoot, options, callbacks);
        report.previewItems = BuildPreviewList(plan, destMap);
        report.analysis = AnalyzeSyncPlan(plan, destMap);

        if (callbacks.onCompareComplete) {
            callbacks.onCompareComplete(plan.dirsToCreate.size(),
                                        plan.filesToCopy.size() + plan.linksToCreate.size(),
                                        plan.itemsToDelete.size());
        }

        return report;
    }

} // namespace ChronoSync

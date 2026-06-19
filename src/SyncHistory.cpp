#include "SyncHistory.h"

#include <windows.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <filesystem>

namespace PrevueSync {

    static std::string WideToUTF8(const std::wstring& wstr) {
        if (wstr.empty()) {
            return {};
        }
        int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
        std::string out(sizeNeeded, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), out.data(), sizeNeeded, nullptr, nullptr);
        return out;
    }

    static std::wstring UTF8ToWide(const std::string& str) {
        if (str.empty()) {
            return {};
        }
        int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
        std::wstring out(sizeNeeded, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), out.data(), sizeNeeded);
        return out;
    }

    static std::string EscapeJson(const std::wstring& value) {
        std::string utf8 = WideToUTF8(value);
        std::string escaped;
        for (char c : utf8) {
            switch (c) {
                case '\\': escaped += "\\\\"; break;
                case '"': escaped += "\\\""; break;
                case '\n': escaped += "\\n"; break;
                case '\r': escaped += "\\r"; break;
                case '\t': escaped += "\\t"; break;
                default: escaped += c; break;
            }
        }
        return escaped;
    }

    static std::wstring UnescapeJson(const std::string& value) {
        std::string unescaped;
        for (size_t i = 0; i < value.size(); ++i) {
            if (value[i] == '\\' && i + 1 < value.size()) {
                char next = value[i + 1];
                switch (next) {
                    case '\\': unescaped += '\\'; ++i; break;
                    case '"': unescaped += '"'; ++i; break;
                    case 'n': unescaped += '\n'; ++i; break;
                    case 'r': unescaped += '\r'; ++i; break;
                    case 't': unescaped += '\t'; ++i; break;
                    default: unescaped += value[i]; break;
                }
            } else {
                unescaped += value[i];
            }
        }
        return UTF8ToWide(unescaped);
    }

    static bool ExtractJsonString(const std::string& json, const std::string& key, std::wstring& outValue) {
        const std::string needle = "\"" + key + "\"";
        size_t keyPos = json.find(needle);
        if (keyPos == std::string::npos) {
            return false;
        }
        size_t colon = json.find(':', keyPos + needle.size());
        if (colon == std::string::npos) {
            return false;
        }
        size_t quoteStart = json.find('"', colon + 1);
        if (quoteStart == std::string::npos) {
            return false;
        }
        size_t i = quoteStart + 1;
        std::string raw;
        while (i < json.size()) {
            if (json[i] == '\\' && i + 1 < json.size()) {
                raw += json[i];
                raw += json[i + 1];
                i += 2;
                continue;
            }
            if (json[i] == '"') {
                outValue = UnescapeJson(raw);
                return true;
            }
            raw += json[i];
            ++i;
        }
        return false;
    }

    static bool ExtractJsonUInt64(const std::string& json, const std::string& key, unsigned long long& outValue) {
        const std::string needle = "\"" + key + "\"";
        size_t keyPos = json.find(needle);
        if (keyPos == std::string::npos) {
            return false;
        }
        size_t colon = json.find(':', keyPos + needle.size());
        if (colon == std::string::npos) {
            return false;
        }
        size_t start = colon + 1;
        while (start < json.size() && (json[start] == ' ' || json[start] == '\t')) {
            ++start;
        }
        size_t end = start;
        while (end < json.size() && json[end] >= '0' && json[end] <= '9') {
            ++end;
        }
        if (end == start) {
            return false;
        }
        outValue = std::stoull(json.substr(start, end - start));
        return true;
    }

    static bool ExtractJsonSizeT(const std::string& json, const std::string& key, size_t& outValue) {
        unsigned long long value = 0;
        if (!ExtractJsonUInt64(json, key, value)) {
            return false;
        }
        outValue = static_cast<size_t>(value);
        return true;
    }

    static bool ExtractJsonBool(const std::string& json, const std::string& key, bool& outValue) {
        const std::string needle = "\"" + key + "\"";
        size_t keyPos = json.find(needle);
        if (keyPos == std::string::npos) {
            return false;
        }
        size_t colon = json.find(':', keyPos + needle.size());
        if (colon == std::string::npos) {
            return false;
        }
        if (json.find("true", colon) != std::string::npos && json.find("true", colon) < colon + 12) {
            outValue = true;
            return true;
        }
        if (json.find("false", colon) != std::string::npos && json.find("false", colon) < colon + 12) {
            outValue = false;
            return true;
        }
        return false;
    }

    static std::wstring FormatUtcNow() {
        SYSTEMTIME st = {};
        GetSystemTime(&st);
        wchar_t buf[32];
        swprintf_s(buf, L"%04d-%02d-%02dT%02d:%02d:%02dZ",
                   st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        return buf;
    }

    static std::wstring MakeRunId(const std::wstring& timestampUtc) {
        std::wstring id = timestampUtc;
        for (wchar_t& ch : id) {
            if (ch == L':' || ch == L'-') {
                ch = L'_';
            }
        }
        return id;
    }

    static std::filesystem::path HistoryRoot(const std::wstring& destination) {
        return std::filesystem::path(destination) / L".prevue_history";
    }

    static std::filesystem::path IndexPath(const std::wstring& destination) {
        return HistoryRoot(destination) / L"index.json";
    }

    static std::filesystem::path SnapshotPath(const std::wstring& destination, const std::wstring& snapshotId) {
        return HistoryRoot(destination) / L"snapshots" / (snapshotId + L".json");
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

    static bool ReadFileUtf8(const std::filesystem::path& path, std::string& content, std::wstring& errorMessage) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            errorMessage = L"Failed to open file: " + path.wstring();
            return false;
        }
        content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        return true;
    }

    static bool WriteFileUtf8(const std::filesystem::path& path, const std::string& content, std::wstring& errorMessage) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            errorMessage = L"Failed to write file: " + path.wstring();
            return false;
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        return true;
    }

    static DestinationSnapshot BuildSnapshotFromScan(const std::wstring& destination,
                                                     const SyncOptions& options,
                                                     const std::wstring& snapshotId,
                                                     const std::wstring& timestampUtc) {
        DestinationSnapshot snapshot;
        snapshot.id = snapshotId;
        snapshot.timestampUtc = timestampUtc;

        SyncCallbacks callbacks;
        std::vector<SyncItem> items = SyncEngine::ScanDirectory(destination, options.filters, callbacks);
        snapshot.entries.reserve(items.size());
        for (const auto& item : items) {
            SnapshotFileEntry entry;
            entry.relativePath = item.relativePath;
            entry.fileSize = item.fileSize;
            entry.mtimeLow = item.lastWriteTime.dwLowDateTime;
            entry.mtimeHigh = item.lastWriteTime.dwHighDateTime;
            entry.isDirectory = item.isDirectory;
            snapshot.entries.push_back(std::move(entry));
        }
        return snapshot;
    }

    static std::string SerializeSnapshot(const DestinationSnapshot& snapshot) {
        std::ostringstream out;
        out << "{\n";
        out << "  \"version\": 1,\n";
        out << "  \"id\": \"" << EscapeJson(snapshot.id) << "\",\n";
        out << "  \"timestampUtc\": \"" << EscapeJson(snapshot.timestampUtc) << "\",\n";
        out << "  \"entries\": [\n";
        for (size_t i = 0; i < snapshot.entries.size(); ++i) {
            const auto& e = snapshot.entries[i];
            out << "    {";
            out << "\"path\": \"" << EscapeJson(e.relativePath) << "\", ";
            out << "\"size\": " << e.fileSize << ", ";
            out << "\"mtimeLow\": " << e.mtimeLow << ", ";
            out << "\"mtimeHigh\": " << e.mtimeHigh << ", ";
            out << "\"isDir\": " << (e.isDirectory ? "true" : "false");
            out << "}";
            if (i + 1 < snapshot.entries.size()) {
                out << ",";
            }
            out << "\n";
        }
        out << "  ]\n";
        out << "}\n";
        return out.str();
    }

    static bool ParseSnapshot(const std::string& json, DestinationSnapshot& snapshot, std::wstring& errorMessage) {
        (void)errorMessage;
        ExtractJsonString(json, "id", snapshot.id);
        ExtractJsonString(json, "timestampUtc", snapshot.timestampUtc);

        size_t entriesPos = json.find("\"entries\"");
        if (entriesPos == std::string::npos) {
            return false;
        }
        size_t bracketStart = json.find('[', entriesPos);
        size_t bracketEnd = json.rfind(']');
        if (bracketStart == std::string::npos || bracketEnd == std::string::npos || bracketEnd <= bracketStart) {
            return false;
        }

        size_t pos = bracketStart;
        while (pos < bracketEnd) {
            size_t objStart = json.find('{', pos);
            if (objStart == std::string::npos || objStart >= bracketEnd) {
                break;
            }
            size_t objEnd = json.find('}', objStart);
            if (objEnd == std::string::npos || objEnd > bracketEnd) {
                break;
            }
            std::string obj = json.substr(objStart, objEnd - objStart + 1);

            SnapshotFileEntry entry;
            ExtractJsonString(obj, "path", entry.relativePath);
            unsigned long long sizeVal = 0;
            ExtractJsonUInt64(obj, "size", sizeVal);
            entry.fileSize = sizeVal;
            unsigned long long low = 0;
            unsigned long long high = 0;
            ExtractJsonUInt64(obj, "mtimeLow", low);
            ExtractJsonUInt64(obj, "mtimeHigh", high);
            entry.mtimeLow = static_cast<unsigned long>(low);
            entry.mtimeHigh = static_cast<unsigned long>(high);
            ExtractJsonBool(obj, "isDir", entry.isDirectory);
            snapshot.entries.push_back(std::move(entry));
            pos = objEnd + 1;
        }
        return true;
    }

    static std::string SerializeRunObject(const SyncHistoryEntry& entry) {
        std::ostringstream out;
        out << "    {\n";
        out << "      \"id\": \"" << EscapeJson(entry.id) << "\",\n";
        out << "      \"timestampUtc\": \"" << EscapeJson(entry.timestampUtc) << "\",\n";
        out << "      \"source\": \"" << EscapeJson(entry.source) << "\",\n";
        out << "      \"destination\": \"" << EscapeJson(entry.destination) << "\",\n";
        out << "      \"filesCopied\": " << entry.filesCopied << ",\n";
        out << "      \"filesSkipped\": " << entry.filesSkipped << ",\n";
        out << "      \"itemsDeleted\": " << entry.itemsDeleted << ",\n";
        out << "      \"dirsCreated\": " << entry.dirsCreated << ",\n";
        out << "      \"totalBytesCopied\": " << entry.totalBytesCopied << ",\n";
        out << "      \"snapshotId\": \"" << EscapeJson(entry.snapshotId) << "\"\n";
        out << "    }";
        return out.str();
    }

    static bool ParseRunObject(const std::string& obj, SyncHistoryEntry& entry) {
        ExtractJsonString(obj, "id", entry.id);
        ExtractJsonString(obj, "timestampUtc", entry.timestampUtc);
        ExtractJsonString(obj, "source", entry.source);
        ExtractJsonString(obj, "destination", entry.destination);
        ExtractJsonSizeT(obj, "filesCopied", entry.filesCopied);
        ExtractJsonSizeT(obj, "filesSkipped", entry.filesSkipped);
        ExtractJsonSizeT(obj, "itemsDeleted", entry.itemsDeleted);
        ExtractJsonSizeT(obj, "dirsCreated", entry.dirsCreated);
        unsigned long long bytes = 0;
        ExtractJsonUInt64(obj, "totalBytesCopied", bytes);
        entry.totalBytesCopied = bytes;
        ExtractJsonString(obj, "snapshotId", entry.snapshotId);
        return !entry.id.empty();
    }

    static bool LoadEntriesFromJson(const std::string& json, std::vector<SyncHistoryEntry>& entries) {
        entries.clear();
        size_t runsPos = json.find("\"runs\"");
        if (runsPos == std::string::npos) {
            return true;
        }
        size_t bracketStart = json.find('[', runsPos);
        size_t bracketEnd = json.rfind(']');
        if (bracketStart == std::string::npos || bracketEnd == std::string::npos) {
            return true;
        }

        size_t pos = bracketStart;
        while (pos < bracketEnd) {
            size_t objStart = json.find('{', pos);
            if (objStart == std::string::npos || objStart >= bracketEnd) {
                break;
            }
            size_t objEnd = json.find('}', objStart);
            if (objEnd == std::string::npos || objEnd > bracketEnd) {
                break;
            }
            SyncHistoryEntry entry;
            if (ParseRunObject(json.substr(objStart, objEnd - objStart + 1), entry)) {
                entries.push_back(std::move(entry));
            }
            pos = objEnd + 1;
        }
        return true;
    }

    static std::string SerializeIndex(const std::vector<SyncHistoryEntry>& entries) {
        std::ostringstream out;
        out << "{\n  \"version\": 1,\n  \"runs\": [\n";
        for (size_t i = 0; i < entries.size(); ++i) {
            out << SerializeRunObject(entries[i]);
            if (i + 1 < entries.size()) {
                out << ",";
            }
            out << "\n";
        }
        out << "  ]\n}\n";
        return out.str();
    }

    static void PruneOldRuns(std::vector<SyncHistoryEntry>& entries, const std::wstring& destination) {
        while (entries.size() > static_cast<size_t>(SyncHistoryIO::MaxRuns)) {
            const auto& oldest = entries.front();
            if (!oldest.snapshotId.empty()) {
                std::error_code ec;
                std::filesystem::remove(SnapshotPath(destination, oldest.snapshotId), ec);
            }
            entries.erase(entries.begin());
        }
    }

    static constexpr size_t kMaxSnapshotEntries = SyncHistoryIO::MaxSnapshotEntries;

    bool SyncHistoryIO::RecordRun(const std::wstring& source,
                                  const std::wstring& destination,
                                  const SyncOptions& options,
                                  const SyncStats& stats,
                                  std::wstring& errorMessage) {
        try {
            std::error_code ec;
            if (!std::filesystem::exists(destination, ec)) {
                errorMessage = L"Destination does not exist; history not recorded.";
                return false;
            }

            std::wstring timestampUtc = FormatUtcNow();
            std::wstring runId = MakeRunId(timestampUtc);

            SyncHistoryEntry entry;
            entry.id = runId;
            entry.timestampUtc = timestampUtc;
            entry.source = source;
            entry.destination = destination;
            entry.filesCopied = stats.filesCopied;
            entry.filesSkipped = stats.filesSkipped;
            entry.itemsDeleted = stats.itemsDeleted;
            entry.dirsCreated = stats.dirsCreated;
            entry.totalBytesCopied = stats.totalBytesCopied;
            entry.snapshotId = runId;

            DestinationSnapshot snapshot = BuildSnapshotFromScan(destination, options, runId, timestampUtc);
            if (snapshot.entries.size() <= kMaxSnapshotEntries) {
                if (!WriteFileUtf8(SnapshotPath(destination, runId), SerializeSnapshot(snapshot), errorMessage)) {
                    return false;
                }
            } else {
                entry.snapshotId.clear();
                errorMessage = L"Run logged without full snapshot (tree has " +
                               std::to_wstring(snapshot.entries.size()) + L" items).";
            }

            std::vector<SyncHistoryEntry> entries;
            std::string indexJson;
            auto indexFile = IndexPath(destination);
            if (std::filesystem::exists(indexFile, ec)) {
                if (!ReadFileUtf8(indexFile, indexJson, errorMessage)) {
                    return false;
                }
                LoadEntriesFromJson(indexJson, entries);
            }

            entries.push_back(std::move(entry));
            PruneOldRuns(entries, destination);

            if (!WriteFileUtf8(indexFile, SerializeIndex(entries), errorMessage)) {
                return false;
            }
            return true;
        } catch (const std::exception& ex) {
            errorMessage = L"History recording failed: " +
                           std::wstring(ex.what(), ex.what() + std::strlen(ex.what()));
            return false;
        } catch (...) {
            errorMessage = L"History recording failed with an unexpected error.";
            return false;
        }
    }

    bool SyncHistoryIO::LoadEntries(const std::wstring& destination,
                                    std::vector<SyncHistoryEntry>& entries,
                                    std::wstring& errorMessage) {
        entries.clear();
        std::error_code ec;
        auto indexFile = IndexPath(destination);
        if (!std::filesystem::exists(indexFile, ec)) {
            return true;
        }
        std::string indexJson;
        if (!ReadFileUtf8(indexFile, indexJson, errorMessage)) {
            return false;
        }
        return LoadEntriesFromJson(indexJson, entries);
    }

    std::vector<SyncHistoryEntry> SyncHistoryIO::QuerySinceDays(const std::wstring& destination, int days) {
        std::vector<SyncHistoryEntry> all;
        std::wstring error;
        if (!LoadEntries(destination, all, error) || days <= 0) {
            return all;
        }

        FILETIME nowFt = {};
        GetSystemTimeAsFileTime(&nowFt);
        ULARGE_INTEGER now;
        now.LowPart = nowFt.dwLowDateTime;
        now.HighPart = nowFt.dwHighDateTime;
        const unsigned long long dayTicks = 24ULL * 60ULL * 60ULL * 10000000ULL;
        const unsigned long long cutoff = now.QuadPart - static_cast<unsigned long long>(days) * dayTicks;

        std::vector<SyncHistoryEntry> filtered;
        for (const auto& entry : all) {
            SYSTEMTIME st = {};
            if (swscanf_s(entry.timestampUtc.c_str(), L"%hu-%hu-%huT%hu:%hu:%hu",
                          &st.wYear, &st.wMonth, &st.wDay, &st.wHour, &st.wMinute, &st.wSecond) != 6) {
                filtered.push_back(entry);
                continue;
            }
            FILETIME entryFt = {};
            SystemTimeToFileTime(&st, &entryFt);
            ULARGE_INTEGER entryTime;
            entryTime.LowPart = entryFt.dwLowDateTime;
            entryTime.HighPart = entryFt.dwHighDateTime;
            if (entryTime.QuadPart >= cutoff) {
                filtered.push_back(entry);
            }
        }
        return filtered;
    }

    bool SyncHistoryIO::LoadSnapshot(const std::wstring& destination,
                                     const std::wstring& snapshotId,
                                     DestinationSnapshot& snapshot,
                                     std::wstring& errorMessage) {
        if (snapshotId.empty()) {
            errorMessage = L"No snapshot was saved for this run. Destinations larger than " +
                           std::to_wstring(MaxSnapshotEntries) +
                           L" items are logged with run stats only.";
            return false;
        }

        std::string json;
        if (!ReadFileUtf8(SnapshotPath(destination, snapshotId), json, errorMessage)) {
            return false;
        }
        snapshot = {};
        return ParseSnapshot(json, snapshot, errorMessage);
    }

    SnapshotDiffResult SyncHistoryIO::DiffSnapshots(const DestinationSnapshot& older,
                                                    const DestinationSnapshot& newer) {
        SnapshotDiffResult result;
        std::unordered_map<std::wstring, const SnapshotFileEntry*> olderMap;
        std::unordered_map<std::wstring, const SnapshotFileEntry*> newerMap;

        for (const auto& e : older.entries) {
            olderMap[e.relativePath] = &e;
        }
        for (const auto& e : newer.entries) {
            newerMap[e.relativePath] = &e;
        }

        for (const auto& [path, newEntry] : newerMap) {
            auto it = olderMap.find(path);
            if (it == olderMap.end()) {
                result.added++;
                if (!newEntry->isDirectory) {
                    result.addedBytes += newEntry->fileSize;
                }
                if (result.sampleAdded.size() < 8) {
                    result.sampleAdded.push_back(L"+ " + path);
                }
            } else {
                const auto* oldEntry = it->second;
                bool changed = oldEntry->isDirectory != newEntry->isDirectory ||
                    oldEntry->fileSize != newEntry->fileSize ||
                    oldEntry->mtimeLow != newEntry->mtimeLow ||
                    oldEntry->mtimeHigh != newEntry->mtimeHigh;
                if (changed) {
                    result.modified++;
                    if (!newEntry->isDirectory) {
                        result.modifiedBytes += newEntry->fileSize;
                    }
                    if (result.sampleModified.size() < 8) {
                        result.sampleModified.push_back(L"~ " + path);
                    }
                }
            }
        }

        for (const auto& [path, oldEntry] : olderMap) {
            if (newerMap.find(path) == newerMap.end()) {
                result.removed++;
                if (!oldEntry->isDirectory) {
                    result.removedBytes += oldEntry->fileSize;
                }
                if (result.sampleRemoved.size() < 8) {
                    result.sampleRemoved.push_back(L"- " + path);
                }
            }
        }
        return result;
    }

    std::wstring SyncHistoryIO::FormatHistoryReport(const std::vector<SyncHistoryEntry>& entries, int daysFilter) {
        std::wstringstream out;
        out << L"PrevueSync History\r\n";
        out << L"=================\r\n\r\n";
        if (daysFilter > 0) {
            out << L"Showing runs from the last " << daysFilter << L" day(s).\r\n\r\n";
        }
        if (entries.empty()) {
            out << L"No sync history recorded yet.\r\n";
            out << L"History is saved under .prevue_history after each sync.\r\n";
            return out.str();
        }

        size_t totalCopied = 0;
        size_t totalDeleted = 0;
        unsigned long long totalBytes = 0;
        size_t runsWithSnapshot = 0;
        for (const auto& e : entries) {
            totalCopied += e.filesCopied;
            totalDeleted += e.itemsDeleted;
            totalBytes += e.totalBytesCopied;
            if (!e.snapshotId.empty()) {
                runsWithSnapshot++;
            }
        }
        out << L"Runs: " << entries.size()
            << L" | Files copied: " << totalCopied
            << L" | Items removed: " << totalDeleted
            << L" | Bytes transferred: " << FormatBytes(totalBytes) << L"\r\n";
        out << L"Runs with snapshots: " << runsWithSnapshot << L" of " << entries.size()
            << L" (snapshots saved when destination has \u2264 "
            << MaxSnapshotEntries << L" items)\r\n\r\n";

        for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
            const auto& e = *it;
            out << e.timestampUtc << L"\r\n";
            out << L"  Copied: " << e.filesCopied
                << L" | Skipped: " << e.filesSkipped
                << L" | Deleted: " << e.itemsDeleted
                << L" | Dirs: " << e.dirsCreated
                << L" | " << FormatBytes(e.totalBytesCopied) << L"\r\n";
            out << L"  Source: " << e.source << L"\r\n";
            if (e.snapshotId.empty()) {
                out << L"  Snapshot: not saved (destination exceeds " << MaxSnapshotEntries
                    << L" items; run stats only)\r\n\r\n";
            } else {
                out << L"  Snapshot: saved (" << e.snapshotId << L")\r\n\r\n";
            }
        }
        return out.str();
    }

    std::wstring SyncHistoryIO::FormatSnapshotDiffReport(const SnapshotDiffResult& diff,
                                                         const std::wstring& labelOlder,
                                                         const std::wstring& labelNewer) {
        std::wstringstream out;
        out << L"Snapshot Diff\r\n";
        out << L"=============\r\n\r\n";
        out << L"Older: " << labelOlder << L"\r\n";
        out << L"Newer: " << labelNewer << L"\r\n\r\n";
        out << L"  + Added:    " << diff.added << L" (" << FormatBytes(diff.addedBytes) << L")\r\n";
        out << L"  - Removed:  " << diff.removed << L" (" << FormatBytes(diff.removedBytes) << L")\r\n";
        out << L"  ~ Modified: " << diff.modified << L" (" << FormatBytes(diff.modifiedBytes) << L")\r\n\r\n";

        auto writeSamples = [&](const wchar_t* title, const std::vector<std::wstring>& samples) {
            if (samples.empty()) {
                return;
            }
            out << title << L"\r\n";
            for (const auto& line : samples) {
                out << L"  " << line << L"\r\n";
            }
            out << L"\r\n";
        };

        writeSamples(L"Sample additions:", diff.sampleAdded);
        writeSamples(L"Sample removals:", diff.sampleRemoved);
        writeSamples(L"Sample modifications:", diff.sampleModified);
        return out.str();
    }

} // namespace PrevueSync

#include "SyncJob.h"
#include <windows.h>
#include <fstream>
#include <sstream>

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

    static void WritePatternArray(std::ostream& out, const char* key, const std::vector<std::wstring>& patterns) {
        out << "      \"" << key << "\": [";
        for (size_t i = 0; i < patterns.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << "\"" << EscapeJson(patterns[i]) << "\"";
        }
        out << "],\n";
    }

    static std::vector<std::wstring> ExtractJsonStringArray(const std::string& objectJson, const std::string& key) {
        std::vector<std::wstring> values;
        const std::string needle = "\"" + key + "\"";
        size_t keyPos = objectJson.find(needle);
        if (keyPos == std::string::npos) {
            return values;
        }
        size_t bracketStart = objectJson.find('[', keyPos);
        size_t bracketEnd = objectJson.find(']', bracketStart == std::string::npos ? keyPos : bracketStart);
        if (bracketStart == std::string::npos || bracketEnd == std::string::npos) {
            return values;
        }

        size_t i = bracketStart + 1;
        while (i < bracketEnd) {
            size_t quoteStart = objectJson.find('"', i);
            if (quoteStart == std::string::npos || quoteStart >= bracketEnd) {
                break;
            }
            size_t j = quoteStart + 1;
            std::string raw;
            while (j < bracketEnd) {
                if (objectJson[j] == '\\' && j + 1 < objectJson.size()) {
                    raw += objectJson[j];
                    raw += objectJson[j + 1];
                    j += 2;
                    continue;
                }
                if (objectJson[j] == '"') {
                    values.push_back(UnescapeJson(raw));
                    i = j + 1;
                    break;
                }
                raw += objectJson[j];
                ++j;
            }
            if (j >= bracketEnd) {
                break;
            }
        }
        return values;
    }

    static bool ExtractJsonString(const std::string& json, const std::string& key, std::wstring& outValue) {
        const std::string needle = "\"" + key + "\"";
        size_t keyPos = json.find(needle);
        if (keyPos == std::string::npos) {
            return false;
        }
        size_t colon = json.find(':', keyPos + needle.size());
        size_t quoteStart = json.find('"', colon + 1);
        if (colon == std::string::npos || quoteStart == std::string::npos) {
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
        size_t truePos = json.find("true", colon + 1);
        size_t falsePos = json.find("false", colon + 1);
        if (truePos != std::string::npos && (falsePos == std::string::npos || truePos < falsePos)) {
            outValue = true;
            return true;
        }
        if (falsePos != std::string::npos) {
            outValue = false;
            return true;
        }
        return false;
    }

    static bool ExtractJsonNumber(const std::string& json, const std::string& key, size_t& outValue) {
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
        outValue = static_cast<size_t>(std::stoull(json.substr(start, end - start)));
        return true;
    }

    static SyncJob ParseJobObject(const std::string& objectJson) {
        SyncJob job;
        ExtractJsonString(objectJson, "name", job.name);
        ExtractJsonString(objectJson, "source", job.source);
        ExtractJsonString(objectJson, "destination", job.destination);
        ExtractJsonBool(objectJson, "prune", job.options.prune);
        bool sha256 = false;
        if (ExtractJsonBool(objectJson, "sha256Compare", sha256) && sha256) {
            job.options.compareMode = CompareMode::Sha256;
        }
        ExtractJsonBool(objectJson, "verifyAfterCopy", job.options.verifyAfterCopy);
        ExtractJsonBool(objectJson, "versionedBackups", job.options.versionedBackups);
        ExtractJsonBool(objectJson, "deltaBlockCopy", job.options.deltaBlockCopy);
        ExtractJsonNumber(objectJson, "maxBackupVersions", job.options.maxBackupVersions);
        job.options.filters.includePatterns = ExtractJsonStringArray(objectJson, "includePatterns");
        auto exclude = ExtractJsonStringArray(objectJson, "excludePatterns");
        if (!exclude.empty()) {
            job.options.filters.excludePatterns = std::move(exclude);
        } else if (job.options.filters.includePatterns.empty()) {
            job.options.filters = FilterOptions::Defaults();
        }
        if (job.name.empty()) {
            job.name = L"Queued Job";
        }
        return job;
    }

    bool SyncJobQueueIO::SaveToFile(const std::vector<SyncJob>& jobs, const std::wstring& filePath, std::wstring& errorMessage) {
        std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
        if (!out) {
            errorMessage = L"Unable to open queue file for writing.";
            return false;
        }

        out << "{\n  \"version\": 1,\n  \"jobs\": [\n";
        for (size_t i = 0; i < jobs.size(); ++i) {
            const auto& job = jobs[i];
            out << "    {\n";
            out << "      \"name\": \"" << EscapeJson(job.name) << "\",\n";
            out << "      \"source\": \"" << EscapeJson(job.source) << "\",\n";
            out << "      \"destination\": \"" << EscapeJson(job.destination) << "\",\n";
            out << "      \"prune\": " << (job.options.prune ? "true" : "false") << ",\n";
            out << "      \"sha256Compare\": " << (job.options.compareMode == CompareMode::Sha256 ? "true" : "false") << ",\n";
            out << "      \"verifyAfterCopy\": " << (job.options.verifyAfterCopy ? "true" : "false") << ",\n";
            out << "      \"versionedBackups\": " << (job.options.versionedBackups ? "true" : "false") << ",\n";
            out << "      \"deltaBlockCopy\": " << (job.options.deltaBlockCopy ? "true" : "false") << ",\n";
            out << "      \"maxBackupVersions\": " << job.options.maxBackupVersions << ",\n";
            WritePatternArray(out, "includePatterns", job.options.filters.includePatterns);
            WritePatternArray(out, "excludePatterns", job.options.filters.excludePatterns);
            out << "    }";
            if (i + 1 < jobs.size()) {
                out << ",";
            }
            out << "\n";
        }
        out << "  ]\n}\n";
        return out.good();
    }

    bool SyncJobQueueIO::LoadFromFile(const std::wstring& filePath, std::vector<SyncJob>& jobs, std::wstring& errorMessage) {
        std::ifstream in(filePath, std::ios::binary);
        if (!in) {
            errorMessage = L"Unable to open queue file.";
            return false;
        }
        std::ostringstream buffer;
        buffer << in.rdbuf();
        const std::string json = buffer.str();
        if (json.empty()) {
            errorMessage = L"Queue file is empty.";
            return false;
        }

        std::vector<SyncJob> loaded;
        size_t searchPos = 0;
        while (true) {
            size_t objectStart = json.find('{', searchPos);
            if (objectStart == std::string::npos) {
                break;
            }
            if (json.compare(objectStart, 8, "{\"version\"") == 0 ||
                json.compare(objectStart, 7, "{ \"vers") == 0) {
                searchPos = objectStart + 1;
                continue;
            }

            int depth = 0;
            size_t objectEnd = std::string::npos;
            for (size_t i = objectStart; i < json.size(); ++i) {
                if (json[i] == '{') {
                    ++depth;
                } else if (json[i] == '}') {
                    --depth;
                    if (depth == 0) {
                        objectEnd = i;
                        break;
                    }
                }
            }
            if (objectEnd == std::string::npos) {
                break;
            }

            std::string objectJson = json.substr(objectStart, objectEnd - objectStart + 1);
            if (objectJson.find("\"source\"") != std::string::npos &&
                objectJson.find("\"destination\"") != std::string::npos) {
                loaded.push_back(ParseJobObject(objectJson));
            }
            searchPos = objectEnd + 1;
        }

        if (loaded.empty()) {
            errorMessage = L"No jobs found in queue file.";
            return false;
        }

        jobs = std::move(loaded);
        return true;
    }

} // namespace PrevueSync

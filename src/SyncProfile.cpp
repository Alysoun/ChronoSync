#include "SyncProfile.h"
#include <windows.h>
#include <fstream>
#include <sstream>

namespace ChronoSync {

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
        escaped.reserve(utf8.size() + 8);
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
        unescaped.reserve(value.size());
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

    static std::vector<std::wstring> ExtractJsonStringArray(const std::string& json, const std::string& key) {
        std::vector<std::wstring> values;
        const std::string needle = "\"" + key + "\"";
        size_t keyPos = json.find(needle);
        if (keyPos == std::string::npos) {
            return values;
        }
        size_t bracketStart = json.find('[', keyPos);
        size_t bracketEnd = json.find(']', bracketStart == std::string::npos ? keyPos : bracketStart);
        if (bracketStart == std::string::npos || bracketEnd == std::string::npos || bracketEnd <= bracketStart) {
            return values;
        }

        size_t i = bracketStart + 1;
        while (i < bracketEnd) {
            size_t quoteStart = json.find('"', i);
            if (quoteStart == std::string::npos || quoteStart >= bracketEnd) {
                break;
            }
            size_t j = quoteStart + 1;
            std::string raw;
            while (j < bracketEnd) {
                if (json[j] == '\\' && j + 1 < json.size()) {
                    raw += json[j];
                    raw += json[j + 1];
                    j += 2;
                    continue;
                }
                if (json[j] == '"') {
                    values.push_back(UnescapeJson(raw));
                    i = j + 1;
                    break;
                }
                raw += json[j];
                ++j;
            }
            if (j >= bracketEnd) {
                break;
            }
        }
        return values;
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

    bool SyncProfileIO::SaveToFile(const SyncProfile& profile, const std::wstring& filePath, std::wstring& errorMessage) {
        std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
        if (!out) {
            errorMessage = L"Unable to open file for writing.";
            return false;
        }

        out << "{\n";
        out << "  \"version\": 1,\n";
        out << "  \"name\": \"" << EscapeJson(profile.name) << "\",\n";
        out << "  \"source\": \"" << EscapeJson(profile.source) << "\",\n";
        out << "  \"destination\": \"" << EscapeJson(profile.destination) << "\",\n";
        out << "  \"prune\": " << (profile.options.prune ? "true" : "false") << ",\n";
        out << "  \"sha256Compare\": " << (profile.options.compareMode == CompareMode::Sha256 ? "true" : "false") << ",\n";
        out << "  \"verifyAfterCopy\": " << (profile.options.verifyAfterCopy ? "true" : "false") << ",\n";
        out << "  \"versionedBackups\": " << (profile.options.versionedBackups ? "true" : "false") << ",\n";
        out << "  \"maxBackupVersions\": " << profile.options.maxBackupVersions << ",\n";
        out << "  \"includePatterns\": [";
        for (size_t i = 0; i < profile.options.filters.includePatterns.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << "\"" << EscapeJson(profile.options.filters.includePatterns[i]) << "\"";
        }
        out << "],\n";
        out << "  \"excludePatterns\": [";
        for (size_t i = 0; i < profile.options.filters.excludePatterns.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << "\"" << EscapeJson(profile.options.filters.excludePatterns[i]) << "\"";
        }
        out << "]\n";
        out << "}\n";

        if (!out.good()) {
            errorMessage = L"Failed while writing profile file.";
            return false;
        }
        return true;
    }

    bool SyncProfileIO::LoadFromFile(const std::wstring& filePath, SyncProfile& profile, std::wstring& errorMessage) {
        std::ifstream in(filePath, std::ios::binary);
        if (!in) {
            errorMessage = L"Unable to open profile file.";
            return false;
        }

        std::ostringstream buffer;
        buffer << in.rdbuf();
        const std::string json = buffer.str();
        if (json.empty()) {
            errorMessage = L"Profile file is empty.";
            return false;
        }

        SyncProfile loaded;
        if (!ExtractJsonString(json, "name", loaded.name)) {
            loaded.name = L"Imported Profile";
        }
        if (!ExtractJsonString(json, "source", loaded.source)) {
            errorMessage = L"Profile is missing a source path.";
            return false;
        }
        if (!ExtractJsonString(json, "destination", loaded.destination)) {
            errorMessage = L"Profile is missing a destination path.";
            return false;
        }
        ExtractJsonBool(json, "prune", loaded.options.prune);
        bool sha256 = false;
        if (ExtractJsonBool(json, "sha256Compare", sha256) && sha256) {
            loaded.options.compareMode = CompareMode::Sha256;
        }
        ExtractJsonBool(json, "verifyAfterCopy", loaded.options.verifyAfterCopy);
        ExtractJsonBool(json, "versionedBackups", loaded.options.versionedBackups);
        ExtractJsonNumber(json, "maxBackupVersions", loaded.options.maxBackupVersions);

        loaded.options.filters.includePatterns = ExtractJsonStringArray(json, "includePatterns");
        auto exclude = ExtractJsonStringArray(json, "excludePatterns");
        if (!exclude.empty()) {
            loaded.options.filters.excludePatterns = std::move(exclude);
        } else if (loaded.options.filters.includePatterns.empty()) {
            loaded.options.filters = FilterOptions::Defaults();
        }

        profile = std::move(loaded);
        return true;
    }

} // namespace ChronoSync

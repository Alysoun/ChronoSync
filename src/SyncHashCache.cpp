#include "SyncHashCache.h"

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

    static bool ParseHexHash(const std::string& hex, std::array<uint8_t, 32>& outHash) {
        if (hex.size() != 64) {
            return false;
        }
        for (size_t i = 0; i < 32; ++i) {
            auto nibble = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = nibble(hex[i * 2]);
            int lo = nibble(hex[i * 2 + 1]);
            if (hi < 0 || lo < 0) {
                return false;
            }
            outHash[i] = static_cast<uint8_t>((hi << 4) | lo);
        }
        return true;
    }

    static std::string HashToHex(const std::array<uint8_t, 32>& hash) {
        static const char* kHex = "0123456789abcdef";
        std::string hex;
        hex.reserve(64);
        for (uint8_t byte : hash) {
            hex.push_back(kHex[byte >> 4]);
            hex.push_back(kHex[byte & 0x0F]);
        }
        return hex;
    }

    std::filesystem::path SyncHashCache::CachePath(const std::filesystem::path& destinationRoot) {
        return destinationRoot / L".chrono_history" / L"hash_cache.json";
    }

    bool SyncHashCache::EntryMatches(const CachedFileHash& entry, const SyncItem& item) {
        return entry.fileSize == item.fileSize &&
               entry.mtimeLow == item.lastWriteTime.dwLowDateTime &&
               entry.mtimeHigh == item.lastWriteTime.dwHighDateTime;
    }

    bool SyncHashCache::Load(const std::filesystem::path& destinationRoot, std::wstring& errorMessage) {
        entries_.clear();
        dirty_ = false;

        std::error_code ec;
        auto path = CachePath(destinationRoot);
        if (!std::filesystem::exists(path, ec)) {
            return true;
        }

        std::ifstream in(path, std::ios::binary);
        if (!in) {
            errorMessage = L"Failed to open hash cache.";
            return false;
        }

        std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

        size_t entriesPos = json.find("\"entries\"");
        if (entriesPos == std::string::npos) {
            return true;
        }
        size_t bracketStart = json.find('[', entriesPos);
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
            std::string obj = json.substr(objStart, objEnd - objStart + 1);

            CachedFileHash entry;
            std::wstring relPath;
            std::string hashHex;
            const auto extractString = [&](const char* key, std::wstring& out) {
                const std::string needle = "\"" + std::string(key) + "\"";
                size_t keyPos = obj.find(needle);
                if (keyPos == std::string::npos) {
                    return;
                }
                size_t colon = obj.find(':', keyPos + needle.size());
                size_t quoteStart = obj.find('"', colon + 1);
                if (quoteStart == std::string::npos) {
                    return;
                }
                size_t i = quoteStart + 1;
                std::string raw;
                while (i < obj.size()) {
                    if (obj[i] == '\\' && i + 1 < obj.size()) {
                        raw += obj[i];
                        raw += obj[i + 1];
                        i += 2;
                        continue;
                    }
                    if (obj[i] == '"') {
                        out = UnescapeJson(raw);
                        return;
                    }
                    raw += obj[i];
                    ++i;
                }
            };
            const auto extractAsciiString = [&](const char* key, std::string& out) {
                const std::string needle = "\"" + std::string(key) + "\"";
                size_t keyPos = obj.find(needle);
                if (keyPos == std::string::npos) {
                    return;
                }
                size_t colon = obj.find(':', keyPos + needle.size());
                size_t quoteStart = obj.find('"', colon + 1);
                if (quoteStart == std::string::npos) {
                    return;
                }
                size_t i = quoteStart + 1;
                while (i < obj.size()) {
                    if (obj[i] == '\\' && i + 1 < obj.size()) {
                        out += obj[i + 1];
                        i += 2;
                        continue;
                    }
                    if (obj[i] == '"') {
                        return;
                    }
                    out += obj[i];
                    ++i;
                }
            };
            const auto extractU64 = [&](const char* key, unsigned long long& out) {
                const std::string needle = "\"" + std::string(key) + "\"";
                size_t keyPos = obj.find(needle);
                if (keyPos == std::string::npos) {
                    return;
                }
                size_t colon = obj.find(':', keyPos + needle.size());
                if (colon == std::string::npos) {
                    return;
                }
                size_t start = colon + 1;
                while (start < obj.size() && (obj[start] == ' ' || obj[start] == '\t')) {
                    ++start;
                }
                out = std::stoull(obj.substr(start));
            };

            extractString("path", relPath);
            extractAsciiString("hash", hashHex);
            extractU64("size", entry.fileSize);
            unsigned long long low = 0;
            unsigned long long high = 0;
            extractU64("mtimeLow", low);
            extractU64("mtimeHigh", high);
            entry.mtimeLow = static_cast<unsigned long>(low);
            entry.mtimeHigh = static_cast<unsigned long>(high);

            if (!relPath.empty() && ParseHexHash(hashHex, entry.hash)) {
                entries_[relPath] = entry;
            }
            pos = objEnd + 1;
        }
        return true;
    }

    bool SyncHashCache::Save(const std::filesystem::path& destinationRoot, std::wstring& errorMessage) {
        if (!dirty_) {
            return true;
        }

        std::error_code ec;
        auto path = CachePath(destinationRoot);
        std::filesystem::create_directories(path.parent_path(), ec);

        std::ostringstream out;
        out << "{\n  \"version\": 1,\n  \"entries\": [\n";
        size_t i = 0;
        for (const auto& [relPath, entry] : entries_) {
            out << "    {\"path\": \"" << EscapeJson(relPath) << "\", ";
            out << "\"size\": " << entry.fileSize << ", ";
            out << "\"mtimeLow\": " << entry.mtimeLow << ", ";
            out << "\"mtimeHigh\": " << entry.mtimeHigh << ", ";
            out << "\"hash\": \"" << HashToHex(entry.hash) << "\"}";
            if (++i < entries_.size()) {
                out << ",";
            }
            out << "\n";
        }
        out << "  ]\n}\n";

        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file) {
            errorMessage = L"Failed to write hash cache.";
            return false;
        }
        const std::string content = out.str();
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        dirty_ = false;
        return true;
    }

    bool SyncHashCache::GetOrCompute(const std::wstring& relativePath,
                                     const SyncItem& item,
                                     const std::filesystem::path& fullPath,
                                     Sha256Session& session,
                                     std::array<uint8_t, 32>& outHash,
                                     const Sha256Progress* progress) {
        auto it = entries_.find(relativePath);
        if (it != entries_.end() && EntryMatches(it->second, item)) {
            outHash = it->second.hash;
            return true;
        }

        if (!session.HashFile(fullPath.wstring(), outHash, progress)) {
            return false;
        }
        Store(relativePath, item, outHash);
        return true;
    }

    void SyncHashCache::Store(const std::wstring& relativePath,
                              const SyncItem& item,
                              const std::array<uint8_t, 32>& hash) {
        CachedFileHash entry;
        entry.fileSize = item.fileSize;
        entry.mtimeLow = item.lastWriteTime.dwLowDateTime;
        entry.mtimeHigh = item.lastWriteTime.dwHighDateTime;
        entry.hash = hash;
        entries_[relativePath] = entry;
        dirty_ = true;
    }

} // namespace ChronoSync

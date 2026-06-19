#pragma once

#include <string>
#include <vector>

namespace PrevueSync {

    // Glob-style path filter rules applied during directory scans.
    struct FilterOptions {
        std::vector<std::wstring> includePatterns;
        std::vector<std::wstring> excludePatterns;

        static FilterOptions Defaults();
        static FilterOptions FromSemicolonList(const std::wstring& includeList, const std::wstring& excludeList);
        std::wstring IncludeToSemicolonList() const;
        std::wstring ExcludeToSemicolonList() const;
    };

    class PathFilter {
    public:
        static bool GlobMatch(const std::wstring& pattern, const std::wstring& text);
        static bool MatchesPattern(const std::wstring& pattern, const std::wstring& relativePath, const std::wstring& name, bool isDirectory);
        static bool IsExcluded(const FilterOptions& options, const std::wstring& relativePath, const std::wstring& name, bool isDirectory);
        static bool ShouldSkipDirectory(const FilterOptions& options, const std::wstring& dirName);
        static std::vector<std::wstring> ParseSemicolonList(const std::wstring& list);
    };

} // namespace PrevueSync

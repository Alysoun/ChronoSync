#pragma once

#include <filesystem>
#include <string>
#include <system_error>

namespace PrevueSync::WinPath {

    bool IsExtended(const std::wstring& path);

    std::wstring ToExtended(const std::wstring& path);

    std::filesystem::path NormalizeRoot(const std::wstring& root);

    std::filesystem::path Join(const std::filesystem::path& root, const std::wstring& relativePath);

    std::wstring JoinExtended(const std::filesystem::path& root, const std::wstring& relativePath);

    bool CreateDirectories(const std::filesystem::path& root,
                           const std::wstring& relativePath,
                           std::error_code& ec);

    bool CreateParentDirectories(const std::filesystem::path& root,
                                 const std::wstring& relativePath,
                                 std::error_code& ec);

} // namespace PrevueSync::WinPath

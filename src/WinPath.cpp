#include "WinPath.h"

#include <stdexcept>

namespace PrevueSync::WinPath {

    bool IsExtended(const std::wstring& path) {
        return path.size() >= 4 && path[0] == L'\\' && path[1] == L'\\' && path[2] == L'?' && path[3] == L'\\';
    }

    std::wstring ToExtended(const std::wstring& path) {
        if (path.empty() || IsExtended(path)) {
            return path;
        }

        if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\') {
            if (path.size() >= 4 && path[2] == L'?' && path[3] == L'\\') {
                return path;
            }
            return L"\\\\?\\UNC\\" + path.substr(2);
        }

        if (path.size() >= 2 && path[1] == L':') {
            return L"\\\\?\\" + path;
        }

        return path;
    }

    std::filesystem::path NormalizeRoot(const std::wstring& root) {
        std::error_code ec;
        std::filesystem::path absolute = std::filesystem::absolute(root, ec);
        if (ec) {
            absolute = std::filesystem::path(root);
        }
        return std::filesystem::path(ToExtended(absolute.wstring()));
    }

    std::wstring JoinExtended(const std::filesystem::path& root, const std::wstring& relativePath) {
        std::filesystem::path rootPath = root;
        if (!IsExtended(rootPath.wstring())) {
            rootPath = NormalizeRoot(rootPath.wstring());
        }
        return (rootPath / relativePath).wstring();
    }

    std::filesystem::path Join(const std::filesystem::path& root, const std::wstring& relativePath) {
        return std::filesystem::path(JoinExtended(root, relativePath));
    }

    bool CreateDirectories(const std::filesystem::path& root,
                           const std::wstring& relativePath,
                           std::error_code& ec) {
        ec.clear();
        try {
            std::filesystem::path destPath = Join(root, relativePath);
            std::filesystem::create_directories(destPath, ec);
            return !ec;
        } catch (const std::exception&) {
            ec.assign(static_cast<int>(std::errc::filename_too_long), std::system_category());
            return false;
        }
    }

    bool CreateParentDirectories(const std::filesystem::path& root,
                                 const std::wstring& relativePath,
                                 std::error_code& ec) {
        ec.clear();
        try {
            std::filesystem::path destPath = Join(root, relativePath);
            std::filesystem::create_directories(destPath.parent_path(), ec);
            return !ec;
        } catch (const std::exception&) {
            ec.assign(static_cast<int>(std::errc::filename_too_long), std::system_category());
            return false;
        }
    }

} // namespace PrevueSync::WinPath

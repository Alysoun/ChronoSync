#include <windows.h>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <cassert>
#include "SyncEngine.h"
#include "PathFilter.h"
#include "SyncOptions.h"

namespace fs = std::filesystem;

// Minimal callback structures for silent test validation
ChronoSync::SyncCallbacks GetTestCallbacks() {
    ChronoSync::SyncCallbacks cb;
    cb.onLog = [](const std::wstring& msg, bool isError) {
        if (isError) {
            std::wcerr << L"[TEST ERROR] " << msg << std::endl;
        }
    };
    return cb;
}

ChronoSync::SyncOptions MakeTestOptions(bool prune, const ChronoSync::FilterOptions& filters = {}) {
    ChronoSync::SyncOptions opts;
    opts.prune = prune;
    opts.filters = filters;
    opts.versionedBackups = false;
    return opts;
}

void WriteTestFile(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    assert(out && "Failed to create test file");
    out.write(content.data(), content.size());
    out.close();
}

bool CreateTestJunction(const fs::path& linkPath, const fs::path& targetPath) {
    std::wstring cmdArgs = L"cmd.exe /c mklink /j \"" + linkPath.wstring() + L"\" \"" + targetPath.wstring() + L"\"";
    std::vector<wchar_t> cmdBuffer(cmdArgs.begin(), cmdArgs.end());
    cmdBuffer.push_back(L'\0');

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = { 0 };

    BOOL procSuccess = CreateProcessW(
        NULL,
        cmdBuffer.data(),
        NULL,
        NULL,
        FALSE,
        CREATE_NO_WINDOW,
        NULL,
        NULL,
        &si,
        &pi
    );

    if (procSuccess) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return exitCode == 0;
    }
    return false;
}

int main() {
    std::wcout << L"==============================================" << std::endl;
    std::wcout << L"ChronoSync Automated Verification Test Suite" << std::endl;
    std::wcout << L"==============================================" << std::endl;

    ChronoSync::SyncOptions noFilters = MakeTestOptions(false);

    fs::path sandbox = fs::current_path() / L"test_sandbox";
    fs::path srcDir = sandbox / L"source";
    fs::path destDir = sandbox / L"destination";

    std::error_code ec;
    // Clean sandbox from any previous failed runs
    fs::remove_all(sandbox, ec);
    fs::create_directories(srcDir, ec);
    fs::create_directories(destDir, ec);

    std::wcout << L"[1/6] Creating mock directory tree..." << std::endl;
    
    // Create structure:
    // source/file1.txt
    // source/file2.txt
    // source/file3.txt
    // source/folder1/file4.txt
    // source/folder2/nested/file5.txt
    // source/folder2/nested/file6.txt
    WriteTestFile(srcDir / L"file1.txt", "Initial file1 contents");
    WriteTestFile(srcDir / L"file2.txt", "Initial file2 contents");
    WriteTestFile(srcDir / L"file3.txt", "Initial file3 contents");
    WriteTestFile(srcDir / L"folder1/file4.txt", "Initial file4 contents inside folder1");
    WriteTestFile(srcDir / L"folder2/nested/file5.txt", "Initial file5 contents inside nested folder");
    WriteTestFile(srcDir / L"folder2/nested/file6.txt", "Initial file6 contents inside nested folder");

    std::wcout << L"[2/6] Executing initial full synchronization..." << std::endl;
    auto callbacks = GetTestCallbacks();
    ChronoSync::SyncStats initialStats = ChronoSync::SyncEngine::Sync(srcDir.wstring(), destDir.wstring(), noFilters, callbacks);

    // Assert initial sync copied everything
    std::wcout << L"      Initial sync transferred " << initialStats.filesCopied << L" files." << std::endl;
    assert(initialStats.filesCopied == 6 && "Initial sync should copy all 6 files.");
    assert(fs::exists(destDir / L"file1.txt"));
    assert(fs::exists(destDir / L"folder2/nested/file5.txt"));

    std::wcout << L"[3/6] Altering exactly 5 files/structure items in source..." << std::endl;
    
    // We need some delay to ensure last write times change noticeably if not set manually.
    // However, let's make explicit modifications.
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // Alteration 1: Modify contents of file1.txt (changes size)
    WriteTestFile(srcDir / L"file1.txt", "Modified file1 contents with new size!");

    // Alteration 2: Modify contents of file2.txt (keeps identical size, but changes content and last write time)
    // Size is 22 bytes. Let's write another 22-byte string.
    WriteTestFile(srcDir / L"file2.txt", "Initial file2 contenXX");

    // Alteration 3: Modify timestamp of file3.txt without changing content size or text.
    // We update last_write_time to now (which is newer than destDir/file3.txt).
    auto now = fs::last_write_time(srcDir / L"file3.txt") + std::chrono::hours(1);
    fs::last_write_time(srcDir / L"file3.txt", now, ec);

    // Alteration 4: Add a brand new file (new_file.txt)
    WriteTestFile(srcDir / L"new_file.txt", "Brand new file content");

    // Alteration 5: Modify contents of a nested file (folder2/nested/file5.txt)
    WriteTestFile(srcDir / L"folder2/nested/file5.txt", "Modified file5 nested contents!");

    // Also, introduce an abandoned file in destination to test pruning
    WriteTestFile(destDir / L"abandoned_file.txt", "Should be pruned");
    WriteTestFile(destDir / L"folder1/abandoned_nested.txt", "Should be pruned nested");

    std::wcout << L"[4/6] Executing differential synchronization with pruning..." << std::endl;
    ChronoSync::SyncStats diffStats = ChronoSync::SyncEngine::Sync(srcDir.wstring(), destDir.wstring(), MakeTestOptions(true), callbacks);

    std::wcout << L"      Differential files copied: " << diffStats.filesCopied << std::endl;
    std::wcout << L"      Differential files skipped: " << diffStats.filesSkipped << std::endl;
    std::wcout << L"      Differential items pruned: " << diffStats.itemsDeleted << std::endl;

    // Verify assertions
    // Exactly 5 files should be transferred:
    // 1. file1.txt (size change)
    // 2. file2.txt (time change, same size)
    // 3. file3.txt (timestamp update only)
    // 4. new_file.txt (new file)
    // 5. folder2/nested/file5.txt (nested change)
    assert(diffStats.filesCopied == 5 && "Differential sync should have copied exactly 5 files.");
    
    // Skipping check:
    // folder2/nested/file6.txt and folder1/file4.txt did not change.
    // Skipped count should be 2.
    assert(diffStats.filesSkipped == 2 && "Differential sync should have skipped exactly 2 files.");

    // Deletion check:
    // abandoned_file.txt and folder1/abandoned_nested.txt should have been deleted.
    assert(diffStats.itemsDeleted == 2 && "Pruning should have deleted exactly 2 items.");
    assert(!fs::exists(destDir / L"abandoned_file.txt"));
    assert(!fs::exists(destDir / L"folder1/abandoned_nested.txt"));

    // Verify dest file contents match source
    assert(fs::file_size(destDir / L"file1.txt") == fs::file_size(srcDir / L"file1.txt"));
    assert(fs::file_size(destDir / L"folder2/nested/file5.txt") == fs::file_size(srcDir / L"folder2/nested/file5.txt"));

    std::wcout << L"[5/8] Verifying folder timestamps sync..." << std::endl;
    // Check if the modified timestamp of the destination files match the source files
    auto srcTime1 = fs::last_write_time(srcDir / L"file1.txt");
    auto destTime1 = fs::last_write_time(destDir / L"file1.txt");
    assert(srcTime1 == destTime1 && "Timestamps must match after synchronization.");

    std::wcout << L"[6/8] Verifying junction/reparse point preservation..." << std::endl;
    // Create a junction in source pointing to folder1: source/folder1_link -> source/folder1
    fs::path srcJunction = srcDir / L"folder1_link";
    fs::path destJunction = destDir / L"folder1_link";
    bool junctionCreated = CreateTestJunction(srcJunction, srcDir / L"folder1");
    assert(junctionCreated && "Failed to create source junction for test");

    // Run sync again to sync the junction
    ChronoSync::SyncStats junctionStats = ChronoSync::SyncEngine::Sync(srcDir.wstring(), destDir.wstring(), MakeTestOptions(true), callbacks);
    std::wcout << L"      Junction sync completed. Created: " << junctionStats.dirsCreated << L" dirs/junctions, " << junctionStats.filesCopied << L" files." << std::endl;

    // Verify destination junction exists, is a reparse point, and points to the correct target
    DWORD destAttrs = GetFileAttributesW(destJunction.c_str());
    assert(destAttrs != INVALID_FILE_ATTRIBUTES && "Destination junction should exist");
    assert((destAttrs & FILE_ATTRIBUTE_REPARSE_POINT) && "Destination junction should be a reparse point");
    assert((destAttrs & FILE_ATTRIBUTE_DIRECTORY) && "Destination junction should be a directory");

    std::error_code readEc;
    auto readTarget = fs::read_symlink(destJunction, readEc);
    assert(!readEc && "Should be able to read destination junction target");
    
    std::wstring normalizedTarget = readTarget.wstring();
    if (normalizedTarget.size() >= 4 && 
        ((normalizedTarget[0] == L'\\' && normalizedTarget[1] == L'?' && normalizedTarget[2] == L'?' && normalizedTarget[3] == L'\\') ||
         (normalizedTarget[0] == L'\\' && normalizedTarget[1] == L'\\' && normalizedTarget[2] == L'?' && normalizedTarget[3] == L'\\'))) {
        normalizedTarget = normalizedTarget.substr(4);
    }
    std::wstring expectedTarget = (srcDir / L"folder1").wstring();
    assert(normalizedTarget == expectedTarget && "Destination junction target must match the source target");

    // Clean up junction in source and verify pruning deletes it from destination
    // We must use RemoveDirectoryW to delete the junction safely in our test cleanup!
    BOOL removeSrcOk = RemoveDirectoryW(srcJunction.c_str());
    assert(removeSrcOk && "Failed to remove source junction for pruning test");

    ChronoSync::SyncStats pruneJunctionStats = ChronoSync::SyncEngine::Sync(srcDir.wstring(), destDir.wstring(), MakeTestOptions(true), callbacks);
    assert(pruneJunctionStats.itemsDeleted == 1 && "Pruning should delete the junction at destination");
    assert(!fs::exists(destJunction) && "Destination junction should be deleted");

    std::wcout << L"[7/9] Verifying exclusion filters (.chrono_trash, .chrono_tmp)..." << std::endl;
    // Create excluded items at various depths in source
    fs::path excludedTrashDir = srcDir / L"folder2/nested/.chrono_trash";
    fs::path excludedTmpDir = srcDir / L"folder1/.chrono_tmp";
    fs::path excludedTmpFile = srcDir / L"folder1/nested/file.chrono_tmp";
    fs::path excludedTmpFile2 = srcDir / L"folder2/nested/.chrono_tmp";

    fs::create_directories(excludedTrashDir);
    fs::create_directories(excludedTmpDir);
    WriteTestFile(excludedTmpFile, "Temporary content");
    WriteTestFile(excludedTmpFile2, "Temporary folder-like file content");
    WriteTestFile(excludedTrashDir / L"trash_file.txt", "Trash file content");

    // Run sync again
    ChronoSync::SyncEngine::Sync(srcDir.wstring(), destDir.wstring(), MakeTestOptions(true), callbacks);

    // Verify none of the excluded paths exist in destination
    assert(!fs::exists(destDir / L"folder2/nested/.chrono_trash") && ".chrono_trash directory at depth should be excluded");
    assert(!fs::exists(destDir / L"folder1/.chrono_tmp") && ".chrono_tmp directory at depth should be excluded");
    assert(!fs::exists(destDir / L"folder1/nested/file.chrono_tmp") && "*.chrono_tmp file should be excluded");
    assert(!fs::exists(destDir / L"folder2/nested/.chrono_tmp") && "*.chrono_tmp file/folder should be excluded");

    std::wcout << L"[8/9] Verifying user exclude filters (*.pkl, node_modules, *.zip)..." << std::endl;
    WriteTestFile(srcDir / L"cache/data.pkl", "pickle payload");
    WriteTestFile(srcDir / L"archive.zip", "zip payload");
    WriteTestFile(srcDir / L"node_modules/pkg/index.js", "module payload");
    WriteTestFile(srcDir / L"allowed.txt", "allowed payload");

    ChronoSync::SyncOptions defaultFilters = MakeTestOptions(false, ChronoSync::FilterOptions::Defaults());
    ChronoSync::SyncEngine::Sync(srcDir.wstring(), destDir.wstring(), defaultFilters, callbacks);

    assert(!fs::exists(destDir / L"cache/data.pkl") && "*.pkl files should be excluded");
    assert(!fs::exists(destDir / L"archive.zip") && "*.zip files should be excluded");
    assert(!fs::exists(destDir / L"node_modules/pkg/index.js") && "node_modules trees should be excluded");
    assert(fs::exists(destDir / L"allowed.txt") && "non-matching files should still sync");

    std::wcout << L"[9/11] Verifying forward-slash path patterns (build/obj)..." << std::endl;
    WriteTestFile(srcDir / L"build/obj/artifact.bin", "build artifact");
    WriteTestFile(srcDir / L"build/allowed.bin", "allowed in build");

    ChronoSync::SyncOptions slashFilters = MakeTestOptions(false, ChronoSync::FilterOptions::FromSemicolonList(L"", L"build/obj"));
    ChronoSync::SyncEngine::Sync(srcDir.wstring(), destDir.wstring(), slashFilters, callbacks);
    assert(!fs::exists(destDir / L"build/obj/artifact.bin") && "build/obj with forward slashes should be excluded");
    assert(fs::exists(destDir / L"build/allowed.bin") && "sibling paths under build/ should still sync");

    std::wcout << L"[10/11] Verifying trailing semicolon and empty-pattern guards..." << std::endl;
    WriteTestFile(srcDir / L"trailing_guard.txt", "should still sync");

    ChronoSync::SyncOptions trailingFilters = MakeTestOptions(false, ChronoSync::FilterOptions::FromSemicolonList(L"", L"*.pkl;node_modules;"));
    assert(trailingFilters.filters.excludePatterns.size() == 2 && "trailing semicolon must not create an empty pattern");
    ChronoSync::SyncEngine::Sync(srcDir.wstring(), destDir.wstring(), trailingFilters, callbacks);
    assert(fs::exists(destDir / L"trailing_guard.txt") && "trailing semicolon must not exclude everything");
    assert(!ChronoSync::PathFilter::GlobMatch(L"", L"anything") && "empty glob pattern must not match");
    assert(!ChronoSync::PathFilter::MatchesPattern(L"", L"path", L"name", false) && "empty match pattern must not match");

    std::wcout << L"[11/13] Verifying SHA256 compare mode..." << std::endl;
    WriteTestFile(srcDir / L"sha_test/equal_time.txt", "aaaaaaaaaaaaaaaaaaaa");
    WriteTestFile(destDir / L"sha_test/equal_time.txt", "bbbbbbbbbbbbbbbbbbbb");
    auto shaSrcTime = fs::last_write_time(srcDir / L"sha_test/equal_time.txt");
    fs::last_write_time(destDir / L"sha_test/equal_time.txt", shaSrcTime, ec);

    ChronoSync::SyncOptions timestampOpts = MakeTestOptions(false);
    ChronoSync::SyncStats tsStats = ChronoSync::SyncEngine::Sync(srcDir.wstring(), destDir.wstring(), timestampOpts, callbacks);
    assert(tsStats.filesCopied == 0 && "timestamp mode should not copy equal-time same-size files");
    assert(fs::file_size(destDir / L"sha_test/equal_time.txt") == 20 && "timestamp mode should leave destination bytes unchanged");

    ChronoSync::SyncOptions shaOpts = MakeTestOptions(false);
    shaOpts.compareMode = ChronoSync::CompareMode::Sha256;
    ChronoSync::SyncStats shaStats = ChronoSync::SyncEngine::Sync(srcDir.wstring(), destDir.wstring(), shaOpts, callbacks);
    assert(shaStats.filesCopied == 1 && "SHA256 mode should copy when content differs");

    std::wcout << L"[12/13] Verifying versioned backup folders..." << std::endl;
    WriteTestFile(destDir / L"versioned_prune_me.txt", "old version");
    ChronoSync::SyncOptions versionedPrune = MakeTestOptions(true);
    versionedPrune.versionedBackups = true;
    versionedPrune.maxBackupVersions = 3;
    ChronoSync::SyncEngine::Sync(srcDir.wstring(), destDir.wstring(), versionedPrune, callbacks);
    assert(!fs::exists(destDir / L"versioned_prune_me.txt") && "pruned file should be removed from destination");
    assert(fs::exists(destDir / L".chrono_backups") && "versioned backups root should exist");

    std::wcout << L"[13/13] Cleaning up test sandbox..." << std::endl;
    fs::remove_all(sandbox, ec);

    std::wcout << L"\n==============================================" << std::endl;
    std::wcout << L"   SUCCESS: All ChronoSync Tests Passed!      " << std::endl;
    std::wcout << L"==============================================" << std::endl;
    return 0;
}

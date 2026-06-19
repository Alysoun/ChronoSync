#include <windows.h>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <cassert>
#include <vector>
#include <algorithm>
#include "SyncEngine.h"
#include "PathFilter.h"
#include "SyncOptions.h"
#include "NetworkShare.h"
#include "DeltaCopy.h"
#include "SyncPlanAnalysis.h"
#include "SyncHistory.h"
#include "WinPath.h"

namespace fs = std::filesystem;

// Minimal callback structures for silent test validation
PrevueSync::SyncCallbacks GetTestCallbacks() {
    PrevueSync::SyncCallbacks cb;
    cb.onLog = [](const std::wstring& msg, bool isError) {
        if (isError) {
            std::wcerr << L"[TEST ERROR] " << msg << std::endl;
        }
    };
    return cb;
}

PrevueSync::SyncOptions MakeTestOptions(bool prune, const PrevueSync::FilterOptions& filters = {}) {
    PrevueSync::SyncOptions opts;
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

static size_t CountRegularFiles(const fs::path& root) {
    std::error_code ec;
    if (!fs::exists(root, ec)) {
        return 0;
    }

    size_t count = 0;
    fs::path scanRoot = PrevueSync::WinPath::NormalizeRoot(root.wstring());
    fs::recursive_directory_iterator it(scanRoot, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (it->is_regular_file(ec) && !ec) {
            ++count;
        }
        ec.clear();
    }
    return count;
}

static void RemoveTestSandbox(const fs::path& sandbox) {
    std::error_code ec;
    if (!fs::exists(sandbox, ec)) {
        return;
    }

    fs::path scanRoot = PrevueSync::WinPath::NormalizeRoot(sandbox.wstring());
    std::vector<fs::path> reparsePoints;
    {
        fs::recursive_directory_iterator it(scanRoot, fs::directory_options::skip_permission_denied, ec);
        const fs::recursive_directory_iterator end;
        for (; it != end; it.increment(ec)) {
            if (ec) {
                ec.clear();
                continue;
            }
            DWORD attrs = GetFileAttributesW(it->path().c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
                reparsePoints.push_back(it->path());
            }
        }
    }

    std::sort(reparsePoints.begin(), reparsePoints.end(),
              [](const fs::path& a, const fs::path& b) {
                  return a.wstring().size() > b.wstring().size();
              });
    for (const fs::path& linkPath : reparsePoints) {
        RemoveDirectoryW(linkPath.c_str());
        fs::remove(linkPath, ec);
        ec.clear();
    }

    fs::remove_all(scanRoot, ec);
    ec.clear();
    fs::remove_all(sandbox, ec);
}

int main() {
    std::wcout << L"==============================================" << std::endl;
    std::wcout << L"PrevueSync Automated Verification Test Suite" << std::endl;
    std::wcout << L"==============================================" << std::endl;

    PrevueSync::SyncOptions noFilters = MakeTestOptions(false);

    // Use a fresh temp sandbox; legacy ./test_sandbox may survive failed cleanups.
    RemoveTestSandbox(fs::current_path() / L"test_sandbox");
    fs::path sandbox = fs::temp_directory_path()
        / (L"PrevueSyncTests_" + std::to_wstring(GetCurrentProcessId()) + L"_"
           + std::to_wstring(GetTickCount64()));
    fs::path srcDir = sandbox / L"source";
    fs::path destDir = sandbox / L"destination";

    std::error_code ec;
    RemoveTestSandbox(sandbox);
    fs::create_directories(srcDir, ec);
    fs::create_directories(destDir, ec);
    assert(!ec && "Failed to create test sandbox directories.");

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
    assert(CountRegularFiles(srcDir) == 6 && "Test setup should create exactly 6 source files.");

    std::wcout << L"[2/6] Executing initial full synchronization..." << std::endl;
    auto callbacks = GetTestCallbacks();
    PrevueSync::SyncStats initialStats = PrevueSync::SyncEngine::Sync(srcDir.wstring(), destDir.wstring(), noFilters, callbacks);

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
    PrevueSync::SyncStats diffStats = PrevueSync::SyncEngine::Sync(srcDir.wstring(), destDir.wstring(), MakeTestOptions(true), callbacks);

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
    PrevueSync::SyncStats junctionStats = PrevueSync::SyncEngine::Sync(srcDir.wstring(), destDir.wstring(), MakeTestOptions(true), callbacks);
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
        if (normalizedTarget.rfind(L"\\\\?\\UNC\\", 0) == 0) {
            normalizedTarget = L"\\\\" + normalizedTarget.substr(8);
        } else {
            normalizedTarget = normalizedTarget.substr(4);
        }
    }
    std::wstring expectedTarget = (destDir / L"folder1").wstring();
    assert(_wcsicmp(normalizedTarget.c_str(), expectedTarget.c_str()) == 0 &&
           "Destination junction target must point to the mirrored folder on the destination");

    // Clean up junction in source and verify pruning deletes it from destination
    // We must use RemoveDirectoryW to delete the junction safely in our test cleanup!
    BOOL removeSrcOk = RemoveDirectoryW(srcJunction.c_str());
    assert(removeSrcOk && "Failed to remove source junction for pruning test");

    PrevueSync::SyncStats pruneJunctionStats = PrevueSync::SyncEngine::Sync(srcDir.wstring(), destDir.wstring(), MakeTestOptions(true), callbacks);
    assert(pruneJunctionStats.itemsDeleted == 1 && "Pruning should delete the junction at destination");
    assert(!fs::exists(destJunction) && "Destination junction should be deleted");

    std::wcout << L"[7/9] Verifying exclusion filters (.prevue_trash, .prevue_tmp)..." << std::endl;
    // Create excluded items at various depths in source
    fs::path excludedTrashDir = srcDir / L"folder2/nested/.prevue_trash";
    fs::path excludedTmpDir = srcDir / L"folder1/.prevue_tmp";
    fs::path excludedTmpFile = srcDir / L"folder1/nested/file.prevue_tmp";
    fs::path excludedTmpFile2 = srcDir / L"folder2/nested/.prevue_tmp";

    fs::create_directories(excludedTrashDir);
    fs::create_directories(excludedTmpDir);
    WriteTestFile(excludedTmpFile, "Temporary content");
    WriteTestFile(excludedTmpFile2, "Temporary folder-like file content");
    WriteTestFile(excludedTrashDir / L"trash_file.txt", "Trash file content");

    // Run sync again
    PrevueSync::SyncEngine::Sync(srcDir.wstring(), destDir.wstring(), MakeTestOptions(true), callbacks);

    // Verify none of the excluded paths exist in destination
    assert(!fs::exists(destDir / L"folder2/nested/.prevue_trash") && ".prevue_trash directory at depth should be excluded");
    assert(!fs::exists(destDir / L"folder1/.prevue_tmp") && ".prevue_tmp directory at depth should be excluded");
    assert(!fs::exists(destDir / L"folder1/nested/file.prevue_tmp") && "*.prevue_tmp file should be excluded");
    assert(!fs::exists(destDir / L"folder2/nested/.prevue_tmp") && "*.prevue_tmp file/folder should be excluded");

    std::wcout << L"[8/9] Verifying user exclude filters (*.pkl, node_modules, *.zip)..." << std::endl;
    WriteTestFile(srcDir / L"cache/data.pkl", "pickle payload");
    WriteTestFile(srcDir / L"archive.zip", "zip payload");
    WriteTestFile(srcDir / L"node_modules/pkg/index.js", "module payload");
    WriteTestFile(srcDir / L"allowed.txt", "allowed payload");

    PrevueSync::SyncOptions defaultFilters = MakeTestOptions(false, PrevueSync::FilterOptions::Defaults());
    PrevueSync::SyncEngine::Sync(srcDir.wstring(), destDir.wstring(), defaultFilters, callbacks);

    assert(!fs::exists(destDir / L"cache/data.pkl") && "*.pkl files should be excluded");
    assert(!fs::exists(destDir / L"archive.zip") && "*.zip files should be excluded");
    assert(!fs::exists(destDir / L"node_modules/pkg/index.js") && "node_modules trees should be excluded");
    assert(fs::exists(destDir / L"allowed.txt") && "non-matching files should still sync");

    std::wcout << L"[9/11] Verifying forward-slash path patterns (build/obj)..." << std::endl;
    WriteTestFile(srcDir / L"build/obj/artifact.bin", "build artifact");
    WriteTestFile(srcDir / L"build/allowed.bin", "allowed in build");

    PrevueSync::SyncOptions slashFilters = MakeTestOptions(false, PrevueSync::FilterOptions::FromSemicolonList(L"", L"build/obj"));
    PrevueSync::SyncEngine::Sync(srcDir.wstring(), destDir.wstring(), slashFilters, callbacks);
    assert(!fs::exists(destDir / L"build/obj/artifact.bin") && "build/obj with forward slashes should be excluded");
    assert(fs::exists(destDir / L"build/allowed.bin") && "sibling paths under build/ should still sync");

    std::wcout << L"[10/11] Verifying trailing semicolon and empty-pattern guards..." << std::endl;
    WriteTestFile(srcDir / L"trailing_guard.txt", "should still sync");

    PrevueSync::SyncOptions trailingFilters = MakeTestOptions(false, PrevueSync::FilterOptions::FromSemicolonList(L"", L"*.pkl;node_modules;"));
    assert(trailingFilters.filters.excludePatterns.size() == 2 && "trailing semicolon must not create an empty pattern");
    PrevueSync::SyncEngine::Sync(srcDir.wstring(), destDir.wstring(), trailingFilters, callbacks);
    assert(fs::exists(destDir / L"trailing_guard.txt") && "trailing semicolon must not exclude everything");
    assert(!PrevueSync::PathFilter::GlobMatch(L"", L"anything") && "empty glob pattern must not match");
    assert(!PrevueSync::PathFilter::MatchesPattern(L"", L"path", L"name", false) && "empty match pattern must not match");

    std::wcout << L"[11/13] Verifying SHA256 compare mode..." << std::endl;
    WriteTestFile(srcDir / L"sha_test/equal_time.txt", "aaaaaaaaaaaaaaaaaaaa");
    WriteTestFile(destDir / L"sha_test/equal_time.txt", "bbbbbbbbbbbbbbbbbbbb");
    auto shaSrcTime = fs::last_write_time(srcDir / L"sha_test/equal_time.txt");
    fs::last_write_time(destDir / L"sha_test/equal_time.txt", shaSrcTime, ec);

    PrevueSync::SyncOptions timestampOpts = MakeTestOptions(false);
    PrevueSync::SyncStats tsStats = PrevueSync::SyncEngine::Sync(srcDir.wstring(), destDir.wstring(), timestampOpts, callbacks);
    assert(tsStats.filesCopied == 0 && "timestamp mode should not copy equal-time same-size files");
    assert(fs::file_size(destDir / L"sha_test/equal_time.txt") == 20 && "timestamp mode should leave destination bytes unchanged");

    PrevueSync::SyncOptions shaOpts = MakeTestOptions(false);
    shaOpts.compareMode = PrevueSync::CompareMode::Sha256;
    PrevueSync::SyncStats shaStats = PrevueSync::SyncEngine::Sync(srcDir.wstring(), destDir.wstring(), shaOpts, callbacks);
    assert(shaStats.filesCopied == 1 && "SHA256 mode should copy when content differs");

    std::wcout << L"[12/13] Verifying versioned backup folders..." << std::endl;
    WriteTestFile(destDir / L"versioned_prune_me.txt", "old version");
    PrevueSync::SyncOptions versionedPrune = MakeTestOptions(true);
    versionedPrune.versionedBackups = true;
    versionedPrune.maxBackupVersions = 3;
    PrevueSync::SyncEngine::Sync(srcDir.wstring(), destDir.wstring(), versionedPrune, callbacks);
    assert(!fs::exists(destDir / L"versioned_prune_me.txt") && "pruned file should be removed from destination");
    assert(fs::exists(destDir / L".prevue_backups") && "versioned backups root should exist");

    std::wcout << L"[13/15] Verifying UNC path helpers..." << std::endl;
    assert(PrevueSync::NetworkShare::IsUncPath(L"\\\\server\\share\\folder\\file.txt"));
    assert(!PrevueSync::NetworkShare::IsUncPath(L"C:\\local\\path"));
    assert(PrevueSync::NetworkShare::GetUncRoot(L"\\\\server\\share\\sub\\file.txt") == L"\\\\server\\share");

    std::wcout << L"[14/15] Verifying atomic block-compare copy..." << std::endl;
    fs::path deltaSrcDir = sandbox / L"delta_src";
    fs::path deltaDestDir = sandbox / L"delta_dest";
    fs::create_directories(deltaSrcDir);
    fs::create_directories(deltaDestDir);

    const size_t blockSize = 4 * 1024 * 1024;
    std::string blockA(blockSize, 'A');
    std::string blockB(blockSize, 'B');
    std::string deltaContent = blockA + blockB + blockA;
    WriteTestFile(deltaSrcDir / L"large.bin", deltaContent);
    WriteTestFile(deltaDestDir / L"large.bin", deltaContent);

    std::string modified = blockA + std::string(blockSize, 'C') + blockA;
    WriteTestFile(deltaSrcDir / L"large.bin", modified);

    PrevueSync::SyncOptions deltaOpts = MakeTestOptions(false);
    deltaOpts.deltaBlockCopy = true;
    PrevueSync::SyncStats deltaStats = PrevueSync::SyncEngine::Sync(
        deltaSrcDir.wstring(), deltaDestDir.wstring(), deltaOpts, callbacks);
    assert(deltaStats.filesCopied == 1 && "block-compare sync should copy one changed file");
    assert(deltaStats.deltaBytesWritten > 0 && deltaStats.deltaBytesWritten < deltaContent.size() &&
           "deltaBytesWritten should count only changed blocks, not full file size");

    std::ifstream verify(deltaDestDir / L"large.bin", std::ios::binary);
    std::string destBytes((std::istreambuf_iterator<char>(verify)), std::istreambuf_iterator<char>());
    assert(destBytes == modified && "destination should match modified source after block-compare copy");

    std::wcout << L"[15/16] Verifying preview replace deletes when prune is off..." << std::endl;
    fs::path replaceSrc = sandbox / L"replace_src";
    fs::path replaceDest = sandbox / L"replace_dest";
    fs::create_directories(replaceSrc);
    fs::create_directories(replaceDest);
    fs::path junctionTarget = replaceSrc / L"junction_target";
    fs::create_directories(junctionTarget);
    fs::path previewDestJunction = replaceDest / L"collision_path";
    assert(CreateTestJunction(previewDestJunction, junctionTarget) && "Failed to create destination junction for preview test");
    WriteTestFile(replaceSrc / L"collision_path", "source file replaces junction");

    PrevueSync::SyncOptions noPrune = MakeTestOptions(false);
    std::vector<PrevueSync::PreviewItem> previewItems =
        PrevueSync::SyncEngine::Preview(replaceSrc.wstring(), replaceDest.wstring(), noPrune, callbacks);

    bool foundReplace = false;
    bool foundPrune = false;
    for (const auto& pi : previewItems) {
        if (pi.action == L"Remove (Replace)" && pi.relativePath == L"collision_path") {
            foundReplace = true;
        }
        if (pi.action == L"Delete (Prune)") {
            foundPrune = true;
        }
    }
    assert(foundReplace && "preview must show Remove (Replace) for type collisions when prune is off");
    assert(!foundPrune && "preview must not show prune deletes when prune is disabled");

    std::wcout << L"[16/19] Verifying plan analysis and risk scoring..." << std::endl;
    auto analysisReport = PrevueSync::BuildSyncPlanReport(
        replaceSrc.wstring(), replaceDest.wstring(), noPrune, callbacks);
    assert(analysisReport.analysis.deletesReplace >= 1 && "analysis should count replacement deletions");
    assert(analysisReport.analysis.filesToCopyUpdate + analysisReport.analysis.filesToCopyNew >= 1);
    assert(analysisReport.analysis.risk == PrevueSync::RiskLevel::Medium ||
           analysisReport.analysis.risk == PrevueSync::RiskLevel::High);
    std::wstring reportText = PrevueSync::FormatSyncPlanReport(analysisReport.analysis);
    assert(reportText.find(L"Risk:") != std::wstring::npos);
    assert(reportText.find(L"collision_path") != std::wstring::npos &&
           "analysis report should name replacement paths");
    assert(reportText.find(L"Largest files:") != std::wstring::npos || analysisReport.analysis.largestFiles.empty());

    assert(reportText.find(L"Top extensions") != std::wstring::npos ||
           analysisReport.analysis.extensionBreakdown.empty());

    std::wcout << L"[17/19] Verifying sync history and snapshots..." << std::endl;
    PrevueSync::SyncStats historyStats = {};
    historyStats.filesCopied = 3;
    historyStats.totalBytesCopied = 4096;
    std::wstring historyError;
    assert(PrevueSync::SyncHistoryIO::RecordRun(
        srcDir.wstring(), destDir.wstring(), noFilters, historyStats, historyError));
    historyStats.filesCopied = 1;
    historyStats.itemsDeleted = 1;
    assert(PrevueSync::SyncHistoryIO::RecordRun(
        srcDir.wstring(), destDir.wstring(), noFilters, historyStats, historyError));

    std::vector<PrevueSync::SyncHistoryEntry> historyEntries;
    assert(PrevueSync::SyncHistoryIO::LoadEntries(destDir.wstring(), historyEntries, historyError));
    assert(historyEntries.size() >= 2);

    PrevueSync::DestinationSnapshot snapA;
    PrevueSync::DestinationSnapshot snapB;
    assert(PrevueSync::SyncHistoryIO::LoadSnapshot(
        destDir.wstring(), historyEntries[0].snapshotId, snapA, historyError));
    assert(PrevueSync::SyncHistoryIO::LoadSnapshot(
        destDir.wstring(), historyEntries[1].snapshotId, snapB, historyError));
    auto diff = PrevueSync::SyncHistoryIO::DiffSnapshots(snapA, snapB);
    std::wstring diffReport = PrevueSync::SyncHistoryIO::FormatSnapshotDiffReport(
        diff, historyEntries[0].timestampUtc, historyEntries[1].timestampUtc);
    assert(diffReport.find(L"Added:") != std::wstring::npos);

    std::wcout << L"[18/19] Verifying history query window..." << std::endl;
    auto recent = PrevueSync::SyncHistoryIO::QuerySinceDays(destDir.wstring(), 7);
    assert(recent.size() >= 2);

    std::wcout << L"[19/20] Verifying long path support..." << std::endl;
    fs::path longSrc = sandbox / L"longsrc";
    fs::path longDest = sandbox / L"longdest";
    fs::create_directories(longSrc);
    fs::create_directories(longDest);

    std::wstring longRel;
    for (int i = 0; i < 24; ++i) {
        longRel += L"deep_directory_level_" + std::to_wstring(i) + L"\\";
    }
    longRel += L"leaf.txt";
    std::error_code longEc;
    assert(PrevueSync::WinPath::CreateParentDirectories(longSrc.wstring(), longRel, longEc) && !longEc);
    {
        std::ofstream out(PrevueSync::WinPath::Join(longSrc.wstring(), longRel).wstring(), std::ios::binary | std::ios::trunc);
        assert(out && "Failed to create long-path test file");
        out << "long path leaf";
    }

    PrevueSync::SyncStats longStats = PrevueSync::SyncEngine::Sync(
        longSrc.wstring(), longDest.wstring(), noFilters, callbacks);
    assert(longStats.filesCopied == 1 && "long path file should sync");
    assert(fs::exists(PrevueSync::WinPath::Join(longDest.wstring(), longRel)) && "long path destination should exist");

    std::wcout << L"[20/20] Cleaning up test sandbox..." << std::endl;
    RemoveTestSandbox(sandbox);

    std::wcout << L"\n==============================================" << std::endl;
    std::wcout << L"   SUCCESS: All PrevueSync Tests Passed!      " << std::endl;
    std::wcout << L"==============================================" << std::endl;
    return 0;
}

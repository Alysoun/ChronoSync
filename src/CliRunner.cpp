#include "CliRunner.h"
#include "SyncEngine.h"
#include "SyncProfile.h"
#include "SyncJob.h"
#include "NetworkShare.h"
#include "TaskScheduler.h"
#include "Disclaimer.h"
#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include <vector>
#include <string>

namespace PrevueSync {

    static std::wstring GetExecutablePath() {
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        return path;
    }

    static PrevueSync::SyncCallbacks GetCliCallbacks() {
        PrevueSync::SyncCallbacks callbacks;
        callbacks.onLog = [](const std::wstring& message, bool isError) {
            if (isError) {
                std::wcerr << L"[ERROR] " << message << std::endl;
            } else {
                std::wcout << L"[INFO] " << message << std::endl;
            }
        };
        callbacks.onCopyStart = [](const std::wstring& relPath, unsigned long long, size_t fileIndex, size_t totalFiles) {
            std::wcout << L"[" << fileIndex << L"/" << totalFiles << L"] " << relPath << std::endl;
        };
        return callbacks;
    }

    static bool ArgEquals(const std::wstring& arg, const wchar_t* flag) {
        return _wcsicmp(arg.c_str(), flag) == 0;
    }

    static int ParseTimeComponent(const std::wstring& text, int fallback) {
        if (text.empty()) {
            return fallback;
        }
        return _wtoi(text.c_str());
    }

    bool CliRunner::TryRun(int& exitCode) {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!argv || argc <= 1) {
            if (argv) {
                LocalFree(argv);
            }
            return false;
        }

        std::vector<std::wstring> args;
        args.reserve(static_cast<size_t>(argc));
        for (int i = 0; i < argc; ++i) {
            args.emplace_back(argv[i]);
        }
        LocalFree(argv);

        const auto callbacks = GetCliCallbacks();

        if (ArgEquals(args[1], L"--sync") && args.size() >= 3) {
            PrevueSync::SyncProfile profile;
            std::wstring error;
            if (!PrevueSync::SyncProfileIO::LoadFromFile(args[2], profile, error)) {
                std::wcerr << L"Failed to load profile: " << error << std::endl;
                exitCode = 1;
                return true;
            }

            std::wstring networkError;
            if (!PrevueSync::NetworkShare::EnsureAccessible(profile.source, networkError) ||
                !PrevueSync::NetworkShare::EnsureAccessible(profile.destination, networkError)) {
                std::wcerr << networkError << std::endl;
                exitCode = 1;
                return true;
            }

            PrevueSync::PrintCliDisclaimer();
            PrevueSync::SyncStats stats = PrevueSync::SyncEngine::Sync(profile.source, profile.destination, profile.options, callbacks);
            std::wcout << L"Sync complete. Files copied: " << stats.filesCopied
                       << L", skipped: " << stats.filesSkipped
                       << L", pruned: " << stats.itemsDeleted << std::endl;
            exitCode = (stats.verifyFailures > 0) ? 2 : 0;
            return true;
        }

        if (ArgEquals(args[1], L"--queue") && args.size() >= 3) {
            std::vector<PrevueSync::SyncJob> jobs;
            std::wstring error;
            if (!PrevueSync::SyncJobQueueIO::LoadFromFile(args[2], jobs, error)) {
                std::wcerr << L"Failed to load queue: " << error << std::endl;
                exitCode = 1;
                return true;
            }

            PrevueSync::PrintCliDisclaimer();
            for (size_t i = 0; i < jobs.size(); ++i) {
                std::wcout << L"=== Queue job " << (i + 1) << L"/" << jobs.size() << L": " << jobs[i].name << L" ===" << std::endl;
                std::wstring networkError;
                if (!PrevueSync::NetworkShare::EnsureAccessible(jobs[i].source, networkError) ||
                    !PrevueSync::NetworkShare::EnsureAccessible(jobs[i].destination, networkError)) {
                    std::wcerr << networkError << std::endl;
                    exitCode = 1;
                    return true;
                }
                PrevueSync::SyncEngine::Sync(jobs[i].source, jobs[i].destination, jobs[i].options, callbacks);
            }

            exitCode = 0;
            return true;
        }

        if (ArgEquals(args[1], L"--schedule-create") && args.size() >= 3) {
            std::wstring profilePath = args[2];
            bool weekly = false;
            std::wstring dayName = L"MON";
            int hour = 2;
            int minute = 0;
            std::wstring taskName = L"PrevueSyncProfile";

            for (size_t i = 3; i < args.size(); ++i) {
                if (ArgEquals(args[i], L"--daily")) {
                    weekly = false;
                } else if (ArgEquals(args[i], L"--weekly")) {
                    weekly = true;
                } else if (ArgEquals(args[i], L"--day") && i + 1 < args.size()) {
                    dayName = args[++i];
                } else if (ArgEquals(args[i], L"--time") && i + 1 < args.size()) {
                    const std::wstring& timeText = args[++i];
                    size_t colon = timeText.find(L':');
                    if (colon != std::wstring::npos) {
                        hour = ParseTimeComponent(timeText.substr(0, colon), hour);
                        minute = ParseTimeComponent(timeText.substr(colon + 1), minute);
                    }
                } else if (ArgEquals(args[i], L"--name") && i + 1 < args.size()) {
                    taskName = args[++i];
                }
            }

            std::wstring sanitized = PrevueSync::TaskScheduler::SanitizeTaskName(taskName);
            std::wstring exePath = GetExecutablePath();
            std::wstring error;
            bool ok = weekly
                ? PrevueSync::TaskScheduler::CreateWeeklyTask(sanitized, exePath, profilePath, hour, minute, dayName, error)
                : PrevueSync::TaskScheduler::CreateDailyTask(sanitized, exePath, profilePath, hour, minute, error);

            if (!ok) {
                std::wcerr << error << std::endl;
                exitCode = 1;
            } else {
                std::wcout << L"Scheduled task created: " << sanitized << std::endl;
                exitCode = 0;
            }
            return true;
        }

        if (ArgEquals(args[1], L"--schedule-remove") && args.size() >= 3) {
            std::wstring error;
            if (!PrevueSync::TaskScheduler::RemoveTask(PrevueSync::TaskScheduler::SanitizeTaskName(args[2]), error)) {
                std::wcerr << error << std::endl;
                exitCode = 1;
            } else {
                std::wcout << L"Scheduled task removed." << std::endl;
                exitCode = 0;
            }
            return true;
        }

        if (ArgEquals(args[1], L"--help") || ArgEquals(args[1], L"-h") || ArgEquals(args[1], L"/?")) {
            std::wcout << L"PrevueSync CLI\n"
                       << L"  --sync <profile.prevuesync>\n"
                       << L"  --queue <queue.prevuequeue>\n"
                       << L"  --schedule-create <profile> [--daily|--weekly] [--day MON] [--time HH:MM] [--name TaskName]\n"
                       << L"  --schedule-remove <TaskName>\n";
            exitCode = 0;
            return true;
        }

        return false;
    }

} // namespace PrevueSync

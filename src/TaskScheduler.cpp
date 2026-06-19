#include "TaskScheduler.h"
#include <windows.h>
#include <sstream>
#include <vector>
#include <algorithm>

namespace PrevueSync {

    std::wstring TaskScheduler::SanitizeTaskName(const std::wstring& name) {
        std::wstring sanitized = name;
        for (wchar_t& ch : sanitized) {
            if (ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*' || ch == L'?' ||
                ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|') {
                ch = L'_';
            }
        }
        if (sanitized.empty()) {
            sanitized = L"PrevueSyncTask";
        }
        return L"PrevueSync\\" + sanitized;
    }

    static bool RunSchTasks(const std::wstring& arguments, std::wstring& errorMessage) {
        std::wstring command = L"schtasks.exe " + arguments;
        std::vector<wchar_t> buffer(command.begin(), command.end());
        buffer.push_back(L'\0');

        STARTUPINFOW si = { sizeof(si) };
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi = {};

        if (!CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            errorMessage = L"Failed to launch schtasks.exe. Win32 Error: " + std::to_wstring(GetLastError());
            return false;
        }

        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exitCode = 1;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        if (exitCode != 0) {
            errorMessage = L"schtasks.exe failed with exit code " + std::to_wstring(exitCode);
            return false;
        }
        return true;
    }

    static std::wstring Quote(const std::wstring& value) {
        return L"\"" + value + L"\"";
    }

    static std::wstring FormatTime(int hour, int minute) {
        wchar_t buf[8];
        swprintf_s(buf, L"%02d:%02d", hour, minute);
        return buf;
    }

    static std::wstring BuildTaskAction(const std::wstring& executablePath, const std::wstring& profilePath) {
        return L"\"" + executablePath + L"\" --sync \"" + profilePath + L"\"";
    }

    bool TaskScheduler::CreateDailyTask(const std::wstring& taskName,
                                        const std::wstring& executablePath,
                                        const std::wstring& profilePath,
                                        int hour,
                                        int minute,
                                        std::wstring& errorMessage) {
        std::wstringstream args;
        args << L"/Create /F /TN " << Quote(taskName)
             << L" /TR " << Quote(BuildTaskAction(executablePath, profilePath))
             << L" /SC DAILY /ST " << FormatTime(hour, minute);
        return RunSchTasks(args.str(), errorMessage);
    }

    bool TaskScheduler::CreateWeeklyTask(const std::wstring& taskName,
                                         const std::wstring& executablePath,
                                         const std::wstring& profilePath,
                                         int hour,
                                         int minute,
                                         const std::wstring& dayName,
                                         std::wstring& errorMessage) {
        std::wstringstream args;
        args << L"/Create /F /TN " << Quote(taskName)
             << L" /TR " << Quote(BuildTaskAction(executablePath, profilePath))
             << L" /SC WEEKLY /D " << dayName
             << L" /ST " << FormatTime(hour, minute);
        return RunSchTasks(args.str(), errorMessage);
    }

    bool TaskScheduler::RemoveTask(const std::wstring& taskName, std::wstring& errorMessage) {
        std::wstringstream args;
        args << L"/Delete /F /TN " << Quote(taskName);
        return RunSchTasks(args.str(), errorMessage);
    }

} // namespace PrevueSync

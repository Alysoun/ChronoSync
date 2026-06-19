#pragma once

#include <string>

namespace PrevueSync {

    class TaskScheduler {
    public:
        static std::wstring SanitizeTaskName(const std::wstring& name);
        static bool CreateDailyTask(const std::wstring& taskName,
                                    const std::wstring& executablePath,
                                    const std::wstring& profilePath,
                                    int hour,
                                    int minute,
                                    std::wstring& errorMessage);
        static bool CreateWeeklyTask(const std::wstring& taskName,
                                     const std::wstring& executablePath,
                                     const std::wstring& profilePath,
                                     int hour,
                                     int minute,
                                     const std::wstring& dayName,
                                     std::wstring& errorMessage);
        static bool RemoveTask(const std::wstring& taskName, std::wstring& errorMessage);
    };

} // namespace PrevueSync

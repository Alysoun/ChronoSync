cmake_minimum_required(VERSION 3.15)
project(ChronoSync LANGUAGES CXX)

# Require C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Ensure MSVC runtime is statically linked (/MT for Release, /MTd for Debug)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Production GUI application
add_executable(ChronoSync WIN32
    src/Main.cpp
    src/GuiCommon.cpp
    src/GuiCommon.h
    src/GuiDialogs.cpp
    src/GuiDialogs.h
    src/SyncGuiWorker.cpp
    src/SyncGuiWorker.h
    src/ScheduleDialog.cpp
    src/ScheduleDialog.h
    src/AnalysisWindow.cpp
    src/AnalysisWindow.h
    src/PreviewWindow.cpp
    src/PreviewWindow.h
    src/MainWindow.cpp
    src/MainWindow.h
    src/HistoryWindow.cpp
    src/HistoryWindow.h
    src/SyncHashCache.cpp
    src/SyncHashCache.h
    src/SyncHistory.cpp
    src/SyncHistory.h
    src/SyncEngine.cpp
    src/SyncEngine.h
    src/SyncPlan.cpp
    src/SyncPlan.h
    src/SyncPlanAnalysis.cpp
    src/SyncPlanAnalysis.h
    src/SyncExecutor.cpp
    src/SyncExecutor.h
    src/SyncBackup.cpp
    src/SyncBackup.h
    src/PathFilter.cpp
    src/PathFilter.h
    src/SyncProfile.cpp
    src/SyncProfile.h
    src/FileHash.cpp
    src/FileHash.h
    src/WinPath.cpp
    src/WinPath.h
    src/SyncJob.cpp
    src/SyncJob.h
    src/NetworkShare.cpp
    src/NetworkShare.h
    src/DeltaCopy.cpp
    src/DeltaCopy.h
    src/TaskScheduler.cpp
    src/TaskScheduler.h
    src/CliRunner.cpp
    src/CliRunner.h
    src/ChronoSync.rc
    src/resource.h
)
target_include_directories(ChronoSync PRIVATE src)

# Test suite executable (Console subsystem)
add_executable(ChronoSyncTests
    tests/TestMain.cpp
    src/SyncEngine.cpp
    src/SyncEngine.h
    src/SyncPlan.cpp
    src/SyncPlan.h
    src/SyncPlanAnalysis.cpp
    src/SyncPlanAnalysis.h
    src/SyncHashCache.cpp
    src/SyncHashCache.h
    src/SyncHistory.cpp
    src/SyncHistory.h
    src/SyncExecutor.cpp
    src/SyncExecutor.h
    src/SyncBackup.cpp
    src/SyncBackup.h
    src/PathFilter.cpp
    src/PathFilter.h
    src/FileHash.cpp
    src/FileHash.h
    src/NetworkShare.cpp
    src/NetworkShare.h
    src/DeltaCopy.cpp
    src/DeltaCopy.h
    src/WinPath.cpp
    src/WinPath.h
)
target_include_directories(ChronoSyncTests PRIVATE src)

# Compiler-specific settings & libraries
if(MSVC)
    target_compile_options(ChronoSync PRIVATE /W4 /WX /EHsc /utf-8)
    target_compile_options(ChronoSyncTests PRIVATE /W4 /WX /EHsc /utf-8)
    target_link_libraries(ChronoSync PRIVATE dwmapi.lib comctl32.lib ole32.lib shell32.lib bcrypt.lib mpr.lib uxtheme.lib)
else()
    target_compile_options(ChronoSync PRIVATE -Wall -Wextra -Werror)
    target_compile_options(ChronoSyncTests PRIVATE -Wall -Wextra -Werror)
    # Statically link runtime for MinGW/gcc
    target_link_options(ChronoSync PRIVATE -static -static-libgcc -static-libstdc++)
    target_link_options(ChronoSyncTests PRIVATE -static -static-libgcc -static-libstdc++)
    target_link_libraries(ChronoSync PRIVATE dwmapi comctl32 ole32 shell32 bcrypt mpr uxtheme)
    target_link_libraries(ChronoSyncTests PRIVATE bcrypt mpr)
    target_link_options(ChronoSyncTests PRIVATE -static)
endif()

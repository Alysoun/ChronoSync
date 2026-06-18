@echo off
setlocal enabledelayedexpansion

echo ==============================================
echo ChronoSync Windows GUI Build Script
echo ==============================================

:: Check if cl.exe is already available in PATH and environment is initialized
where cl.exe >nul 2>&1
if %errorlevel% equ 0 (
    if not "%INCLUDE%"=="" (
        echo MSVC cl.exe is already in PATH and environment is initialized.
        goto msvc_build
    )
)

:: Try to locate vcvars64.bat from BuildTools or VS2022 Installations
set "VCVARS_PATH="
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
)

if not "%VCVARS_PATH%"=="" (
    echo Found Visual Studio environment setup script at: "%VCVARS_PATH%"
    call "%VCVARS_PATH%" >nul
    goto msvc_build
)

echo MSVC compiler not found. Searching for MinGW g++ fallback...
where g++.exe >nul 2>&1
if %errorlevel% equ 0 (
    echo Found MinGW g++.exe. Compiling using MinGW...
    goto mingw_build
)

echo [ERROR] Neither MSVC (cl.exe) nor MinGW (g++.exe) was found in path.
exit /b 1

:msvc_build
echo [Building ChronoSync GUI with MSVC (/MT)]
rc.exe /nologo /fo ChronoSync.res src\ChronoSync.rc
if %errorlevel% neq 0 (
    echo [ERROR] Resource compilation failed with MSVC.
    exit /b %errorlevel%
)
cl.exe /EHsc /std:c++20 /utf-8 /MT /O2 /W4 /WX /Isrc /Fe:ChronoSync.exe src\Main.cpp src\GuiCommon.cpp src\GuiDialogs.cpp src\SyncGuiWorker.cpp src\ScheduleDialog.cpp src\AnalysisWindow.cpp src\PreviewWindow.cpp src\MainWindow.cpp src\HistoryWindow.cpp src\SyncHashCache.cpp src\SyncHistory.cpp src\SyncEngine.cpp src\SyncPlan.cpp src\SyncPlanAnalysis.cpp src\SyncExecutor.cpp src\SyncBackup.cpp src\PathFilter.cpp src\SyncProfile.cpp src\FileHash.cpp src\WinPath.cpp src\SyncJob.cpp src\NetworkShare.cpp src\DeltaCopy.cpp src\TaskScheduler.cpp src\CliRunner.cpp ChronoSync.res dwmapi.lib comctl32.lib ole32.lib shell32.lib bcrypt.lib mpr.lib uxtheme.lib user32.lib gdi32.lib /link /SUBSYSTEM:WINDOWS
if %errorlevel% neq 0 (
    echo [ERROR] GUI compilation failed with MSVC.
    exit /b %errorlevel%
)

echo [Building ChronoSyncTests with MSVC (/MT)]
cl.exe /EHsc /std:c++20 /utf-8 /MT /O2 /W4 /WX /Isrc /Fe:ChronoSyncTests.exe tests\TestMain.cpp src\SyncEngine.cpp src\SyncPlan.cpp src\SyncPlanAnalysis.cpp src\SyncHashCache.cpp src\SyncHistory.cpp src\SyncExecutor.cpp src\SyncBackup.cpp src\PathFilter.cpp src\FileHash.cpp src\WinPath.cpp src\NetworkShare.cpp src\DeltaCopy.cpp bcrypt.lib mpr.lib
if %errorlevel% neq 0 (
    echo [ERROR] Test compilation failed with MSVC.
    exit /b %errorlevel%
)

:: Clean up build artifacts (.obj files)
if exist *.obj (
    del *.obj
)
if exist ChronoSync.res (
    del ChronoSync.res
)
echo [SUCCESS] MSVC GUI Build Completed.
goto end

:mingw_build
echo [Building ChronoSync GUI with MinGW (static)]
windres -i src\ChronoSync.rc -o ChronoSync_res.o
if %errorlevel% neq 0 (
    echo [ERROR] Resource compilation failed with MinGW.
    exit /b %errorlevel%
)
g++ -std=c++20 -O3 -Wall -Wextra -Werror -Isrc -static -static-libgcc -static-libstdc++ -mwindows -o ChronoSync.exe src/Main.cpp src/GuiCommon.cpp src/GuiDialogs.cpp src/SyncGuiWorker.cpp src/ScheduleDialog.cpp src/AnalysisWindow.cpp src/PreviewWindow.cpp src/MainWindow.cpp src/HistoryWindow.cpp src/SyncHashCache.cpp src/SyncHistory.cpp src/SyncEngine.cpp src/SyncPlan.cpp src/SyncPlanAnalysis.cpp src/SyncExecutor.cpp src/SyncBackup.cpp src/PathFilter.cpp src/SyncProfile.cpp src/FileHash.cpp src/WinPath.cpp src/SyncJob.cpp src/NetworkShare.cpp src/DeltaCopy.cpp src/TaskScheduler.cpp src/CliRunner.cpp ChronoSync_res.o -ldwmapi -lcomctl32 -lole32 -lshell32 -lbcrypt -lmpr -luxtheme -lgdi32
if %errorlevel% neq 0 (
    echo [ERROR] GUI compilation failed with MinGW.
    exit /b %errorlevel%
)

echo [Building ChronoSyncTests with MinGW (static)]
g++ -std=c++20 -O3 -Wall -Wextra -Werror -Isrc -static -static-libgcc -static-libstdc++ -o ChronoSyncTests.exe tests/TestMain.cpp src/SyncEngine.cpp src/SyncPlan.cpp src/SyncPlanAnalysis.cpp src/SyncHashCache.cpp src/SyncHistory.cpp src/SyncExecutor.cpp src/SyncBackup.cpp src/PathFilter.cpp src/FileHash.cpp src/WinPath.cpp src/NetworkShare.cpp src/DeltaCopy.cpp -lbcrypt -lmpr
if %errorlevel% neq 0 (
    echo [ERROR] Test compilation failed with MinGW.
    exit /b %errorlevel%
)
echo [SUCCESS] MinGW GUI Build Completed.

:end
exit /b 0

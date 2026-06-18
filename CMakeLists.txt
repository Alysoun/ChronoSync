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
    src/SyncEngine.cpp
    src/SyncEngine.h
    src/PathFilter.cpp
    src/PathFilter.h
    src/SyncProfile.cpp
    src/SyncProfile.h
    src/FileHash.cpp
    src/FileHash.h
    src/SyncJob.cpp
    src/SyncJob.h
)
target_include_directories(ChronoSync PRIVATE src)

# Test suite executable (Console subsystem)
add_executable(ChronoSyncTests
    tests/TestMain.cpp
    src/SyncEngine.cpp
    src/SyncEngine.h
    src/PathFilter.cpp
    src/PathFilter.h
    src/FileHash.cpp
    src/FileHash.h
)
target_include_directories(ChronoSyncTests PRIVATE src)

# Compiler-specific settings & libraries
if(MSVC)
    target_compile_options(ChronoSync PRIVATE /W4 /WX /EHsc)
    target_compile_options(ChronoSyncTests PRIVATE /W4 /WX /EHsc)
    target_link_libraries(ChronoSync PRIVATE dwmapi.lib comctl32.lib ole32.lib shell32.lib bcrypt.lib)
else()
    target_compile_options(ChronoSync PRIVATE -Wall -Wextra -Werror)
    target_compile_options(ChronoSyncTests PRIVATE -Wall -Wextra -Werror)
    # Statically link runtime for MinGW/gcc
    target_link_options(ChronoSync PRIVATE -static -static-libgcc -static-libstdc++)
    target_link_options(ChronoSyncTests PRIVATE -static -static-libgcc -static-libstdc++)
    target_link_libraries(ChronoSync PRIVATE dwmapi comctl32 ole32 shell32 bcrypt)
    target_link_libraries(ChronoSyncTests PRIVATE bcrypt)
    target_link_options(ChronoSyncTests PRIVATE -static)
endif()

# Project Plan: PrevueSync Folder Synchronizer (GUI Refinements)

PrevueSync is a high-performance, native Windows C++ desktop utility for ultra-fast differential folder synchronization. It features a modern dark-themed Graphical User Interface (GUI), supports selective directory pruning, runs synchronization on a background worker thread, displays a Sync Preview dialog using a high-performance Virtual ListView, and employs atomic, corruption-free file replacement operations.

---

## 1. Specifications & Core Components

### Core Tech Stack
- **Language**: C++20 (`std::filesystem`, standard container templates).
- **GUI Engine**: Native Win32 API (`windows.h`, `commctrl.h`, `dwmapi.h`).
- **COM Services**: Windows Shell COM interfaces (`IFileDialog` / `IFileOpenDialog`) for a modern folder picker.
- **Multithreading**: Standard library `std::thread` to isolate synchronization work.
- **Virtual ListView**: High-performance Virtual List View control (`LVS_OWNERDATA`) that queries list items dynamically via `LVN_GETDISPINFOW`.
- **I/O Engine**: Native Windows `CopyFileExW` with custom progress callback routines.
- **Linking**: Static runtime linking (`/MT` on MSVC, `-static` on MinGW) for a standalone, portable executable.

### Component Design
1. **Desktop GUI Window**: Fixed 620x500 main window styled with a Visual Studio Dark theme background (`RGB(45, 45, 48)`).
2. **Action Buttons**:
   - `Preview Changes`: Performs comparative dry-run and displays a Sync Preview pop-up list.
   - `Sync Now`: Runs the physical differential synchronization.
   - `Undo Pruning`: Restores deleted files from the `.prevue_trash` folder.
3. **Responsive Threading**: Disables action and folder-browse buttons during operations. Reports progress using thread-safe `PostMessageW` dispatch queues.
4. **Checkbox Contrast Fix**: Employs a borderless checkbox beside an independent static control (`SS_NOTIFY` enabled) to allow proper text painting in bright white under Visual Styles v6.
5. **Sync Preview Window**: Secondary window featuring a dark-themed ListView that renders comparison results, complete with a symmetrically aligned **Export CSV...** action button.
6. **CSV Export Utility**: Uses modern COM `IFileSaveDialog` to save preview items to a file with UTF-8 BOM encoding and standard RFC 4180 escaping.

---

## 2. Directory Layout
- `src/`
- `Main.cpp` - GUI Window Setup, secondary Preview window procedure with CSV export dialog, Virtual ListView notifications, COM folder picking, and worker thread handlers.
- `SyncEngine.h` - Add `PreviewItem` and `SyncEngine::Preview` declarations.
- `SyncEngine.cpp` - Scan, compare, copy, and delete implementations (reused for both sync and dry-runs).
- `tests/`
- `TestMain.cpp` - Automated test suite verifying differential logic boundaries.
- `CMakeLists.txt` - Target building script linking `dwmapi`, `comctl32`, and `ole32`.
- `build.bat` - Multi-compiler build driver prioritising local MSVC and static compilation options.

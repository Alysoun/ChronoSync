# ChronoSync

High-performance native Windows folder synchronizer with differential sync, preview, pruning with undo, and a dark-themed GUI.

## Features

- **Differential sync** — copies only new or changed files (timestamp or SHA256 comparison)
- **SHA256 verification** — optional content-hash compare mode and verify-after-copy
- **Atomic file replacement** — writes via `.chrono_tmp` then renames with `MOVEFILE_WRITE_THROUGH`
- **Prune with undo** — removed files archived to `.chrono_backups/<timestamp>/` (or legacy `.chrono_trash`)
- **Versioned backups** — keeps the last N prune snapshots (default: 5)
- **Multi-job queue** — queue multiple source→destination jobs, save/load `.chronoqueue` files, run sequentially
- **Symlink & junction preservation** — reparse points are recreated at the destination
- **Preview dialog** — virtual ListView with sort, search/filter, CSV export, and right-click **Show in File Explorer**
- **Include/exclude filters** — glob patterns (default excludes: `*.pkl`, `node_modules`, `*.zip`)
- **Sync profiles** — save/load source, destination, prune, filters, and compare options (`.chronosync` JSON)

## Build

Requires CMake 3.15+, a C++20 compiler (MSVC or MinGW), and Windows.

```bat
build.bat
```

Or with CMake directly:

```bat
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

Outputs: `ChronoSync.exe`, `ChronoSyncTests.exe`

## Run tests

```bat
ChronoSyncTests.exe
```

## Profile format

Profiles are JSON files with extension `.chronosync`:

```json
{
  "version": 1,
  "name": "My Backup",
  "source": "C:\\Projects\\MyApp",
  "destination": "D:\\Backups\\MyApp",
  "prune": true,
  "sha256Compare": false,
  "verifyAfterCopy": false,
  "versionedBackups": true,
  "maxBackupVersions": 5,
  "includePatterns": [],
  "excludePatterns": ["*.pkl", "node_modules", "*.zip"]
}
```

## Queue format

Job queues use `.chronoqueue` JSON files with a `jobs` array. Each job mirrors the profile fields above.

## Roadmap

| Priority | Feature |
|----------|---------|
| Low | Scheduled syncs |
| Low | Network share support |
| Low | Delta block-copying |

## License

MIT (see repository for details).

# ChronoSync

High-performance native Windows folder synchronizer with differential sync, preview, pruning with undo, and a dark-themed GUI.

## Features

- **Differential sync** — copies only new or changed files (timestamp or SHA256 comparison)
- **Atomic block-compare copy** — for same-size files, compares 4MB blocks and rebuilds the destination atomically via `.chrono_tmp`; `deltaBytesWritten` counts only changed block data (unchanged blocks are copied from the existing destination into the temp file)
- **SHA256 verification** — optional content-hash compare mode and verify-after-copy
- **Atomic file replacement** — writes via `.chrono_tmp` then renames with `MOVEFILE_WRITE_THROUGH`
- **Prune with undo** — removed files archived to `.chrono_backups/<timestamp>/` (or legacy `.chrono_trash`)
- **Versioned backups** — keeps the last N prune snapshots (default: 5)
- **Multi-job queue** — queue multiple source→destination jobs, save/load `.chronoqueue` files, run sequentially
- **Scheduled syncs** — create daily/weekly Windows Task Scheduler jobs from the GUI or CLI
- **Network share support** — UNC path detection, connection retry, and copy retries on transient errors
- **Symlink & junction preservation** — reparse points are recreated at the destination
- **Preview dialog** — virtual ListView with sort, search/filter, CSV export, and right-click **Show in File Explorer**
- **Include/exclude filters** — glob patterns (default excludes: `*.pkl`, `node_modules`, `*.zip`)
- **Sync profiles** — save/load source, destination, prune, filters, and compare options (`.chronosync` JSON)
- **Plan analysis** — **Analyze Plan** summarizes transfer size, largest files, category and extension breakdown, duration estimate, and LOW/MEDIUM/HIGH risk before syncing; preview shows risk headline
- **Sync history** — each sync records run metadata and a destination snapshot under `.chrono_history`; **History...** shows recent activity and compares snapshots for `+ / - / ~` counts

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

## CLI (headless)

ChronoSync can run without the GUI for automation and scheduled tasks:

```bat
ChronoSync.exe --sync profile.chronosync
ChronoSync.exe --queue jobs.chronoqueue
ChronoSync.exe --schedule-create profile.chronosync --daily --time 02:00 --name NightlyBackup
ChronoSync.exe --schedule-create profile.chronosync --weekly --day MON --time 03:30 --name WeeklyBackup
ChronoSync.exe --schedule-remove NightlyBackup
ChronoSync.exe --help
```

Scheduled tasks invoke `ChronoSync.exe --sync <profile>` at the configured time.

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
  "deltaBlockCopy": false,
  "includePatterns": [],
  "excludePatterns": ["*.pkl", "node_modules", "*.zip"]
}
```

`deltaBlockCopy` enables atomic block-compare copy (see feature list above). The JSON key name is historical; the operation always rebuilds via `.chrono_tmp` rather than patching blocks in place.

## Queue format

Job queues use `.chronoqueue` JSON files with a `jobs` array. Each job mirrors the profile fields above.

## Roadmap

ChronoSync already covers the usual sync-tool checklist (preview, filters, profiles, queues, scheduling, SHA verification, versioned backups, CLI, and more). The next differentiator is **visibility and confidence** — knowing exactly what will happen before files are touched.

### Visibility & confidence (next focus)

| Priority | Feature | Description |
|----------|---------|-------------|
| High | **Analyze Plan / "Explain this sync"** | Done — impact summary, risk scoring, file-type breakdown, largest files, duration estimate |
| High | **Sync impact summary** | Done — integrated into Analyze Plan and preview risk headline |
| High | **Risk scoring** | Done — LOW/MEDIUM/HIGH with human-readable reasons |
| Medium | **File-type analytics** | Done — category and top-extension breakdown in Analyze Plan |
| Medium | **Historical change tracking** | Done — per-sync metadata log in `.chrono_history` with last-7-days summary |
| Medium | **Snapshot diff viewer** | Done — compare two history snapshots for `+ / - / ~` counts in History dialog |

### Deep Windows integration (performance & reliability)

| Priority | Feature | Description |
|----------|---------|-------------|
| Medium | **NTFS USN Journal scanning** | Record USN index after sync; next run queries the change journal instead of full tree walk — O(changes) instead of O(all files) |
| Medium | **VSS open-file backup** | Optional Volume Shadow Copy snapshot for locked files (`ERROR_SHARING_VIOLATION` during IDE builds, games, databases) |
| Low | **Sparse file preservation** | Detect `FILE_ATTRIBUTE_SPARSE_FILE`; copy only allocated ranges via `FSCTL_QUERY_ALLOCATED_RANGES` / `FSCTL_SET_SPARSE` so VHDX/PAK backups don't bloat |

### Major scope (later)

| Priority | Feature | Description |
|----------|---------|-------------|
| Low | **Bi-directional sync & collision UI** | Split-screen resolver when both sides changed (keep source, keep dest, fork side-by-side) |
| Low | **Structured profile format** | Replace hand-rolled JSON parser if profiles gain schema versioning or richer nesting |

### Design principle

> **Know exactly what ChronoSync is about to do before you let it touch your files.**

Speed matters, but the standout bet is a smarter preview and plan analysis layer — not another copy-engine trick.

## License

MIT (see repository for details).

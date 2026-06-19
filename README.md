# ChronoSync

High-performance native Windows folder synchronizer with differential sync, preview, pruning with undo, and a dark-themed GUI.

**Design goal:** confidence before execution — know exactly what will happen before files are touched.

**Disclaimer:** ChronoSync can overwrite or permanently delete files. You use this software at your own risk — see [Disclaimer](#disclaimer) below.

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

## Current capability

Numbers from real-world use (not formal benchmarks). Updated as larger runs are validated.

| Metric | Observed |
|--------|----------|
| Items scanned (source tree) | ~240k |
| Items scanned (destination tree) | ~290k |
| Incremental sync (already in sync) | ~215k skipped, handful of files copied |
| Largest single planned transfer | ~3.2 GB |
| History snapshot cap | 25,000 items per destination tree |
| Long path support (`\\?\`) | Yes |
| Automated regression tests | 20 |

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

The highest-impact item here is **NTFS USN Journal scanning**. Today a large tree means walking every file to find a few changes; with the change journal, the same work becomes O(changes) instead of O(all files). That matters more than most UI polish for massive project trees.

| Priority | Feature | Description |
|----------|---------|-------------|
| **High** | **NTFS USN Journal scanning** | Record USN index after sync; next run queries the change journal instead of full tree walk — ask NTFS what changed, not the whole disk |
| Medium | **VSS open-file backup** | Optional Volume Shadow Copy snapshot for locked files (`ERROR_SHARING_VIOLATION` during IDE builds, games, databases) |
| Low | **Sparse file preservation** | Detect `FILE_ATTRIBUTE_SPARSE_FILE`; copy only allocated ranges via `FSCTL_QUERY_ALLOCATED_RANGES` / `FSCTL_SET_SPARSE` so VHDX/PAK backups don't bloat |

### Major scope (later)

| Priority | Feature | Description |
|----------|---------|-------------|
| Low | **Bi-directional sync & collision UI** | Split-screen resolver when both sides changed (keep source, keep dest, fork side-by-side) |
| Low | **Structured profile format** | Replace hand-rolled JSON parser if profiles gain schema versioning or richer nesting |

### Design principle

> **Know exactly what ChronoSync is about to do before you let it touch your files.**

ChronoSync is not trying to be the fastest or most feature-dense sync tool. The bet is **confidence before execution** — preview, analyze, history, and backups so you can approve work before it runs.

Speed still matters, but the standout differentiator is a smarter preview and plan analysis layer — not another copy-engine trick.

### Enterprise readiness (long-term)

Enterprise adoption is not a single certification badge. It is a stack of requirements across **security**, **reliability**, **compliance**, and **operational maturity**. ChronoSync already has a safety-first architecture (atomic writes, preview/analyze, versioned backups, undo, SHA256, history, long-path support). The tracks below are what CTOs, CISOs, and compliance teams typically evaluate before deployment in finance, healthcare, government, or regulated SaaS environments.

**Today:** suitable for power users and small teams with strong local observability.

**Target:** packaged, auditable, deployable software an IT department can roll out with confidence.

| Track | Status | Roadmap |
|-------|--------|---------|
| **Security hardening** | Partial | Code signing (EV ideal); signed/tamper-proof updates; static analysis + fuzzing; STRIDE / MITRE ATT&CK threat modeling; formal symlink/path-traversal/race reviews; secure temp-file policy (atomic `.chrono_tmp` already); no elevation-of-privilege paths; no plaintext secrets |
| **Compliance alignment** | Not started | Organizational, not only code: documented processes, access controls, audit logs, incident response, vendor risk assessments, data retention. Map to frameworks customers require: SOC 2 Type II, ISO 27001, HIPAA, FedRAMP, GDPR, NIST 800-53 |
| **Enterprise deployment** | Partial | MSI + silent install/uninstall; Group Policy; registry/JSON central config; portable mode; run without admin for normal sync; remote configuration; centralized log shipping |
| **Scalability & stress testing** | Partial | Formal torture tests: millions of files, multi-TB trees, 24/7 soak (memory leaks); graceful degradation under network loss, disk full, locked files, permission errors, AV interference |
| **Observability & monitoring** | Partial | Local logs/history exist; add Windows Event Log, syslog, SIEM-friendly JSON (Splunk/Sentinel/Elastic), optional SNMP, central dashboards |
| **RBAC & policy** | Not started | Admin vs operator roles; lock dangerous options (e.g. prune) via policy; audit trail of who ran what, when, with which profile |
| **Enterprise support** | Not started | SLAs, support contracts, onboarding, security questionnaires, pen-test reports, business continuity, liability coverage — business layer as much as engineering |
| **Formal QA & release engineering** | Partial | `ChronoSyncTests.exe` + reproducible builds exist; add version pinning, release notes discipline, regression matrix, coverage targets, LTS branches |
| **Data integrity guarantees** | Strong base | Already: SHA256 compare/verify, atomic copy, delta copy, versioned backups, undo, snapshot history. Add: end-to-end integrity reports per run, optional signed/tamper-evident snapshot manifests |

#### Suggested phasing

| Phase | Focus | Outcomes |
|-------|-------|----------|
| **1 — Prove it** | Stress testing, Event Log / structured logging, MSI silent install, release notes + LTS tagging | IT can pilot on one fleet; failures are measurable |
| **2 — Secure it** | Code signing, update signing, static analysis/fuzzing, threat model doc, pen test | Security review questionnaires become answerable |
| **3 — Operate it** | Central config, RBAC/policy, audit trail, SIEM export | Multi-user orgs can enforce policy |
| **4 — Certify it** | Compliance program (SOC 2 / ISO 27001 as needed), support SLAs, vendor docs | Enterprise procurement-ready |

#### Already enterprise-grade (design)

These are ahead of many commercial sync tools and do not need reinvention — only formal verification and documentation:

- Transparent preview and plan analysis before destructive work
- Atomic destination writes and block-compare copy
- Versioned prune backups with undo
- Per-run history and snapshot diff (within size limits)
- Optional SHA256 compare and post-copy verification
- Disclaimer and explicit risk surfacing in the UI

## Disclaimer

ChronoSync copies, overwrites, and may permanently delete files on your computer, especially when options such as **Prune destination** are enabled.

**YOU USE THIS SOFTWARE AT YOUR OWN RISK.** The authors and contributors of ChronoSync are not responsible for any lost, corrupted, overwritten, or deleted data, or for any other damage arising from your use of this software.

ChronoSync is designed to help prevent mistakes by providing **Preview**, **Analyze Plan**, **History**, and **Versioned Backups**.

However, no software can guarantee protection from user error. Always maintain independent backups of important data.

You are solely responsible for:

- Backing up important data before syncing
- Verifying source and destination folders
- Reviewing **Preview** and **Analyze Plan** before destructive operations
- Understanding the sync options you enable

By downloading, building, or running ChronoSync (GUI or CLI), you agree to these terms.

The GUI shows the full disclaimer on first launch and provides a link to read it again. The CLI prints a notice when running `--sync` or `--queue`.

## License and ownership

**ChronoSync is proprietary dual-licensed software. It is not MIT/GPL/open source.**

| Document | Purpose |
|----------|---------|
| [LICENSE](LICENSE) | Full dual-license terms |
| [COPYRIGHT](COPYRIGHT) | Who owns the software and IP |

**Copyright (c) 2026 Michael Gartner.** All rights reserved.

### Dual license at a glance

| | **License A — Gratis** | **License B — Commercial** |
|--|------------------------|----------------------------|
| **Cost** | Free | Negotiated (written agreement) |
| **Personal use** | Yes | N/A (use License A) |
| **Internal business use** | Yes — e.g. a 50-person firm syncing its own project files | N/A (use License A) |
| **Commercial redistribution / resale** | No | Written permission required |
| **SaaS / hosted offering to third parties** | No | Written permission required |
| **OEM bundling** | No | Written permission required |

**License A** covers personal and internal business use of [official releases](LICENSE) (binaries or source packages published via the official GitHub repo or other channels the copyright holder designates) at no charge.

**License B** is required for commercial redistribution, resale, SaaS offerings, OEM bundling, and other commercial uses beyond License A.

ChronoSync is currently held by the copyright holder personally. The copyright holder may **later assign or license** intellectual property to a wholly owned legal entity (e.g. an LLC) if and when they choose. That does not revoke your right to use a release you lawfully obtained under the terms published with that release.

**Commercial licensing:** Rpracing00@gmail.com

When you publish a GitHub Release, include `LICENSE` and `COPYRIGHT` in the zip alongside `ChronoSync.exe` and `README.md`.

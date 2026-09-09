# Changelog
## [2.2] — 2026-09-03

### Queues and uninstallation

- Added a shared queue operation for uninstalling installed titles
- Press `-` in the installed-title browser to add a title to the uninstall queue
- Press `X` there to open the queue and review pending operations
- Press `+` in the queue to process installations, downloads, and uninstallations sequentially
- Stored independent MCP title data for queued uninstallations to keep them valid after leaving the browser

## [2.1] — 2026-09-02

Period: August 26 – September 2, 2026  
Base: `99a03cb` → `73e7b87` (18 commits)

### Version and identity

- Version bumped from **1.157-ALPHA2** to **2.1**
- Copyright updated to **© 2025-2030 Funregis**
- **Funregis** added to the developer credits on the main menu
- README updated to state that this fork is based on [NUSspli by V10lator](https://github.com/V10lator/NUSspli)

### Network and downloads

#### Network reset (`resetNetwork`)

- Removed early return that sometimes prevented network reset
- Fixed `ACGetCloseStatus()` usage (code 0 = success, 1 = in progress)
- Added a **timeout** with active polling (every 10 ms)
- Full disconnect/reconnect cycle: `ACFinalize` → `socket_lib_finish` → `socket_lib_init` → `ACInitialize` → `ACConnect`
- On **"Error closing network!"**, force a full connection reset instead of calling `ACClose()` in a loop
- Proper cleanup on reconnect failure

#### libCURL / sockets

- Socket options (WinScale, TCP SACK, etc.) no longer fail the transfer when unsupported (`ENOPROTOOPT`)
- Added `CURLOPT_NOSIGNAL`, `CURLOPT_CONNECTTIMEOUT` (30 s), and `CURLOPT_MAXREDIRS`
- Refactored `initDownloader()` with simplified error handling
- **Retry on closed connection**: `curlReuseConnection = false` after any error to force a fresh connection
- Added a CA certificate bundle (`data/ca-certs.pem`)

#### Network error messages

- **Detailed, contextual error messages** based on the curl error code (DNS, timeout, SSL, etc.)
- Curl errors translated via `localise()`
- Fixed auto-resume countdown display (remaining seconds)
- New translations: `Error closing network!`, `Error connecting to network!`, and related network messages

### Notifications

- Wiimote rumble and GamePad LED handled in the same notification thread
- LED stays on for **4 seconds** before turning off
- Wiimote rumble only enabled when the rumble notification method is selected
- GamePad rumble still handled separately via `VPADControlMotor`
- LED turned off when notifications are shut down

### Stability and memory

- **Removed shared static buffers** (`getStaticScreenBuffer`, `getStaticLineBuffer`, `getStaticPathBuffer`)
- Replaced with stack buffers throughout the codebase (downloader, menus, renderer, etc.)
- Fixed text truncation in the renderer (buffer overflow, ellipsis)
- Major **SDL_FontCache** update

### Localisation

- Added many missing strings across **8 languages** (French, German, Spanish, Italian, Portuguese, Brazilian Portuguese, Turkish, Welsh)
- Network, SSL, and `title.tmd` / `title.tik` save errors are now translated

### Build

- **Makefile**: switched to `-O3`, `--gc-sections`, removed aggressive `-Ofast`/LTO
- **Dockerfile**: adjusted compile flags, static libraries built without PIC
- **build.py**: run `SDL2/setup.sh` via `sh`, download certificates without SSL verification

### Commits

| Date   | Commit    | Description                                      |
|--------|-----------|--------------------------------------------------|
| 08/26  | `4ca67c5` | Optimisations, fixes, reliability improvements   |
| 08/26  | `a721f40` | Optimisations, fixes, reliability improvements   |
| 08/26  | `e7d7d81` | Optimisations, fixes, reliability improvements   |
| 08/26  | `1ca4957` | Optimisations, fixes, reliability improvements   |
| 08/26  | `7876223` | Optimisations, fixes, reliability improvements   |
| 08/26  | `3be7668` | Optimisations, fixes, reliability improvements   |
| 08/26  | `c9ea831` | Optimisations, fixes, reliability improvements   |
| 08/26  | `6e5b7cf` | Optimisations, fixes, reliability improvements   |
| 08/26  | `80c8025` | README update                                    |
| 08/26  | `c571ca0` | Better notifications                             |
| 08/26  | `f7d41e1` | Build update                                     |
| 08/26  | `586d87f` | Version update → 2.1                             |
| 08/26  | `cc2be0b` | Localisation, optimisations, fixes               |
| 08/27  | `7019cd8` | Better network handling (sockets, reset)         |
| 08/27  | `c3e04fa` | Better network handling                          |
| 09/01  | `3a5e822` | Better network handling (AC API, timeout)        |
| 09/01  | `701d7ab` | Retry on closed connection + CA certificates     |
| 09/02  | `73e7b87` | Add Funregis to developer credits                |

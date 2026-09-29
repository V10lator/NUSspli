# AGENTS.md

Guidance for AI agents and human contributors working on NUSspli. It collects
the build, style and workflow rules of this repository. README.md covers the
user facing side (features, install, release downloads).

## The project

NUSspli is Wii U homebrew (GPLv3, `LICENSE`) that downloads titles from
Nintendo's Update Servers (NUS), installs them to internal or external storage,
creates fake tickets when needed, ships a keygen, an on screen keyboard and an
auto updater.

- Plain C, plus `src/swkbd_wrapper.cpp` (C++20) around the software keyboard.
- Toolchain: devkitPro (`devkitppc`, `wut`) plus the devkitPro SDL2 portlibs
  (SDL2, SDL2_image, SDL2_mixer, SDL2_ttf, harfbuzz, jansson), libcurl with
  mbedTLS and nghttp2. All versions are pinned by the `Dockerfile`.
- Two packaging flavours: **Aroma** (`.wuhb`) and **Channel** (installed with
  WUPInstaller), each as a normal and a `-DEBUG` build.

### Layout

| Path         | Contents                                                            |
| ------------ | ------------------------------------------------------------------- |
| `src/`       | Application code, `src/menu/` holds the screens                     |
| `include/`   | Headers, mirroring `src/` (`include/menu/` for the screens)         |
| `data/`      | Content shipped inside the app: `locale/*.json`, textures, audio    |
| `meta/`      | Channel metadata, icons, `meta/baselocale.json` (all English keys)  |
| `.github/`   | CI workflows and dependabot                                         |
| `build.py`   | Packaging script used by CI and by the Docker build below           |

`zlib/` and `SDL_FontCache/` are git submodules, `SDL_FontCache-patches/`
holds local patches that `make real` applies on top of them.

## Building

Docker is the supported route and matches CI:

```sh
docker build -t nussplibuilder .
docker run --rm -v ${PWD}:/project nussplibuilder python3 build.py
```

`build.py` needs network access: it downloads `src/gtitles.c` (title database
from `napi.v10lator.de`), `data/ca-certs.pem` and `NUSPacker.jar` when they are
missing, writes `version.txt` from `NUSSPLI_VERSION` and produces `out/` and
`zips/`. It only builds the release flavours when the version contains neither
`ALPHA` nor `BETA`.

Without Docker: set `DEVKITPRO`, then `make` (debug) or `make release`. The
`real` target runs `git submodule update --init --recursive` and applies
`SDL_FontCache-patches/*.patch` before compiling.

The version lives in `include/utils.h` (`NUSSPLI_VERSION`). Bump it there, never
in `version.txt`.

### Never commit generated files

`src/gtitles.c`, `data/ca-certs.pem`, `nuspacker.jar`, `NUSPacker.jar`,
`version.txt`, `encryptKeyWith`, `out/`, `zips/`, `debug/`, `release/`,
`payload/`, `*.elf`, `*.rpx`, `*.wuhb`, `TODO.txt` are ignored or produced by
the build. Do not stage them, and do not stage modified submodule checkouts by
accident (`git status` must show only what you actually changed).

## Checks (CI)

- `.github/workflows/master.yml` runs on every push to `master`,
  `.github/workflows/pr.yaml` on pull requests. Both run two jobs:
  1. **clang-format** over `./src` and `./include` with the command shown
     below. The generated `src/gtiles.c` and the vendored
     `src/SDL_FontCache.c` / `include/SDL_FontCache.h` are listed in
     `.clang-format-ignore`, so the formatter skips them on its own.
  2. **Build** via `python3 build.py` inside the image built from the
     `Dockerfile` (Docker layer cache keyed on it), uploading the Aroma and
     Channel artifacts.
- A commit subject that looks like `v<version>` and a version without
  `ALPHA`/`BETA` additionally tags the release and publishes the zips.
- There is no automated test suite. Verify changes with a `-DEBUG` build on a
  console or in an emulator, and check the release build still compiles.

Run the formatter yourself before committing. The first command is the check
the CI job runs (`--dry-run --Werror`, no `-i`: it prints the diffs and exits
non-zero when anything is unformatted), the second one rewrites the files in
place:

```sh
docker run --rm -v ${PWD}:/src xianpengshen/clang-tools:22 \
  clang-format --dry-run --Werror \
  $(find src include -type f \( -name "*.c" -o -name "*.cpp" \
  -o -name "*.h" -o -name "*.hpp" \))

docker run --rm -v ${PWD}:/src xianpengshen/clang-tools:22 \
  clang-format -i \
  $(find src include -type f \( -name "*.c" -o -name "*.cpp" \
  -o -name "*.h" -o -name "*.hpp" \))
```

## Code style

- `.clang-format` is the authority: WebKit base, **Allman braces**, four
  spaces, `PointerAlignment: Right` (`char *p`), `SpaceBeforeParens: Never`
  (`if(`, `while(`), `IndentCaseLabels: true`, aligned macro continuations.
- Keep the GPLv3 header block that every source file starts with, updated for
  the file you touch.
- Compile warning free under the `-Wall -Wextra -Wundef -Wshadow
  -Wpointer-arith -Wcast-align` set of the `Makefile`.
- UI strings are English source text passed through `localise()`, so keep them
  plain and translatable.
- Code comments are written in **English**, too: they explain why something
  works the way it does for everyone who reads the file later.

## Architecture notes

- `src/main.c` boots the subsystems one loading screen at a time (filesystem,
  renderer, crypto, MCP, sanity check, notifications, downloader, I/O thread,
  config, SWKBD, queue) and then enters `mainMenu()`.
- App lifetime is driven by `AppRunning()` (`include/state.h`): it pumps the
  SDL/ProcUI events and returns `false` once the app has to exit. Only the main
  thread calls it with `true`; workers pass `false` and merely observe the
  state.
- Every menu is a screen of the UI kernel (`include/ui.h`): `uiRun()` is the
  one main loop instead of a loop per menu, a screen is a pair of update
  (input and state) and render (drawing), pushed with `uiPush()` or opened
  modally with `uiModal()`. `uiFrame()` runs one frame (events, input,
  update, render when the picture is dirty, present, VSync), so reacting to
  `vpad.trigger` in update is what replaced the old redraw flag.
- Frame drawing goes through `include/renderer.h`: `startNewFrame()`,
  `textToFrame()` / `textToFrameColored()`, `lineToFrame()`, `barToFrame()`...
  and finally `drawFrame()`, which presents the frame and keeps the pace.
- Dialogs and status screens live in `src/menu/menuUtils.c`
  (`drawErrorFrame()`, `showErrorFrame()`, `showFinishedScreen()`, the screen
  log via `addToScreenLog()`). Prefer them over ad hoc drawing.
- Localisation: `localise("English text")` falls back to the key itself, so
  English needs no file. Other languages load `data/locale/<Name>.json` at
  runtime. When you add a user visible string, add its key to
  `meta/baselocale.json` **and** to every `data/locale/*.json`.
- Threads (`include/thread.h`): downloader on CPU 0, I/O thread on CPU 2,
  socket pool on CPU 1/2, UI on the main thread. Priorities and stack sizes are
  defined there - respect them instead of inventing new values.
- Config is stored as `sd:/NUSspli.txt` (`CONFIG_PATH` in `include/config.h`).

## Git rules

- Commit messages are written in **English** (like the code comments).
- Commit messages use an **imperative subject with a body** explaining why the
  change is needed. Do not use apostrophes in commit messages.
- One commit per new function or bug fix: every commit carries a single
  focused change.
- Format the commit header according to https://github.com/jaywcjlove/changelog-generator/blob/v2.4.0/README.md#getting-started
  - Load that section instead of working from memory, it is the authority
    and may change.
  - Styling guideline only: the release notes are generated from the
    headers between two tags, so a header outside of the format makes the
    changelog of the next version look worse, but nothing breaks.
  - Exception: the release commit `v<version>` keeps its plain subject,
    else the release job does not fire.
- Squash commits that belong together when that makes the history clearer and
  the commits are **not on `https://github.com/V10lator/NUSspli/commits/master/`
  yet**. That link is the measure, never `origin/master`: an agent works in a
  fork and its own `origin/master` can be anywhere it likes up to the PR.
  Belong together means one thought: a follow up, a cleanup or a rework of a
  detail of the change next to it, a second commit that only exists because the
  first one was incomplete, and a fix for a feature that the same branch added -
  as long as the branch has not reached that link, the repair belongs to the
  feature and no fix header is needed for code that is not there. Two commits
  stay separate when the second one tells a story of its own - another bug,
  another user visible behaviour - and the first one is already on
  `https://github.com/V10lator/NUSspli/commits/master/`: the release notes are
  built from the headers between two tags, so a fix folded into a `feat(...)` or
  a `refactor(...)` header vanishes from them. Whether a fix deserves its own
  line in them is decided before the branch lands there, not afterwards. While
  squashing, update the commit message and the code comments if they no longer
  describe the result.
- Never use `git stash`.
- Never stage dirty submodules, generated files or unrelated untracked files.
- If you are an AI agent: author commits with **your own identity**, not with
  the maintainer's name (for example `MiMo-V2.6-Flash <noreply@open-code.ai>`).
  Credit human contributors in `Co-Authored-By:` trailers instead of putting
  them into the author field. A commit you wrote on your own carries no such
  trailer: it credits a contribution that was merged into that change, not the
  report of the bug the change fixes.
- When two commits are squashed into one, the author of the commit the change
  lands in survives. The rule follows the direction of the squash, not the
  identity of the two authors: the commit that gets folded away can be by the
  maintainer, by a third party or by another AI.
  - Folding an AI commit into another commit keeps the author of that other
    commit with his `Signed-off-by:` untouched, the AI is added as
    `Co-Authored-By:`.
  - Folding another commit into an AI commit makes the AI the author, and
    the other author moves out of the author field into `Co-Authored-By:`.
    A `Signed-off-by:` he carried becomes that line - neither kept nor
    added beside it. Without a sign-off there is nothing to replace, the
    line is simply added. The AI adds no `Co-Authored-By:` for itself:
    it is the author now.
  - A plain rebase keeps every author and every trailer as it is.
- Rewriting an existing commit moves the identity with it, not only the
  message: whoever the commit belongs to afterwards sits in the author line,
  and the name that comes out of that line goes into `Co-Authored-By:` - or
  out of the message entirely when it is the AI, because an AI never credits
  its own name on a commit it is the author of. Author and trailer may never
  name the same identity, so a leftover `Co-Authored-By: <own name>` is
  dropped when a header rewrite or a chain rebuild puts the AI into the
  author field. Check a rewritten chain with
  `git log --format='%h %an | %(trailers:key=Co-Authored-By,valueonly)' <range>`
  and fix every line that repeats the author.

## Pull requests

- **One new feature or bug fix per pull request.** Do not bundle unrelated
  changes into one PR, open a second one instead.
- No pointless code refactoring and no pointless code style changes: a PR
  that fixes a bug stays about that bug fix, formatting is clang-format's
  job. Follow `.clang-format` and run the formatter before committing (see
  Checks above).
- The GitHub Actions of the PR, the **clang-format** job and the **build**
  job, have to pass, and you are expected to verify that yourself before
  pushing instead of waiting for CI to fail (see below).

### Verify the checks locally

Both CI jobs are plain Docker commands, so run them on the branch before you
ask for a review:

```sh
# 1. clang-format job: exits 0 and prints nothing when everything is formatted
git submodule update --init --recursive
docker run --rm -v ${PWD}:/src xianpengshen/clang-tools:22 \
  clang-format --dry-run --Werror \
  $(find src include -type f \( -name "*.c" -o -name "*.cpp" \
  -o -name "*.h" -o -name "*.hpp" \))

# 2. build job: same image and script the CI job uses (see Building)
docker build -t nussplibuilder .
docker run --rm -v ${PWD}:/project nussplibuilder python3 build.py
```

Both commands have to exit 0, and the build has to leave `out/Aroma-DEBUG`
and `out/Channel-DEBUG` behind, the two artifacts the PR workflow uploads.
Fix what they report (format with the `-i` variant above, then rebuild)
until both are clean. After the PR is up, still watch the Actions tab once -
pushing needs the maintainer's OK first.

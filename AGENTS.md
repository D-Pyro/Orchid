# Repository instructions

Applies to the main checkout, all its worktrees (also under `~/.t3/worktrees/` and
`~/Documents/Codex/`) and other checkouts of MythosOfs/Orchid, for every agent and subagent
(including automatic issue, review/merge and fork-scan runs). Machine addresses and the
maintainer's own rules live in the maintainer's private `~/.claude/CLAUDE.md`, not here.

This codebase is yours: what it's for and how it's built are your call, and you'll keep building on it.

## Commit and push

Standing authorization, unless the user says otherwise: commit and push requested changes
directly to `origin/main` without asking.

- Small, clearly scoped fixes (e.g. emulator UI, status bar): after appropriate checks.
- Larger changes: only after all required tests and checks passed. Failed, skipped or
  unavailable required checks do not count.
- Include only changes of the requested task. Fetch `origin/main` before pushing and integrate
  newer changes. Never force-push `main` or overwrite unrelated user changes. If branch
  protection or permissions block the push, report it; do not bypass.

## Local development builds

`./tools/build-local.sh` builds, `./build-local/bin/orchid` runs the GUI. Keep the build
directory between edits; do not clean or recreate it routinely.

- Configuration: Ninja, ccache, the available faster linker, `-O2 -g1`, no LTO. Builds the `yuzu`
  GUI target (output name `orchid`) and its dependencies, without tests, CLI or the standalone
  room server; emulation and graphics features stay enabled. Development configuration: use a
  separate release/PGO build for performance comparisons, full debug info for variable inspection.
- Needs the normal Linux build dependencies plus `ccache` and Ninja.
- Compiler cache `${XDG_CACHE_HOME:-$HOME/.cache}/orchid/ccache`, shared across worktrees. Use the
  script so its ccache environment and debug path mapping stay consistent. Do not clear the cache
  routinely.
- Parallelism is capped by CPU count and available RAM; override with
  `CMAKE_BUILD_PARALLEL_LEVEL=6 ./tools/build-local.sh`. Avoid several full builds at once on one
  host.
- Existing builds skip explicit reconfiguration (avoids timestamp-induced rebuilds); Ninja detects
  changed CMake inputs itself. Configure explicitly with `./tools/build-local.sh --configure-only`.
- Extra arguments are CMake configure options. `ORCHID_BUILD_DIR` sets another output directory.
  Never share one build directory between source worktrees.
- Share dependency downloads with `-DCPM_SOURCE_CACHE=/absolute/cache/path` (CMake keeps the choice
  in that build directory). On Leon's PC reuse `/home/leon/src/Eglantine/.cache/cpm`.
- First builds fill the compiler cache and take time. Do not claim a speedup without measuring.
  Verify the build and report limitations. Cached rebuild timings are not timings for changed
  source. Details: [docs/Build.md](docs/Build.md#faster-local-linux-test-builds), measured results
  and their limits: [docs/LocalBuildBenchmark.md](docs/LocalBuildBenchmark.md).
- These settings are for local iteration; preserve release/CI configurations.

GitHub Actions: preserve Release/PGO flags and packaging targets. The release workflow already
uses compiler caching and Ninja; normal Linux builds use mold, PGO builds keep LLD. Before claiming
a CI speedup, read per-job durations and cache statistics in the Actions summaries; local timings
say nothing about hosted runners. Do not cancel or restart an important running release to apply
build optimizations; prepare them for later runs.

## C++ navigation and refactoring (LSP, clangd, ast-grep)

The code is almost entirely C++. For symbol questions (definition, references, call hierarchy,
`workspaceSymbol`) use the `LSP` tool (clangd), not grep. It needs a compile database.

- Once per worktree, in its root: `orchid-lsp-setup` (`~/.local/bin`). It runs
  `tools/build-local.sh --configure-only` (no build), writes a local `.clangd` pointing at
  `build-local/` and excludes it via `.git/info/exclude`. Without it clangd finds no includes and
  `findReferences` sees one file only. The background index (about 3000 files, 30 MB in
  `build-local/.cache/clangd`) builds in a few minutes while clangd runs and stays on disk. After
  editing `.clangd`, kill clangd; the plugin restarts it.
- `findReferences` on widely used classes (e.g. `Scheduler`) returns hundreds of lines. Prefer
  `workspaceSymbol`, `incomingCalls`, `outgoingCalls`, or read only the files you need.
- LSP cannot rename, apply fix-its or debug. Use `clang-tidy --fix -p build-local`, `clang-format`
  and `ast-grep run -l cpp -p '<pattern>' [--rewrite '<new>'] <dir>` (structural search and
  multi-file rewrite). After a rewrite, build and run the tests.

## Mutation check (mandatory part of final verification)

When production code or unit tests change, run it yourself, unasked. Pick the Catch2 filter,
build directory and diff base yourself.

```sh
orchid-agent-test -- "<matching Catch2 filter>"
# Other build directories or already committed changes:
orchid-agent-test --build-dir build-local --base origin/main -- "<filter>"
# Test-only changes: name the matching production file:
orchid-agent-test --file src/common/example.h -- "<filter>"
```

- The runner builds and tests normally, then mutates a limited selection of production code with
  dumbmutate in a private source copy. Default: at most three changed lines, at least 80 % of
  valid mutants detected.
- `--base HEAD` covers unstaged, staged and new production files; for committed branch changes
  pass the matching base.
- Running `build/bin/tests` directly is fine for quick diagnosis but does not replace this run.
- Exit 1 = score below threshold; exit 2 = failed baseline or incomplete/inconclusive check.
  "No matching mutations" is not a pass.
- Surviving mutants: add a test or justify the behavioural equivalence concretely. Never lower
  the threshold to get green.
- Name partial checks (files, line budget) in the result; they do not guarantee freedom from bugs.
- Time limit per line 10 minutes; timeouts count as incomplete.
- Reports: `~/Orchid/orchid-issues/mutation/<run>/`. Install: `~/.local/share/orchid-mutation/`,
  engine `~/.local/share/orchid-dumbmutate/`. No git hooks or global compiler flags. Do not ship or
  start the private builds under `~/.cache/orchid-mutation/`.
- Do not aim ninja diagnostics (`-n`, `-t`) at a build directory while another ninja runs there;
  even diagnostics can corrupt ninja log files.

## Emulator tests: on the PC, run by the agent

Every test that starts the emulator (performance/fps, headless boots, repro runs, A/B
comparisons, screenshots, logs) runs on the PC (RX 9070 XT). The agent runs performance and sound
tests itself, even while the maintainer is at home. Start emulators with `agent-run` (the `orchid`
MCP does this already), keep runs short, end the emulator afterwards. The laptop runs no emulator
tests; it may only compile (see "Builds: PC or laptop"). The maintainer tests only the final build
before a release.

### Test runs go to a Terra subagent

Routine test runs (boot a game, drive the `orchid` MCP, `orchid-test-run`, screenshots, sound
recordings, collecting logs, CSV and `summary.txt`) go to a subagent on GPT-6.1 Terra
(`gpt-6.1-terra`), reasoning effort `medium`, started with `delegate_task` on the Codex provider.
It runs on the maintainer's ChatGPT subscription and spares the Claude limits. Not the default
`low`: clearly weaker at computer use.

- The main agent (Fable or Opus) writes the brief up front: game, scene, exact steps, what to
  record, return format (file paths, raw numbers, screenshot paths).
- The subagent returns data only. The main agent decides: regression or not, root cause, code
  changes, whether to ship. When a screenshot decides the outcome, look at it yourself.
- All rules in this file apply to the subagent run (`agent-run`, `orchid-test-run` for performance
  numbers, short runs, end the emulator afterwards).
- Basis (2026-11-10): 21 Orchid screenshots classified by scene type (gameplay, cutscene, menu,
  loading, title, black screen, error dialog): Terra `medium` 21/21, Haiku 5.5 21/21. Not covered:
  subtle rendering artifacts and small status-bar text; check those yourself until a harder test
  says otherwise.

### Harness facts every test brief relies on (`orchid` MCP)

- A route belongs to the snapshot (save state) it was recorded on. `route_list` shows it, `boot`
  takes it as `snapshot=...`, and `route_play` refuses a session booted from another one. From the
  default `initial` (a fresh save) the same inputs end in tutorials and intros.

  | Game | Snapshot | Route | Ends in |
  |---|---|---|---|
  | Mario Tennis Fever | `mtf-after-first-practice-20261107` | `mtf-mario-yoshi-stadium-match-v1` | Free Play match, several points played |
  | Donkey Kong Bananza | `bananza-leon-import-20261107` | `bananza-leon-tunnel-entry-v3` | mining tunnel, HUD checkpoint |
  | Yoshi and the Mysterious Book | `yoshi-chapter-one-saved-20261106-2114` | `yoshi-wildwoods-entry-v1` | Wildwoods level, Yoshi controllable |
  | Orbitals | `initial` | none: wait 60 s, then A, DDOWN x3, A, A, A, A (2 s apart), accept the EULA | main menu |

- `route_play` waits by itself until 35 s after boot before the first input (the routes were
  recorded with that lead-in). Routes are timed inputs; only Bananza ends with a checkpoint, so
  confirm where a run ended on the last screenshot.
- `boot` returns `out`, the run directory. Screenshots: `<out>/NNNN-<name>.png`. Frame times:
  `<out>/frames/*.csv` (column `frame_ms`, one row per guest frame). Log: what the `log` and
  `wait_log` tools read, on disk `<out>/profile/data/eden/log/orchid_<tag>_<title id>.txt`, after
  `stop` copied to `<out>/eden_log.txt`. The `eden_log.txt` inside `profile/` is an old log from
  the seed, not this run.
- Games run in a private throwaway profile without network. In-game dialogs (EULA, tutorials) are
  part of the game: accept and continue.
- Say in the brief: every reported screenshot must be opened and described, and an obstacle in
  one run is a result to write down, not a reason to skip the remaining runs. When a number or a
  log count decides the outcome, check it against the log yourself.
- The harness lives in `~/.local/share/codex-orchid-mcp/` (not in this repository). A running MCP
  server keeps the code it started with; changes reach a session only after its restart.
- Checkpoint reference images are in `state/checkpoints/`. Five older checkpoints lost theirs with
  deleted run directories and must be recorded again before use: `mtf-title-menu-v1`,
  `pokopia-cave-walking-v1`, `air-startup-loading-v1`, both `hw-*`.

### Gameplay gate: only before a release or a PR

Before starting a release build and before opening or pushing a pull request, boot every game
listed under "Project scope" in `README.md` once and play it briefly into real gameplay (not the
title or main menu). Plain commits and pushes to `main` need no gate, including the maintainer's
own commits.

- Per game: boot, reach gameplay (saved route from the table above, otherwise by hand), play about
  a minute, take screenshots, stop. Skip a game only if it is not on the PC or is known not to
  reach gameplay yet (README table); name every skipped game in the result.
- Pass: each game renders correctly (characters, world, HUD, text without missing glyphs), runs
  without hang, crash or device loss, and its log shows no new errors compared with the last good
  build. A game that worked in the last good build and fails now blocks the release or PR.
- The Terra subagent runs it and returns screenshots and log excerpts. The main agent looks at
  the key screenshots and decides pass or fail.
- Fail: no release or PR; find the regression, fix it, run the gate again.

### Performance measurements

- Every reported number comes from `orchid-test-run <label> <binary|AppImage> -g <game>`. It
  freezes `agent-work.slice` (parallel builds and emulators of other sessions) for the duration,
  thaws it afterwards, and records MangoHud CSV, per-thread CPU, log, settings and a verdict in
  `~/orchid-perf/<date>-<label>/summary.txt`. Compare runs with `orchid-perf-summary <dir>...`.
  Plain `agent-run` is not a valid measurement while other builds may run. The visible window on
  the desktop is expected.
- Measure in the real scene (an actual match or gameplay, not menus or videos) at 1x resolution
  scaling, and judge against the fps cap. Loop (baseline first, one change per run): skill
  `orchid-fps-bench`.
- fps can be bimodal from host scheduling (issue 128 swung between about 21 and 30 fps with
  identical code). So: at least 3 runs per variant, alternating A and B, always with a baseline of
  the previous good build. A change counts only if it clearly exceeds the spread between runs of
  the same variant. Check scheduler churn in `threads.log`/`meta.txt` before blaming the code.

### Sound checks (dropouts show up as gaps in the waveform)

`PULSE_SINK=orchid_test` keeps the speakers silent and records only the emulator:

```sh
M=$(pactl load-module module-null-sink sink_name=orchid_test)
parec -d orchid_test.monitor --file-format=wav rec.wav &   # start before the game, kill afterwards
# ... PULSE_SINK=orchid_test agent-run <binary> -g <game> ...
ffmpeg -hide_banner -i rec.wav -af silencedetect=n=-60dB:d=0.02 -f null - 2>&1 | grep silence_
pactl unload-module $M
```

- Silence of 20 ms or more in the middle of continuous music or effects is a dropout (underrun).
  Silence at the start, on loading screens and in real game pauses is normal.
- Always compare with a baseline recording of the last good build in the same scene, and check
  the emulator log for audio underruns.
- Baselines: `~/orchid-perf/audio-baseline/<game>-<scene>.wav`, one fixed scene per game. Create
  the baseline from the last known good build before judging a change, and keep it up to date.
- This finds gaps, not wrong pitch, wrong tempo or missing instruments; for those compare length
  and a spectrogram with the baseline. Verified with a test tone containing an 80 ms gap, not yet
  with Orchid itself (check that the audio backend honours `PULSE_SINK`).

### Builds: PC or laptop

If the laptop is idle, compile there to relieve the PC. Check:
`ssh <laptop> 'pgrep -x orchid || pgrep -x eden-cli || pgrep -x ninja'` (no output = idle; address
in the maintainer's `~/.claude/CLAUDE.md`). One full build per machine at a time; otherwise build
on the PC (`build-scope`/`agent-run`). Copy the finished binary or AppImage to the PC with `scp`
and check it with `ldd`.

## Test shortcuts for the maintainer (final build before a release)

Create the shortcut only for the final release build, not for intermediate builds. It is done
only when its exact `Exec` passes `--check`; the maintainer must never have to click twice.

1. File `~/.local/share/applications/orchid-test-<label>.desktop`:
   ```ini
   [Desktop Entry]
   Type=Application
   Name=Orchid <label> (Test)
   Exec=env EDEN_XYZ=1 ~/.local/bin/orchid-test-run <label> <absolute path to AppImage or build*/bin/orchid> %f
   Icon=orchid
   Terminal=false
   Categories=Game;Emulator;
   X-Orchid-Test=true
   ```
   Use an absolute home path in `Exec` if the desktop does not expand `~`. Set environment
   switches only via `env VAR=value` before `orchid-test-run`. Quote paths with spaces in `"..."`.
   Do not set `QT_QPA_PLATFORM` or `DISPLAY`; `orchid-test-run` does that.
2. Check; all three must be green:
   ```sh
   desktop-file-validate ~/.local/share/applications/orchid-test-<label>.desktop
   ex=$(sed -n 's/^Exec=//p' <file> | sed 's/ %f$//; s#orchid-test-run #orchid-test-run --check #'); eval "$ex"   # -> "check ok"
   ldd <binary> | grep 'not found'    # must be empty (not needed for AppImages)
   ```
3. `update-desktop-database ~/.local/share/applications`.

Why (2026-11-07): Hyprland 0.56 provides Xwayland only as `/tmp/.X11-unix/X0_`, but Qt-xcb looks
for `X0` and aborts after 0.5 s with exit 134, which looks as if the shortcut does not start.
`orchid-test-run` creates the link `X0 -> X0_` itself, checks the display with `xprop` and reports
start failures as a desktop notification. If a test still does not start: `coredumpctl list` and
`~/orchid-perf/<newest>/meta.txt` (exit code).

## Clean up worktrees and builds when a task is done

When an Orchid task is finished and its worktree/build is no longer needed, clean up yourself
without asking. Covers worktrees under `~/.t3/worktrees/Eglantine/`, `~/src/Eglantine-*`,
`~/src/Eglantine/.claude/worktrees/`, `~/Documents/Codex/*/work/`, their `build*/` directories,
test binaries under `~/Orchid/orchid-builds/` and private builds under `~/.cache/orchid-mutation/`.

- Remove only if all hold: `git status` shows no code changes (only logs/builds),
  `git rev-list HEAD --not --remotes` is empty, no process has its cwd/binary there, no
  `orchid-test-*.desktop` points to it, no running T3 thread works in it.
- Then `git -C ~/src/Eglantine worktree remove --force <path>`, delete the local branch (its tip
  is on the remote), `git worktree prune`.
- Worktrees with unsaved work stay; their old `build*/` directories may go (ccache rebuilds).
- Delete test binaries in `~/Orchid/orchid-builds/` together with the `orchid-test-*.desktop`
  shortcuts once the maintainer is done testing. Do not delete game data (`.nup`, dumps).
- Do not touch: the main checkout `~/src/Eglantine` and its `build/`, worktrees of other
  repositories (e.g. `~/Development/Eglantine-Official`, Djihads80), your own running worktree.

### Disk budget (clean up regularly, not only at task end)

Limit 100 GB for everything agents create: the worktrees above, their `build*/` directories,
`~/Orchid/orchid-builds/`, `~/.cache/orchid-mutation/`, the compiler cache `~/.cache/orchid/`, and
perf runs in `~/orchid-perf/` and `~/Orchid/orchid-perf/`. Not counted and never deleted for
space: game data (`.nup`, dumps), the main checkout, issue attachments in
`~/Orchid/orchid-issues/`. A source checkout is about 0.5 GB, a build directory several GB, so
only a handful of worktrees with builds fit: one worktree per active task, removed when it ends.

Check at the start and end of every Orchid task, and before creating a worktree:

```sh
du -sch ~/.t3/worktrees/Eglantine ~/src/Eglantine-* ~/Documents/Codex ~/Orchid/orchid-builds \
  ~/.cache/orchid-mutation ~/.cache/orchid ~/orchid-perf ~/Orchid/orchid-perf 2>/dev/null | tail -1
```

Above the limit, clean until below it, in this order, each only under the safety rules above:
1. private mutation builds in `~/.cache/orchid-mutation/` that no run is using (rebuilt on demand),
2. test binaries in `~/Orchid/orchid-builds/` that no `orchid-test-*.desktop` points to,
3. finished worktrees, oldest first,
4. `build*/` directories of worktrees untouched for 7 days (ccache rebuilds them),
5. perf runs older than 14 days, keeping `audio-baseline/` and the latest baseline run per game.

Never clear the compiler cache for space. If still above the limit after all safe steps, tell
the maintainer what blocks (e.g. worktrees with unpushed work); do not delete unsaved work.

Measured 2026-11-10, before this rule: about 120 GB (62 worktrees under
`~/.t3/worktrees/Eglantine` 39 GB, `~/Documents/Codex` 38 GB, `~/Orchid/orchid-builds` 20 GB,
`~/.cache/orchid-mutation` 12 GB, `~/src/Eglantine-*` about 7 GB, compiler cache 2 GB).

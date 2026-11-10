# Repository instructions

## Commit and push completed changes

For small, clearly scoped fixes requested by the user (for example, emulator UI or
status bar fixes), complete appropriate checks, then commit the changes and push
them directly to `origin/main` without asking for confirmation. This is the user's
standing authorization for these fixes, unless they explicitly request otherwise.

For larger changes requested by the user, also commit and push directly to
`origin/main` without asking for confirmation once all required tests and checks
have completed successfully. Failed, skipped, or unavailable required checks do
not satisfy this condition. This is the user's standing authorization for these
larger changes, unless they explicitly request otherwise.

Include only changes belonging to the requested task. Fetch `origin/main` before
pushing and integrate any newer changes as needed. Never force-push `main` or
overwrite unrelated user changes. If a push is blocked by branch protection or
permissions, report the blocker instead of bypassing it.

# Local development builds

For repeated local Linux emulator testing, use `./tools/build-local.sh` from this
checkout. Run the resulting GUI with `./build-local/bin/orchid`. Keep the build
directory between edits; do not clean or recreate it routinely.

The script uses Ninja, ccache, the available faster linker, `-O2 -g1`, and no LTO.
It builds the `yuzu` GUI target (output name `orchid`) and its dependencies, without
tests, CLI or the standalone room server. Normal emulation and graphics features
remain enabled. This is a development configuration: use a separate release/PGO
build for performance comparisons, or full debug information for variable inspection.

- Required tools: normal Linux build dependencies plus `ccache` and Ninja.
- Compiler cache: `${XDG_CACHE_HOME:-$HOME/.cache}/orchid/ccache`, shared across
  worktrees. Prefer the script so its ccache environment and debug path mapping
  remain consistent. Do not clear the cache routinely.
- Parallelism is capped by CPU count and available RAM. Override with
  `CMAKE_BUILD_PARALLEL_LEVEL=6 ./tools/build-local.sh` if necessary. Avoid running
  several full builds concurrently on the same host.
- Existing builds skip explicit reconfiguration to avoid timestamp-induced rebuilds.
  Ninja detects changed CMake inputs automatically. Configure explicitly with
  `./tools/build-local.sh --configure-only`, or pass extra CMake options.
- Extra arguments are CMake configure options. Use `ORCHID_BUILD_DIR` for a separate
  output directory. Do not reuse one build directory across different source worktrees.
- Dependency downloads can be shared via `-DCPM_SOURCE_CACHE=/absolute/cache/path`.
  CMake preserves this choice in that build directory. On Leon's PC an existing
  dependency cache is `/home/leon/src/Eglantine/.cache/cpm`; reuse it when available
  instead of downloading the same sources for every worktree.
- First builds populate the compiler cache and can still take time. Do not claim a
  measured speedup without measurement. Verify the build and report any limitations.

See [docs/Build.md](docs/Build.md#faster-local-linux-test-builds) for details. These
settings are for local iteration; preserve release/CI configurations.
Measured results and their limits are recorded in
[docs/LocalBuildBenchmark.md](docs/LocalBuildBenchmark.md). In particular, cached
rebuild timings must not be presented as timings for genuinely changed source.

For GitHub Actions, preserve Release/PGO flags and packaging targets. The release
workflow already uses compiler caching and Ninja; normal Linux builds use mold,
while PGO builds retain LLD. Consult per-job build durations and cache statistics
in Actions summaries before claiming a CI speedup. Local timings do not establish
hosted-runner performance. Do not cancel or restart an important running release
to apply build optimizations; prepare them for subsequent runs.

# Orchid agent workflow

Applies to the main checkout, all its worktrees (also under `~/.t3/worktrees/` and
`~/Documents/Codex/`) and other checkouts of MythosOfs/Orchid, for every agent and subagent
(including automatic issue, review/merge and fork-scan runs). Machine addresses and the
maintainer's own rules live in the maintainer's private `~/.claude/CLAUDE.md`, not here.

This codebase is yours: what it's for and how it's built are your call, and you'll keep building on it.

## Mutation check is part of the final verification

When production code or unit tests change, run the mutation check yourself as a mandatory part of
the final verification, without being reminded or asked. Pick the Catch2 filter, build directory
and diff base yourself; for test-only changes select the affected production file with `--file`.
Use the local runner:

```sh
orchid-agent-test -- "<matching Catch2 filter>"
# Other build directories or already committed changes:
orchid-agent-test --build-dir build-local --base origin/main -- "<filter>"
# Test-only changes: name the matching production file:
orchid-agent-test --file src/common/example.h -- "<filter>"
```

The runner first builds and tests normally, then mutates a limited selection of production code
with dumbmutate in a private source copy. Default: at most three changed lines, at least 80 % of
valid mutants detected. `--base HEAD` covers unstaged, staged and new production files; for
committed branch changes pass the matching base explicitly. Running `build/bin/tests` directly is
fine for quick diagnosis but does not replace this final run.

- Exit 1 = mutation score below threshold; exit 2 = failed baseline or incomplete/inconclusive check.
- "No matching mutations" is not a pass.
- Investigate surviving mutants: add a test or justify the behavioural equivalence concretely.
  Never lower the threshold just to get green.
- Name partial checks (files, line budget) in the result; they do not guarantee freedom from bugs.
- Reports: `~/Orchid/orchid-issues/mutation/<run>/`. Do not ship or start the private builds under
  `~/.cache/orchid-mutation/`. Time limit per line is 10 minutes; timeouts count as incomplete.
  Install: `~/.local/share/orchid-mutation/`, engine `~/.local/share/orchid-dumbmutate/`.
  No git hooks or global compiler flags.
- Do not aim ninja diagnostics (`-n`, `-t`) at a build directory in parallel with another ninja
  run; even diagnostics can corrupt ninja log files.

## All emulator tests run on the PC

Every test that starts the emulator (performance/fps, headless boots, repro runs, A/B
comparisons, screenshots, logs) runs on the PC (RX 9070 XT), not on the laptop, and the agent
runs performance and sound tests itself even while the maintainer is at home. Start emulators
with `agent-run` (the `orchid` MCP does this already), keep runs short, and end the emulator
afterwards. The laptop runs no emulator tests; it may only compile (see below). The maintainer
tests only the final build before a release.

### Test runs are done by a Terra subagent

Routine test runs (boot a game, drive the `orchid` MCP, `orchid-test-run`, screenshots, sound
recordings, collecting logs, CSV and `summary.txt`) go to a subagent on GPT-6.1 Terra
(`gpt-6.1-terra`) at reasoning effort `medium`, started with `delegate_task` on the Codex
provider. This runs on the maintainer's ChatGPT subscription and spares the Claude limits. Do
not use the default `low`; it is clearly weaker at computer use.

- The main agent (Fable or Opus) writes the brief up front: game, scene, exact steps, what to
  record, and the return format (file paths, raw numbers, screenshot paths).
- The subagent returns data only. Decisions stay with the main agent: regression or not, root
  cause, code changes, whether to ship. When a screenshot decides the outcome, look at it
  yourself.
- All rules in this file still apply to the subagent run (`agent-run`, `orchid-test-run` for
  performance numbers, short runs, end the emulator afterwards).
- Basis (2026-11-10): 21 Orchid screenshots classified by scene type (gameplay, cutscene, menu,
  loading, title, black screen, error dialog): Terra `medium` 21/21, Haiku 5.5 21/21. That test
  did not cover subtle rendering artifacts or reading small status-bar text; check those
  yourself until a harder test says otherwise.

### Mario Tennis Fever regression gate: play a Free Play match

The gate before pushing emulator changes (after tests pass, fork-scan ports, perf or graphics
changes) is a real match, not the main menu. Do not use the title/main-menu smoke
(`mtf-title-smoke-v1`) as the gate any more; it only proves the game boots.

- Start Mario Tennis Fever, go to Free Play, Singles, Quick Play (7 points), Stadium Court
  (Grass), Mario against a CPU opponent, and play several points (serves, rallies, point
  transitions). The saved route `mtf-mario-yoshi-stadium-match-v1` (`route_play` in the `orchid`
  MCP, 47 steps) does this; re-record it if the game binding no longer matches. About five
  minutes in total.
- Pass only if all of this holds: both players, court, crowd, effects and scoreboard render;
  text has no missing glyphs; the match runs through a point change without a hang, crash or
  device loss; the log shows no new errors compared with the last good build; and the native
  cadence is in the range of the last good run (`docs/performance/mtf-stadium-match-20261107.json`:
  mean about 16.7 ms, p99 about 22 ms). The opponent is random, so this is a regression check,
  not an A/B measurement; use `orchid-test-run` for numbers.
- The Terra subagent runs it (see above) and returns screenshots, log excerpts and cadence
  numbers. The main agent looks at the key screenshots and decides pass or fail.
- Fail means: do not push; find the regression, fix it, run the gate again.

### Performance measurements

- Use `orchid-test-run <label> <binary|AppImage> -g <game>` for every number you report. It freezes
  `agent-work.slice` (parallel builds and emulators of other sessions) for the duration and thaws
  it afterwards, and records MangoHud CSV, per-thread CPU, log, settings and a verdict in
  `~/orchid-perf/<date>-<label>/summary.txt`. Compare runs with `orchid-perf-summary <dir>...`.
  Plain `agent-run` is not a valid performance measurement while other builds may be running.
  `orchid-test-run` opens a visible window on the desktop; that is expected.
- Measure in the real scene (an actual match or gameplay, not menus or videos) at 1x resolution
  scaling, and judge against the fps cap. See the `orchid-fps-bench` skill for the
  baseline-first, one-change-per-run loop.
- fps can be bimodal because of host scheduling (issue 128 swung between about 21 and 30 fps with
  identical code). Therefore: at least 3 runs per variant, alternating A and B, always with a
  baseline of the previous good build. A change counts only if it clearly exceeds the spread
  between runs of the same variant. Check the scheduler churn in `threads.log`/`meta.txt` before
  blaming the code.

### Sound checks (dropouts show up as gaps in the waveform)

Run the game with `PULSE_SINK=orchid_test` so nothing comes out of the speakers and only the
emulator is recorded:

```sh
M=$(pactl load-module module-null-sink sink_name=orchid_test)
parec -d orchid_test.monitor --file-format=wav rec.wav &   # start before the game, kill afterwards
# ... PULSE_SINK=orchid_test agent-run <binary> -g <game> ...
ffmpeg -hide_banner -i rec.wav -af silencedetect=n=-60dB:d=0.02 -f null - 2>&1 | grep silence_
pactl unload-module $M
```

- Silence of 20 ms or more in the middle of continuous music or effects is a dropout (underrun).
  Silence at the start, on loading screens and in real game pauses is normal.
- Always compare with a baseline recording of the last good build in the same scene, and also
  check the emulator log for audio underruns.
- Baselines: `~/orchid-perf/audio-baseline/<game>-<scene>.wav`, one fixed scene per game. Create
  the baseline from the last known good build before judging a change, and keep it up to date.
- This finds gaps, not wrong pitch, wrong tempo or missing instruments; for those compare the
  length and a spectrogram with the baseline. The method was verified with a test tone containing
  an 80 ms gap, not yet with Orchid itself (check that the audio backend honours `PULSE_SINK`).

### Builds: PC or laptop

If the laptop is idle (no `orchid`/`eden-cli`, no other build), compiling there relieves the PC.
Check it with `ssh <laptop> 'pgrep -x orchid || pgrep -x eden-cli || pgrep -x ninja'` (no output =
idle); the address is in the maintainer's `~/.claude/CLAUDE.md`. Only one full build per machine at
a time; otherwise build on the PC (`build-scope`/`agent-run`). Copy the finished binary or
AppImage to the PC with `scp` and check it with `ldd`.

## Test shortcuts for the maintainer (final build before a release)

The maintainer tests only the final release build. Create the shortcut then, not for every
intermediate build. A shortcut is only done when its exact `Exec` passes `--check`; the
maintainer must never have to click twice.

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
   Use an absolute home path in `Exec` if your desktop does not expand `~`. Set environment switches
   only via `env VAR=value` before `orchid-test-run`. Quote paths with spaces in `"..."`. Do not
   set `QT_QPA_PLATFORM` or `DISPLAY`; `orchid-test-run` does that.
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
start failures as a desktop notification. If a test still does not start: look at
`coredumpctl list` and `~/orchid-perf/<newest>/meta.txt` (exit code).

## Clean up worktrees and builds when a task is done

When an Orchid task is finished and its worktree/build is no longer needed, the agent cleans up
itself without asking. This covers worktrees under `~/.t3/worktrees/Eglantine/`, `~/src/Eglantine-*`,
`~/src/Eglantine/.claude/worktrees/`, `~/Documents/Codex/*/work/`, their `build*/` directories, test
binaries under `~/Orchid/orchid-builds/` and private builds under `~/.cache/orchid-mutation/`.

- Remove only if everything is safe: `git status` shows no code changes (only logs/builds),
  `git rev-list HEAD --not --remotes` is empty, no process has its cwd/binary there, no
  `orchid-test-*.desktop` points to it, and no running T3 thread works in it.
- Then `git -C ~/src/Eglantine worktree remove --force <path>`, delete the local branch (its tip is
  on the remote), `git worktree prune`.
- Worktrees with unsaved work stay; their old `build*/` directories may go (ccache rebuilds).
- Delete test binaries in `~/Orchid/orchid-builds/` once the maintainer is done testing (together
  with the `orchid-test-*.desktop` shortcuts). Do not delete game data (`.nup`, dumps).
- Do not touch: the main checkout `~/src/Eglantine` and its `build/`, worktrees of other
  repositories (e.g. `~/Development/Eglantine-Official`, Djihads80), your own running worktree.

### Disk budget (clean up regularly, not only at task end)

Soft limit 60 GB, hard limit 100 GB for everything agents create: worktrees (all three
locations above), their `build*/` directories, `~/Orchid/orchid-builds/`,
`~/.cache/orchid-mutation/`, the compiler cache `~/.cache/orchid/`, and perf runs in
`~/orchid-perf/` and `~/Orchid/orchid-perf/`. Not counted and never deleted for space: game data
(`.nup`, dumps), the main checkout, and issue attachments in `~/Orchid/orchid-issues/`. A source
checkout is about 0.5 GB, a build directory several GB, so at most a handful of worktrees with
builds fit; keep one worktree per active task and remove it when the task ends.

Check at the start and at the end of every Orchid task, and before creating a worktree:

```sh
du -sch ~/.t3/worktrees/Eglantine ~/src/Eglantine-* ~/Documents/Codex ~/Orchid/orchid-builds \
  ~/.cache/orchid-mutation ~/.cache/orchid ~/orchid-perf ~/Orchid/orchid-perf 2>/dev/null | tail -1
```

Above the soft limit, clean until below it, in this order, each only under the safety rules above:
1. private mutation builds in `~/.cache/orchid-mutation/` that no run is using (rebuilt on demand),
2. test binaries in `~/Orchid/orchid-builds/` that no `orchid-test-*.desktop` points to,
3. finished worktrees, oldest first,
4. `build*/` directories of worktrees untouched for 7 days (ccache rebuilds them),
5. perf runs older than 14 days, keeping `audio-baseline/` and the latest baseline run per game.

Never clear the compiler cache for space. If still above the hard limit after all safe steps, tell
the maintainer what blocks (for example worktrees with unpushed work); do not delete unsaved work.

Measured 2026-11-10 (before this rule): about 120 GB in these locations (62 worktrees under
`~/.t3/worktrees/Eglantine` 39 GB, `~/Documents/Codex` 38 GB, `~/Orchid/orchid-builds` 20 GB,
`~/.cache/orchid-mutation` 12 GB, `~/src/Eglantine-*` about 7 GB, compiler cache 2 GB). The first
cleanup pass therefore removes finished worktrees and stale builds.

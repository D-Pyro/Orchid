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
  | Mario Tennis Fever | `mtf-after-first-practice-20261107` | `mtf-stadium-match-v3` | Free Play match (waits for the player HUD) |
  | Donkey Kong Bananza | `bananza-leon-import-20261107` | `bananza-leon-tunnel-entry-v4` | mining tunnel, HUD checkpoint |
  | Yoshi and the Mysterious Book | `yoshi-chapter-one-saved-20261106-2114` | `yoshi-wildwoods-entry-v2` | Wildwoods level, Yoshi controllable |
  | Super Mario Bros. Wonder | `wonder-goal60-overworld-20261107` | `wonder-level-entry-v2` | inside a level, HUD checkpoint |
  | Pokémon Pokopia | `pokopia-intro-language-set-20261107` | `pokopia-cave-v3` | first cave, character controllable |
  | Hyrule Warriors: Age of Imprisonment | `hw-blur-mission-20261106` (2x; the gate overrides to 1x) | `hw-mission-v2` (long: `route_start`) | mission, objective text checkpoint |
  | Star Fox | `starfox-tutorial-controls-loop-20261107` | `starfox-flight-v3` | flight tutorial, Arwing controllable |
  | Kirby and the Forgotten Land | `kirby-leon-dlc-20261110` | `kirby-stage-v2` | stage "Meteor Shower's Arrival" (no HUD at full health: checkpoint on the mission screen before, confirm on the screenshot) |
  | Orbitals | `initial`, boot with `settings={"player_1_connected":"true"}` (co-op: needs a second controller) | `orbitals-gameplay-v2` | ship interior, both characters controllable |
  | Xenoblade Chronicles: Definitive Edition | `xbde-leon-20261112` (no save: New Game) | `xbde-battle-v2` | prologue battle |
  | Xenoblade Chronicles 2 | `xb2-leon-20261112` (no save: New Game) | `xb2-field-v2` | first battle, Rex controllable |
  | Xenoblade Chronicles X: Definitive Edition | `xbx-leon-20261112` (the maintainer's save) | `xbx-field-v2-a` | field, minimap HUD |

  All twelve are registered for the gate (`gate_list`). Kirby Air Riders has no route on purpose:
  its races can hang the GPU (README: do not play).

- Routes should wait for screens, not for seconds: `wait_checkpoint(name, buttons=..)` waits until
  a recorded screen region appears (optionally pressing a button meanwhile) and is saved into the
  route, so a replay survives longer loading. `route_list` shows `screen_waits` per route; a route
  with 0 is timed inputs only (`route_play` then keeps the 35 s lead-in it was recorded with) and
  where it ended must be confirmed on the last screenshot. A failed checkpoint says how close the
  screen came ("closest rmse 0.090, limit 0.08"): close = limit too strict or another build's
  picture, far = wrong scene.
- Checkpoints: region on HUD that only exists in that scene, at most 120000 px (then the picture
  may sit up to 8 px elsewhere, which it does between builds), `max_rmse` 0.12 for opaque HUD and
  0.2 for text on a translucent bar over the 3D scene, recorded on the current build and checked
  twice. HUD that hides at times (between tennis points) needs a wait, not one look.
  `checkpoint_list` shows which are usable.
- Recording a route: explore first, then a clean boot with only the needed steps, then
  `route_save` (it keeps the time between your steps as waits, so do nothing extra in that boot),
  then replay from a fresh boot. If the run that found the way is already stopped,
  `route_from_run(name, run_id, final_checkpoint=..)` builds the route from its input log. Routes
  longer than 240 s replay with `route_start` (a job). Brief for a subagent: the template
  `~/.local/share/codex-orchid-mcp/briefs/record-route.md` plus the game's details, one game per
  brief; several games in one brief ended early every time (2026-11-11).
- Every `boot` should pass `bin=<build under test>`; without it the harness's stored default build
  runs and the reply says so. `default_binary_set` replaces the default (the last good build).
  `queue_s=240` waits for the GPU test lock instead of failing while another agent tests (call
  again if it is still held); the
  error names the holder. A session unused for 15 minutes is stopped to free the lock.
- More `boot` options: `settings={"use_docked_mode":"1"}` overrides `qt-config.ini` keys for one
  run (no extra snapshot needed); `audio=True` records the sound (see "Sound checks").
  `boot_ui` opens the frontend without a game; drive it with `click`, `hotkey`, `type_text`.
  `seed_import` makes a snapshot from the maintainer's saves (and settings) or raw save files.
- Judging a run: `health` (ok / stalled / device_lost / gpu_reset / exited; "alive" alone does not
  tell a hang from a running game), `summary` (frame times, log errors, and which error groups are
  new against the baseline of that game and route), `clip` (a few seconds as one sheet of frames,
  for flicker and animation). `stop` returns the summary, the audio report and, after a crash,
  a backtrace from the core dump. `baseline_save` stores a run you checked as "last good".
- Long runs are jobs, polled with `job_status(wait_s=240)`: `soak_start` (minutes under watch,
  ends at a hang with thread backtraces), `gate_start`, `desktop_run_start`, `benchmark_start`.
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
- The harness lives in `~/.local/share/codex-orchid-mcp/` (its own local git repository, not this
  one). A running MCP server keeps the code it started with; changes reach a session only after
  its restart. After changing it: `uv run --script selftest.py` (no emulator needed), then
  `uv run --script livetest.py <binary> core` (real boot; more tests in its header).
- Its `state/` cleans itself at every boot (`cleanup`): profile clones of runs after 2 days, whole
  runs and bundles after 14 days. Screenshots, logs and frame times of a run stay until then.

### Gameplay gate: only before a release or a PR

Before starting a release build and before opening or pushing a pull request, boot every game
listed under "Project scope" in `README.md` once and play it briefly into real gameplay (not the
title or main menu). Plain commits and pushes to `main` need no gate, including the maintainer's
own commits.

- Run it with `gate_start(bin=<build>)`: every game registered with `gate_set` (see `gate_list`)
  is booted, played along its route into gameplay and on for about a minute, and stopped; the
  result gives pass / review / fail per game with reasons and one contact sheet of all last
  frames. A scope game without a gate entry is played by hand as before. Skip a game only if it
  is not on the PC or is known not to reach gameplay yet (README table); name every skipped game.
- The harness cannot judge rendering: open the contact sheet (and single screenshots where it is
  too small). After a gate you accept, `gate_accept` stores its runs as the new baselines.
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
- Unattended: `desktop_run_start(label, bin, game, snapshot, route)` of the `orchid` MCP runs
  `orchid-test-run` with a clone of the snapshot and replays the route's button presses by time
  (`EDEN_AUTO_BUTTONS`; play the route once with `route_play` before, that records the times). It
  returns `summary.txt` and a screenshot of the window; where the run ended is known only from
  that screenshot. An AppImage works as `bin`, so this also covers the release build in a real
  window. The frame times of a normal `boot` are guest work on a software-presented private
  display: good for comparing two runs there, not a number to report.
- Measure in the real scene (an actual match or gameplay, not menus or videos) at 1x resolution
  scaling, and judge against the fps cap. Loop (baseline first, one change per run): skill
  `orchid-fps-bench`.
- fps can be bimodal from host scheduling (issue 128 swung between about 21 and 30 fps with
  identical code). So: at least 3 runs per variant, alternating A and B, always with a baseline of
  the previous good build. A change counts only if it clearly exceeds the spread between runs of
  the same variant. Check scheduler churn in `threads.log`/`meta.txt` before blaming the code.

### Sound checks (dropouts show up as gaps in the waveform)

`boot(audio=True)` of the `orchid` MCP unmutes the game into a private null sink, keeps the
speakers silent and records `<out>/audio.wav`; `stop` reports its silences (`middle_silences` =
between start and end). `readiness.audio.streams` in the boot reply must be 1 or more; 0 means
the sound is on the speakers instead: stop at once. By hand the same looks like this:

```sh
M=$(pactl load-module module-null-sink sink_name=orchid_test)
parec -d orchid_test.monitor --file-format=wav rec.wav &   # start before the game, kill afterwards
# ... PIPEWIRE_NODE=orchid_test PULSE_SINK=orchid_test agent-run <binary> -g <game> ...
ffmpeg -hide_banner -i rec.wav -af silencedetect=n=-60dB:d=0.02 -f null - 2>&1 | grep silence_
pactl unload-module $M
```

Orchid's SDL talks to PipeWire directly and ignores `PULSE_SINK` (measured 2026-11-11: the
recording stayed silent and the game played on the speakers); `PIPEWIRE_NODE` is what routes it.

- Silence of 20 ms or more in the middle of continuous music or effects is a dropout (underrun).
  Silence at the start, on loading screens and in real game pauses is normal.
- Always compare with a baseline recording of the last good build in the same scene, and check
  the emulator log for audio underruns.
- Baselines: `~/orchid-perf/audio-baseline/<game>-<scene>.wav`, one fixed scene per game. Create
  the baseline from the last known good build before judging a change, and keep it up to date.
- This finds gaps, not wrong pitch, wrong tempo or missing instruments; for those compare length
  and a spectrogram with the baseline. Verified with a test tone containing an 80 ms gap and
  with a Mario Tennis Fever boot (recording not silent, peak -31.7 dB).

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
`~/Orchid/orchid-builds/`, `~/.cache/orchid-mutation/`, the compiler cache `~/.cache/orchid/`,
perf runs in `~/orchid-perf/` and `~/Orchid/orchid-perf/`, and the harness state
`~/.local/share/codex-orchid-mcp/state/` (it cleans itself, see "Harness facts"). Not counted and never deleted for
space: game data (`.nup`, dumps), the main checkout, issue attachments in
`~/Orchid/orchid-issues/`. A source checkout is about 0.5 GB, a build directory several GB, so
only a handful of worktrees with builds fit: one worktree per active task, removed when it ends.

Check at the start and end of every Orchid task, and before creating a worktree:

```sh
du -sch ~/.t3/worktrees/Eglantine ~/src/Eglantine-* ~/Documents/Codex ~/Orchid/orchid-builds \
  ~/.cache/orchid-mutation ~/.cache/orchid ~/orchid-perf ~/Orchid/orchid-perf \
  ~/.local/share/codex-orchid-mcp/state 2>/dev/null | tail -1
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

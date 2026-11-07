# Target-game performance campaign

Started 2026-11-07. Scope is README.md “Project scope”; existing README FPS
claims are historical context, not new measurements. Target: at least 60 native
gameplay FPS with correct graphics and game speed. Frame generation does not
satisfy this target. A guest 30 FPS cap must be distinguished from host slowdown;
unlocking it requires timing and physics validation.

## Acceptance and evidence

Use one GPU workload at a time. Build this checkout with tools/build-local.sh.
Record source SHA, binary SHA256, game identity/update, host CPU/GPU/driver,
immutable save/config/cache seed, resolution, handheld/docked, accuracy, VSync,
speed limit and frame generation. Use the same labelled gameplay checkpoint,
input sequence and measurement window for every comparison.

For each game: inspect screenshots before marking gameplay; warm caches in each
arm; collect at least three baseline runs and three candidate runs, alternating
order. Report median/min/max/spread, native present intervals, p50/p95/p99,
counts above 16.67/33.33/50/100 ms, shader compilation and host CPU/GPU load.
System frame-work CSV inverse means are not display FPS. Keep diagnostic perf,
RenderDoc and verbose traces separate from timed runs. Record sample failures.
Profile the heaviest reproducible scene, rank measured costs, change one cause,
and repeat. Promote only gains beyond run spread that repeat and pass graphics,
regression tests and Tennis menu smoke checks. Do not turn missing measurements
into zeros or use menu frame rates as gameplay results.

If blocked, retain evidence, reason and concrete next action here and move to
the next game. No title is verified at 60 FPS by this initial campaign entry.

## Queue and backlog

| Game | Current campaign state | Next action / blocker |
| --- | --- | --- |
| Hyrule Warriors: Age of Imprisonment | Measured stationary gameplay at 2x; graphics gate blocked by native shader rejection | Backlog: reproduce rejected fragment shader at instruction 0xf0, validate full shader and scene, then independent baseline boots |
| Other Hyrule Warriors title | Input blocked: no second Hyrule Warriors package found in /home/leon/Switch2 | Identify intended title and locate legal installed game data; continue with available titles |
| Mario Tennis Fever | Queued; existing packaged-title smoke route available | Validate match route on current build, baseline match and menu smoke |
| Pokémon Pokopia | Ersteinrichtung/intro reached; no gameplay measurement | Resume pokopia-intro-language-set-20261107; reach gameplay and inspect sky/geometry |
| Star Fox | Queued; legacy route available | Reproduce crash separately from timing; preserve bundle and move on if blocked |
| Super Mario Bros. Wonder | Queued; overworld filesystem seeds available | Establish level and overworld routes; distinguish guest frame cap from slow emulation |
| Yoshi and the Mysterious Book | Queued; local package and historical stage seeds available | Verify package/demo identity before reusing saves; validate level one and level two rendering |
| Donkey Kong Bananza | Queued; legacy route available | Validate save/route and select heavy area; distinguish native cap and host limits |
| Kirby Air Riders | Queued; legacy race route available | Recheck reported race-load crash, capture evidence if blocked |
| Kirby and the Forgotten Land | Identification pending: KirbySwitch2.nup exists but filename is insufficient | Verify title identity and save compatibility before route recording |

## Initial diagnostic evidence (not a performance result)

Source b2f19333a6, build-local/bin/orchid, build succeeded via prescribed helper
with shared CPM cache. Build log: /tmp/orchid-goal-build.log.
Binary SHA256: 79edc189ab3474cdc50fb415c483958bceb87461a52c0731f25865ecfd98631c.
Host CPU: Ryzen 7 5800X3D; RADV reported by GUI. Full GPU/driver manifest pending.

Hyrule Warriors run: 20261107-162249-e81608bd under
/home/leon/.local/share/codex-orchid-mcp/state/runs/.
Seed: hw-blur-mission-20261106; game version 1.0.3 visible on title.
Settings inherited from seed: 2x resolution, handheld, FSR1, SMAA, speed 100%,
frame generation disabled. These are diagnostic settings, not a 1x baseline.

CPU perf captured 5,180 samples, no lost samples reported, 46,353,124 data bytes.
Artifact directory: perf-1794065006779137249-e29193 inside run. Collection covered
startup/title, not a controlled gameplay window. JIT symbols are unresolved and
addr2line emitted warnings. Individual GPU split wait/back-loop samples are leads,
not established gameplay bottlenecks. Do not optimize from this trace alone.

Harness compatibility backlog: current build writes profile/data/eden/log/orchid_log.txt,
while MCP log/error tools read eden_log.txt. Read the actual log for errors;
empty MCP classification is not a correctness pass. A preliminary test of the
installed older binary (84e5a38a14) is excluded from current-source results.
An exploratory Fast Fusion route was stopped after scope discovery and is excluded.

Mission gameplay reached and movement verified by consecutive screenshots; GUI
momentary readings around 41–43 FPS at 2x are observations, not a measured batch.
Input route: hw-mission-workshop-entry-inputs-v1 (11 steps, current package).
Checkpoint hw-mission-workshop-entry-v1 was recorded, but its immediate
comparison failed; route with required final checkpoint was NOT saved. Repair
the ROI/reference and validate replay before using this route in benchmarks.
Gameplay CPU diagnostic: perf-1794065287719052151-125abc in the same run.

Gameplay profile captured 4,972 samples (147,606,308 bytes); timing is excluded.
Top self-sample entries (diagnostic leads, not causal attribution):

```text
5.09% GPU orchid [.] VideoCommon::GpuSplit::WaitForWriters() - -
4.83% GPU libc.so.6 [.] __sched_yield - -
0.93% GPUFencingThrea libc.so.6 [.] 0x00000000001b1b33 - -
0.86% GPUBack orchid [.] Tegra::MemoryManager::GetSubmappedRangeImpl<true>(unsigned long, unsigned long, boost::container::small_vector<std::pair<std::conditional<true, unsigned long, unsigned long>::type, unsigned long>, 32ul, void, void>&
0.70% CPUCore_0 [JIT] tid 1759683 [.] 0x00007f4383fd0fca - -
0.70% GPUBack libc.so.6 [.] 0x00000000001b1bb3 - -
0.60% GPUBack orchid [.] VideoCommon::GpuSplit::BackLoop(std::stop_token) - -
0.58% GPUBack libc.so.6 [.] 0x00000000001b1b33 - -
0.58% GPUFencingThrea libc.so.6 [.] 0x00000000001b1b29 - -
0.54% GPUFencingThrea libc.so.6 [.] 0x00000000001b1b4a - -
```

Stop returned code 0 without forced termination, but MCP finalized=false and
qt_teardown_seen=false. The log-name mismatch may affect teardown detection;
this run remains ineligible for benchmark acceptance regardless.

## 2026-11-07: Hyrule stationary diagnostic measurement

Run 20261107-163003-2588fe32; same binary/seed and 2x settings as above.
Moved Zelda toward workshop, then left controller idle. Screenshots 0016, 0019
and 0020 show the same playable scene with characters, environment and objective.
This is a light stationary scene, not a heavy combat benchmark.

Three consecutive 30 s windows within ONE boot (not three independent runs):

| Window | Samples | Work mean ms | Work p50 ms | Work p95 ms | Work p99 ms | >33.33 ms | >50 ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 1257 | 23.847 | 22.498 | 32.886 | 40.973 | 52 | 0 |
| 2 | 1249 | 23.989 | 22.518 | 34.256 | 41.231 | 73 | 1 |
| 3 | 1256 | 23.862 | 22.469 | 33.529 | 41.452 | 66 | 0 |

These are system frame WORK durations, not GPU execution or display intervals.
The separate native service log advances by 600 presents every 14.30–14.40 s
in the final stationary scene (about 41.65–41.96 presents/s). No physical display
FPS claim is made. This does not meet 60 native frames/s. Raw window boundaries,
metrics and stutter counts: stationary-report.json inside the run directory.

Corrected log diagnosis: startup orchid_log.txt is also insufficient! SetGame
in src/common/logging.cpp switches to orchid_HWAOI_04003F40232DE000.txt. That
file contains both the Qt cleanup marker and two shader error records at 285.135 s.
The process returned 0 without force; manual inspection proves clean teardown,
while MCP's finalized=false is a false negative caused by following the wrong log.
The local eden_log.txt -> orchid_log.txt alias alone did not solve this.

Graphics gate: FAILED. Program 0x234f98dd00, stage 4, instruction 0xf0:
“native instruction before exit setup state is dead”, op words
80b 1800780b 40000000 03f1e200 001fc800. Screenshot inspection does not explain
the rejected pass. Do not accept a performance optimization until this is traced.
Next: capture complete failing program and determine whether the stage rejection
affects this scene. Continue other games in the meantime.

Checkpoint hw-workshop-objective-v2 uses the objective text ROI and passes
both immediate comparison (RMSE 0.01605) and end comparison (0.00608), threshold
0.02. Old checkpoint remains invalid. Input replay still needs explicit startup
waits and cutscene-state checks; its elapsed gaps cannot be inferred from tool
wait_s alone. No candidate code or setting was promoted.

## Pokopia setup progress

Current-source run 20261107-163851-fda849c8 reached language selection and
Ditto intro. This is not gameplay and no FPS result is accepted. Existing
legacy data-2 save contains only SYDAT.BIN (~2 KB), so its mere presence
does not establish an available progressed gameplay save.

Setup input route: pokopia-first-setup-v1. Stopped cleanly with finalized=true
and qt_teardown_seen=true after pointing this run's empty eden_log.txt at
orchid_PP_0400A3A01EE4C000.txt. This verifies the per-game log correction
without changing the emulator or shared configuration.
Filesystem seed pokopia-intro-language-set-20261107 saved successfully, ID
8f0bacec6934872de4c69532724938d861ae6b99fce850fe63f2fb36df188b65.
The seed records language/setup progress; it is not a RAM save-state and
must be checked on reboot. Next: advance intro and save gameplay entry.

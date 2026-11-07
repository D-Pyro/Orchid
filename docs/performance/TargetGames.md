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
| Pokémon Pokopia | First cave gameplay measured; broader validation pending | Verify saved-state reload, then measure outside/heavy scenes and investigate startup service assertion |
| Star Fox | Queued; legacy route available | Reproduce crash separately from timing; preserve bundle and move on if blocked |
| Super Mario Bros. Wonder | Queued; overworld filesystem seeds available | Establish level and overworld routes; distinguish guest frame cap from slow emulation |
| Yoshi and the Mysterious Book | Queued; local package and historical stage seeds available | Verify package/demo identity before reusing saves; validate level one and level two rendering |
| Donkey Kong Bananza | Ingot Isle gameplay measured; pacing experiment in progress | Compare stable-swap diagnostic with default, then independent repeated runs and speed/graphics gates |
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

## 2026-11-07: Pokopia first-cave diagnostic

Run 20261107-164226-b69a3144, source/binary unchanged, 1x handheld. The prior
intro-language seed returns to language selection; it did NOT prove persistent
setup progress. Advanced setup and verified movement in the first cave.
Checkpoint pokopia-cave-walking-v1 passed (RMSE 0.01812). Single 60 s stationary
window before profiling: 3,599 system-frame work samples, mean 16.666 ms, p95
18.532 ms, p99 19.803 ms, maximum 25.667 ms, none over 33.33 ms. These work
statistics are not physical display intervals. Native 600-present log windows
advance in roughly 10 s, consistent with 60/s. This light wall-facing scene is
not representative of the outside world and does not establish full-game success.

Later diagnostic host sampling: about 3.04 CPU core equivalents, RSS 11.23 GB,
device-wide GPU busy 29–31%, VRAM 3.35 GB. Sampling was under CPU profiling;
it does not establish unprofiled utilization or exclusive per-process GPU cost.
Perf recorded data but default report/script generation timed out. A bounded
flat report recovered successfully with no lost samples reported; largest
individual self-sample entries include GPU split back loop (2.55%), sched_yield
(2.05%), split Drain (1.68%), WaitForWriters (1.31%). These are diagnostic CPU
cost leads, not a critical-path proof. Raw artifact: perf-flat-recovered.txt in run.

Errors retained: startup IUserShimScopedObject command 1 assertion, and unmapped
48-byte read near teardown. Neither is silently treated as a graphics pass.
No optimization promoted. Raw results: pokopia-cave-20261107.json.
Input route pokopia-first-cave-exploration-v1 is recorded, not replay-validated.
Seed pokopia-first-cave-tested-20261107 saved after clean stop; only SYDAT.BIN
was visible before stopping, so persistent gameplay progress still needs reboot
verification. A filesystem seed does not resume RAM at the measured scene.

## 2026-11-07: Bananza Ingot Isle baseline diagnostic

Historical dk-leon save copied into stopped isolated run only, original preserved.
Immutable seed bananza-leon-import-20261107:
56e57703f9e5b9669e5622032646f8151c0decdb9fde6eb8cbe1d17617a4c1cd.
Bananza version 3.2.0, DLC 1.0.0, 1x handheld; RX 9070 XT / RADV 26.99.99.
Run 20261107-165157-c3f26d25 uses EDEN_NX2_HITCH_TRACE=1.
Continued File 1, Ingot Isle, moved forward 300 ms; tunnel scene, fur, rock
surfaces, lighting and HUD visible. Checkpoint bananza-ingot-tunnel-v1 passes
(RMSE 0.00891). Route bananza-leon-tunnel-entry-v1 recorded with final checkpoint.

Single 60 s diagnostic window: 2,931 complete native service intervals; mean
20.465 ms, p50 16.705 ms, p95 33.402 ms, p99 33.537 ms, maximum 39.370 ms.
Native service cadence estimate 48.86/s; no physical display FPS claim.
Mean phase costs: external 1.197 ms, fence 3.167 ms, composite 0.003 ms, pace
16.095 ms. Phase statistics exclude zero entries and are not a GPU execution
profile. Raw result: bananza-tunnel-baseline-20261107.json.

Log shows frequent 1/2 swap-interval transitions. Existing EDEN_NX2_SWAP_STABLE
option is the next isolated hypothesis: require 120 unchanged presents before
honouring interval >1. This preserves sustained low-rate requests while testing
whether transient requests produce avoidable pacing. Must validate game speed,
visuals, independent repeated A/B and no-trace runs before any default change.
Startup IUserShimScopedObject assertion is present here too; no shader rejection
was seen in the inspected initial gameplay log. Full gate remains pending.

### Bananza first candidate (not promoted)

Run 20261107-165701-dc3bbdf2 replays the same recorded entry route against the
same immutable seed and binary; only EDEN_NX2_SWAP_STABLE=120 differs. Hitch
trace remains enabled in both. Single 60 s stationary gameplay window: 3,594
complete native intervals; mean 16.692 ms, p50 16.659 ms, p95 18.235 ms, p99
20.309 ms, maximum 30.254 ms; no interval over 33.33 ms. Native cadence estimate
59.91/s. Mean phase times (nonzero): external 2.489 ms, fence 3.168 ms, composite
0.003 ms, pacing 11.790 ms, service 14.201 ms. The large observed change is
consistent with transient two-vblank pacing being responsible for lost throughput.
It is not a proven production gain or full-game 60 FPS result.

Automatic final checkpoint failed at RMSE 0.0376142 > 0.02 and route aborted.
The reference ROI includes animated illuminated rock behind the objective text.
Full screenshot still shows the expected tunnel, DK, fur, lighting, HUD and punch
tutorial, but a single visual inspection does not replace the failed route guard
or independent correctness tests. Raw results: bananza-swapstable-candidate-20261107.json.

Next: select a tighter/stable scene guard based on inspected reference images,
validate replay on both arms, collect >=3 alternating independent runs per arm
and a repeat batch. Validate movement/animation/cutscene duration against wall
time, audio sync, sustained native 30 requests and both handheld/docked scenes.
Then repeat without Hitch trace. Check default-profile regression and Tennis menu
before any production setting/code change. Existing global default remains unchanged.

| Variant | Runs | Native cadence estimate | Gate | Verdict |
| --- | ---: | ---: | --- | --- |
| default swap handling, Hitch trace | 1 | 48.86/s | startup unimplemented service assertion; independent correctness pending | diagnostic baseline |
| swap-stable 120, Hitch trace | 1 | 59.91/s | final automated image mismatch | rejected for promotion; repeat candidate |

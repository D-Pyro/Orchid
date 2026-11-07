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
| Hyrule Warriors: Age of Imprisonment | Current-source build reaches controllable workshop mission; gameplay baseline pending | Repair visual checkpoint, replay recorded inputs; collect three unprofiled runs and compare 1x/2x separately |
| Other Hyrule Warriors title | Input blocked: no second Hyrule Warriors package found in /home/leon/Switch2 | Identify intended title and locate legal installed game data; continue with available titles |
| Mario Tennis Fever | Queued; existing packaged-title smoke route available | Validate match route on current build, baseline match and menu smoke |
| Pokémon Pokopia | Queued; package available | Establish gameplay route/save; inspect sky and geometry before timing |
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

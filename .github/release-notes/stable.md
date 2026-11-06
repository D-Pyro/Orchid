> [!IMPORTANT]
> ## Report issues from inside Orchid
> Use **Help > Report an Issue on GitHub** to open a report with your build, system, GPU, driver and recent game log. After a crash, restart Orchid first and attach the full game log from the folder it opens.

## 0.9.0 — Yoshi gameplay works, shader fixes and smoother Wonder Overworld

### Yoshi and the Mysterious Book now works in tested gameplay

The severe horizontal stripes are fixed: **the first level now renders correctly, Yoshi and the scene are visible, and movement was tested without the previous corruption**. The fix corrects Ampere `IMAD.HI` arithmetic to include the complete 64-bit addend and its low-word carry. Shader corrections also cover packed 16-bit surface writes, storage-image integer types, matrix operations, and shader-code/cache synchronization.

A cold game run reached the first level and confirmed movement, with no device loss, compute-shader rejection or upscale fallback in that run. Docked output was 1920×1080, compared with 1280×720 in handheld mode. This is a first-level gameplay validation, **not a full-game completion or compatibility guarantee**. Some initial graphics-code-read warnings remain; other scenes, hardware and drivers may still have issues.

The Vulkan pipeline-cache revision changes automatically so old compiled shaders are rebuilt after updating.

### Other fixes included

- **Super Mario Bros. Wonder:** prevents simultaneously sampled render-target copies from overwriting each other. Its native Switch 2 Edition can now use the available driver memory budget, with existing safety margins, to reduce Overworld texture eviction/re-upload churn. The paired test improved frame pacing; budget pressure and other causes can still produce dips.
- **Wonder FPS and timing:** retains the custom 1–1000 FPS selector and adds more precise optional speed auditing. The experimental wall-time simulation mode remains opt-in through `EDEN_WONDER_DYNAMIC_FPS=1`; it is not enabled by default.
- **Kirby / dynamic textures:** corrects computed texture handles, descriptor-array limits and shader-cache revisions.
- **Vulkan:** batches command-list copy barriers and corrects viewport/scissor scaling in the viewport-transform-disabled path. The latter is not yet confirmed to resolve the reported Hyrule Warriors blur.
- **Interface and logs:** shows emulation speed with multicore enabled, warns before launching games outside the supported scope, uses Orchid's GitHub links and issue-report wording, and retains each game's latest log.
- **Development and release builds:** adds the local incremental-build helper and improves compiler-cache handling and build-time reporting.

### Validation

The HI64 source shader regression suite passed **202 test cases / 8,659,640 assertions**, including **9 targeted integer-multiply cases / 7,455,271 assertions**. The Wonder follow-up checks passed **13 cases / 12,172 assertions**. The earlier integrated shader fixes also passed 32 generated SPIR-V module validations for Vulkan 1.2. Gameplay checks are short local samples; results on every supported platform are not established by these tests.

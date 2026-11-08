> [!IMPORTANT]
> ## Report issues from inside Orchid
> Use **Help > Report an Issue on GitHub** to open a report with your build, system, GPU, driver and recent game log. After a crash, restart Orchid first and attach the full game log from the folder it opens.

## 0.9.1 — Xenoblade colours, 5.1 audio, NUP add-ons and graphics sync fixes

### Highlights

- **Xenoblade Chronicles: Definitive Edition – Nintendo Switch 2 Edition:** textures, colours and fonts that use the classic Maxwell GOB layout are now decoded correctly. Loose dumps with nested `updates/<version>/program` and add-on folders are detected, so the game loads from an extracted folder.
- **Switch 2 audio:** games that output six-channel (5.1) system-mix audio, such as Orbitals, are now audible instead of silent; the host sink downmixes as needed.
- **NUP files:** updates and DLC embedded in a `.nup` show up in the game list and in the per-game add-on dialog. Android can now load NUP files and builds again.
- **Graphics synchronization (all games, Vulkan):** guest CPU writes to GPU-written memory are no longer overwritten by older GPU downloads (fixes black/white blocks and distorted models in Yoshi and the Mysterious Book with multi-core). Texture uploads and copies now wait for every pipeline stage that uses them, and uploads to buffers already in use are ordered correctly.
- **Linux AppImage:** uses the system Vulkan loader, so Vulkan layers such as MangoHud work.

### Mario Tennis Fever FPS setting (experimental, opt-in)

A per-game FPS setting for Mario Tennis Fever is available under the game's properties, together with a measured game-speed readout in the status bar. **It is highly experimental**: CPU opponents may swing without hitting the ball. The default, 60 FPS, leaves the game unpatched and behaves as in 0.9.0; only other values turn the patch on.

### Fixed before release

- **OpenGL:** pages with a held-back CPU write were never released, so they stopped receiving GPU readbacks and every fence did more work over time. OpenGL now ends each round of writes like Vulkan does.
- The Tennis FPS patch no longer applies at the default 60 FPS (it did on the development builds since 0.9.0).

### Validation

Regression A/B on Linux, NVIDIA Quadro RTX 3000 (driver 595.99), Vulkan, eden-cli, cold shader cache, 150 s each with scripted input: Mario Tennis Fever (practice match), Donkey Kong Bananza (intro), Zelda: Ocarina of Time (intro) render the same as 0.9.0, with the same frame times (within run-to-run noise) and no new errors. Xenoblade Chronicles: Definitive Edition (Switch 2 Edition) reaches its intro from an extracted folder, which 0.9.0 could not load. Unit tests: 635 test cases / 16.6 million assertions passed. The OpenGL fix was not run in a game (no headless OpenGL on the test machine); it mirrors the Vulkan code path. Windows, AMD and macOS were not tested by hand.

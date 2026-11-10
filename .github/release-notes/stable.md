> [!IMPORTANT]
> ## Report issues from inside Orchid
> Use **Help > Report an Issue on GitHub** to open a report with your build, system, GPU, driver and recent game log. After a crash, restart Orchid first and attach the full game log from the folder it opens.

## 0.9.4 — Wonder Switch 2 Edition runs much faster above 60 fps

### Highlights

- **Super Mario Bros. Wonder – Nintendo Switch 2 Edition:** with the game's fps option raised to 240, the first world map went from about 140 to about 180 guest presents a second, and levels run around 45 % faster without a frame limit. The game now gets the same per-title GPU defaults as Orbitals (Low GPU accuracy, Balanced fences, reactive flushing off), which lets the GPU thread split work and stops about 34 MB per frame of GPU-written buffers from being copied back. A per-game setting still wins over these defaults. At the default option of 60 fps nothing changes: it still holds 60.
- **Per-game settings dialog:** resetting a setting to global in the dialog no longer drops the early GPU defaults for the rest of the session (affected Wonder and Orbitals).
- **Crash reports:** Qt fatal errors and uncaught C++ exceptions now write their reason to the log before the process ends, so reports like a silent Windows fail-fast (issue 133) show the cause.

### Known issues

- Xenoblade Chronicles X: the intro video can freeze, and character creation can hang on a black screen.

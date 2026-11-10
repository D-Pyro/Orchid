> [!IMPORTANT]
> ## Report issues from inside Orchid
> Use **Help > Report an Issue on GitHub** to open a report with your build, system, GPU, driver and recent game log. After a crash, restart Orchid first and attach the full game log from the folder it opens.

## 0.9.3 — Orbitals holds 30 fps, lower shader-compile memory

### Highlights

- **Orbitals:** the split-screen hub now holds a steady 30 fps (before 26-29 fps). Per-game GPU defaults, fewer needless GPU waits when the game invalidates its instruction cache, cheaper storage-buffer and buffer-view lookups, and a GPU thread that sleeps instead of spinning while it waits.
- **Shader compilation:** translated shader IR is freed before the driver build, so loading a shader cache holds less memory at once. Adapted from Zortos' Lichii.

### Known issues

- Xenoblade Chronicles X: the intro video can freeze, and character creation can hang on a black screen.

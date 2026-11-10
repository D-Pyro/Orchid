> [!IMPORTANT]
> ## Report issues from inside Orchid
> Use **Help > Report an Issue on GitHub** to open a report with your build, system, GPU, driver and recent game log. After a crash, restart Orchid first and attach the full game log from the folder it opens.

## Hyrule Warriors: Age of Imprisonment combat crash fix

### Highlights

- Fixed excessive GPU buffer readbacks during combat. A previously CPU-read page no longer causes an entire estimated 8 MiB compute write to be copied back at every fence.
- Completed readback buffers are recycled during long frames, and pending readbacks have a bounded producer queue with shutdown cancellation.
- Fixed stale thread-local memory translations when a new memory manager reuses an old manager's address.

### Validation and limitations

- The maintainer tested actual Hyrule combat without the previous hit-triggered hang or crash. Observed dips into the 20 FPS range remain; this is not a measured performance benchmark.
- Local build, targeted regression tests (161 assertions in 8 cases), and scoped mutation checks passed.
- The remaining cross-game gameplay gate was explicitly waived by the maintainer for this release. Bananza reached gameplay, but its full comparison test was not completed.
- The separately reported Kirby allocation crash has not been confirmed fixed.
- Xenoblade Chronicles X: the intro video can freeze, and character creation can hang on a black screen.

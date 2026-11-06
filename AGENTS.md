# Repository instructions

## Commit and push small fixes

For small, clearly scoped fixes requested by the user (for example, emulator UI or
status bar fixes), complete appropriate checks, then commit the changes and push
them directly to `origin/main` without asking for confirmation. This is the user's
standing authorization for these fixes, unless they explicitly request otherwise.

Include only changes belonging to the requested fix. Fetch `origin/main` before
pushing and integrate any newer changes as needed. Never force-push `main` or
overwrite unrelated user changes. If a push is blocked by branch protection or
permissions, report the blocker instead of bypassing it.

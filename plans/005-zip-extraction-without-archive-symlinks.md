# Plan 005: Zip extraction for mods, firmware and data import never creates links from the archive

> **Executor instructions**: Follow this plan step by step. Run every
> verification command and confirm the expected result before moving to the
> next step. If anything in the "STOP conditions" section occurs, stop and
> report — do not improvise. When done, update the status row for this plan
> in `plans/README.md` — unless a reviewer dispatched you and told you they
> maintain the index.
>
> **Drift check (run first)**: `git diff --stat a6597b1ef2..HEAD -- src/qt_common/util/compress.cpp src/qt_common/util/compress.h src/qt_common/util/mod.cpp src/qt_common/util/content.cpp`
> If any in-scope file changed since this plan was written, compare the
> "Current state" excerpts against the live code before proceeding; on a
> mismatch, treat it as a STOP condition.

## Status

- **Priority**: P2
- **Effort**: S
- **Risk**: LOW
- **Depends on**: none
- **Category**: security
- **Planned at**: commit `a6597b1ef2`, 2026-11-10

## Why this matters

The Qt frontend unpacks zip files in three places: installing a mod from a zip, installing
firmware from a zip, and importing a user-data backup. Mod and firmware zips come from the
internet. A zip entry can be a symbolic link, and the extraction code creates that link on disk
with whatever target the archive names. The check that keeps entries inside the output folder
only compares path text, so a later entry written "through" such a link passes the check and
lands wherever the link points, for example in the user's home directory or autostart folder
(Linux and macOS; on Windows a `.lnk` file is created instead, which is harmless). Mods and
firmware never need links. After this plan the extractor skips link entries, and all three
places use the project's own extractor instead of the library helper that has the same
behaviour.

## Current state

**The project's own extractor**: `src/qt_common/util/compress.cpp` (namespace
`QtCommon::Compress`, "a modified version of JlCompress" from the QuaZip library).

```cpp
// src/qt_common/util/compress.cpp:178
QStringList extractDir(QString fileCompressed, QString dir, QtCommon::QtProgressCallback callback) {
    // Open zip
    QuaZip zip(fileCompressed);
    return extractDir(zip, dir, callback);
}

QStringList extractDir(QuaZip& zip, const QString& dir, QtCommon::QtProgressCallback callback) {
    ...
    std::size_t progress = 0;
    callback(total, progress);                       // line 204: called without a null check

    do {
        QString name = zip.getCurrentFileName();
        QString absFilePath = directory.absoluteFilePath(name);
        QString absCleanPath = QDir::cleanPath(absFilePath);
        if (!absCleanPath.startsWith(absCleanDir))   // text comparison only
            continue;
        if (!extractFile(&zip, QLatin1String(""), absFilePath, total, progress, callback)) {
            removeFile(extracted);
            return QStringList();
        }
        extracted.append(absFilePath);
    } while (zip.goToNextFile());
```

```cpp
// src/qt_common/util/compress.cpp:270  (inside extractFile)
    if (info.isSymbolicLink()) {
        QString target = QFile::decodeName(inFile.readAll());
        return QFile::link(target, fileDest);
    }
```

The declaration in `src/qt_common/util/compress.h`:

```cpp
// src/qt_common/util/compress.h:119
QStringList extractDir(QString fileCompressed, QString dir,
                       QtCommon::QtProgressCallback callback = {});
```

`extractFile` has exactly one caller, the loop in `extractDir` shown above. On a failed entry
`extractDir` calls `removeFile(extracted)` to delete what it wrote so far, so the list must only
hold paths that were really created.

`QtProgressCallback` is `std::function<bool(std::size_t, std::size_t)>`
(`src/qt_common/qt_common.h:34`): it receives (total, done) and returns `false` to cancel. Note
that the default argument `{}` is an *empty* function and `extractDir` calls it without a check,
which throws `std::bad_function_call`. So a caller must pass a real callback.

**The three callers:**

```cpp
// src/qt_common/util/mod.cpp, in ExtractMod (starts at line 121), about line 133 -- uses the QuaZip library helper
    QString qCacheDir = QString::fromStdString(tmp.string());

    QFile zip{path};

    // TODO(crueter): use QtCompress
    QStringList result = JlCompress::extractDir(&zip, qCacheDir);
    if (result.isEmpty()) {
```

```cpp
// src/qt_common/util/content.cpp, in UnzipFirmwareToTmp (starts at line 182), about line 193 -- uses the QuaZip library helper
    QString qCacheDir = QString::fromStdString(tmp.string());

    QFile zip(location);

    // TODO(crueter): use QtCompress
    QStringList result = JlCompress::extractDir(&zip, qCacheDir);
    if (result.isEmpty()) {
        return QString();
    }
```

```cpp
// src/qt_common/util/content.cpp:466  (data import) -- already uses the project's extractor
            return !QtCommon::Compress::extractDir(zip_dump_location, QString::fromStdString(dir),
                                                   progress_callback)
                        .empty();
```

`JlCompress::extractDir` from the QuaZip library creates symbolic links from archive entries in
the same way as the code above. `mod.cpp` includes it with `#include <JlCompress.h>` (line 6),
`content.cpp` with `#include <JlCompress.h>` (line 19).

Conventions: 4 spaces, lines up to about 100 columns, comments explain why in one or two lines.
This code is only built into the Qt frontend; the unit test executable does not link Qt, so this
plan has no unit test (see Test plan).

## Commands you will need

Run all commands from the repository root.

| Purpose | Command | Expected on success |
|---|---|---|
| Build the emulator GUI | `./tools/build-local.sh` | exit 0, last line `Run: .../build-local/bin/orchid` |
| No link creation left | `grep -n "QFile::link" src/qt_common/util/compress.cpp` | no output |
| No library extractor left | `grep -rn "JlCompress::extractDir" src/qt_common src/yuzu` | no output |

Notes: `./tools/build-local.sh` without arguments reuses an existing `build-local` folder. On the
maintainer's PC, for a first configure, add
`-DCPM_SOURCE_CACHE=/home/leon/src/Eglantine/.cache/cpm` if that folder exists. Only one full
build at a time on a machine.

## Scope

**In scope** (the only files you should modify):

- `src/qt_common/util/compress.cpp` (the entry loop in `extractDir` and the link branch in `extractFile`)
- `src/qt_common/util/mod.cpp` (`ExtractMod`)
- `src/qt_common/util/content.cpp` (`UnzipFirmwareToTmp`)
- `plans/README.md` (status row)

**Out of scope** (do NOT touch):

- The compression half of `compress.cpp` (`compressDir`, `compressFile`, ...).
- The file permission handling in `extractFile` (`setPermissions(srcPerm)`). It only copies
  mode bits such as "executable" and is left as it is.
- The fixed temporary folders (`<temp>/eden/unzip_mod`, `<temp>/eden/firmware`).
- `src/qt_common/qt_common.cpp`, which also includes `JlCompress.h`: leave its include alone.
- The QuaZip library itself and `cpmfile.json`.

## Git workflow

- One commit. Subject style of this repository: area first, for example
  `frontend: treat Xenoblade 1, 2, X, 3 and Orbitals as supported games`. Suggested subject:
  `frontend: skip symbolic links when unpacking zips`.
- The body says which three places unpack zips and why links are skipped.
- Do NOT push unless the operator told you to or you work under this repository's `AGENTS.md`.

## Steps

### Step 1: Skip link entries in the project's extractor

Two edits in `src/qt_common/util/compress.cpp`.

1. In `extractDir(QuaZip& zip, const QString& dir, ...)`, in the `do { ... } while (zip.goToNextFile());`
   loop, directly after the existing two lines

```cpp
        if (!absCleanPath.startsWith(absCleanDir))
            continue;
```

   add:

```cpp
        // A link's target comes from the archive and can point anywhere; later entries would
        // then be written through it, outside the output folder. Nothing we unpack needs links.
        QuaZipFileInfo64 entry;
        if (zip.getCurrentFileInfo(&entry) && entry.isSymbolicLink())
            continue;
```

   (`continue` in this loop moves on to the next zip entry, exactly like the existing `continue`
   above it. The skipped entry is not added to `extracted`.)

2. In `extractFile`, delete this whole block, so that no caller can create a link:

```cpp
    if (info.isSymbolicLink()) {
        QString target = QFile::decodeName(inFile.readAll());
        return QFile::link(target, fileDest);
    }
```

   Leave the lines before and after it as they are (`info` is still used for the permissions).

**Verify**: `grep -n "QFile::link" src/qt_common/util/compress.cpp` → no output, and
`grep -c "isSymbolicLink" src/qt_common/util/compress.cpp` → `1`.

### Step 2: Use the project's extractor for mod zips

In `src/qt_common/util/mod.cpp`, in `ExtractMod`, replace

```cpp
    QFile zip{path};

    // TODO(crueter): use QtCompress
    QStringList result = JlCompress::extractDir(&zip, qCacheDir);
```

with

```cpp
    // The zip library's own extract helper creates symbolic links from archive entries.
    QStringList result = QtCommon::Compress::extractDir(
        path, qCacheDir, [](std::size_t, std::size_t) { return true; });
```

Add `#include "compress.h"` to the includes of `mod.cpp`, next to the existing `#include "mod.h"`
(`content.cpp` in the same folder includes it in exactly this form, line 14). Then run
`grep -n "JlCompress\|QuaZip" src/qt_common/util/mod.cpp`: if the only remaining match is the
line `#include <JlCompress.h>`, delete that include line. If other code in the file still uses
`JlCompress` or `QuaZip`, keep the include. (At planning time nothing else in `mod.cpp` used
them, and the only use of `QFile` was the line you just removed.)

**Verify**: `grep -n "JlCompress::extractDir" src/qt_common/util/mod.cpp` → no output.

### Step 3: Use the project's extractor for firmware zips

In `src/qt_common/util/content.cpp`, in `UnzipFirmwareToTmp`, replace

```cpp
    QFile zip(location);

    // TODO(crueter): use QtCompress
    QStringList result = JlCompress::extractDir(&zip, qCacheDir);
```

with

```cpp
    // The zip library's own extract helper creates symbolic links from archive entries.
    QStringList result = QtCommon::Compress::extractDir(
        location, qCacheDir, [](std::size_t, std::size_t) { return true; });
```

Then run `grep -n "JlCompress\|QuaZip" src/qt_common/util/content.cpp`: if the only remaining
match is the line `#include <JlCompress.h>`, delete that include line; otherwise keep it.

**Verify**: `grep -rn "JlCompress::extractDir" src/qt_common src/yuzu` → no output.

### Step 4: Build

**Verify**: `./tools/build-local.sh` → exit 0.

If the build fails because `QFile` is no longer declared in `mod.cpp` or `content.cpp` after you
removed the include, add `#include <QFile>` to that file; do not restore `JlCompress.h` for it.

## Test plan

There is no automated test: `compress.cpp` belongs to the Qt frontend library, and the `tests`
executable is built without Qt (the CI test job even configures with `-DENABLE_QT=OFF`). Adding
Qt and QuaZip to the test target is out of proportion for this change. Verification is the
build, the two `grep` checks, and this manual check, which the operator can run once
(describe it in your report, do not attempt GUI automation yourself):

1. Create a test zip with a link entry (needs the `zip` command):
   `rm -rf /tmp/ziptest; mkdir -p /tmp/ziptest/mod/romfs && echo data > /tmp/ziptest/mod/romfs/a.txt && ln -s /tmp/ziptest/target /tmp/ziptest/mod/link && (cd /tmp/ziptest && zip -r -y mod.zip mod)`
2. In the emulator, with no game running: right-click a game in the list, choose
   "Configure Game", open the "Add-Ons" tab and click "Import Mod from ZIP" (the button wired to
   `ConfigurePerGameAddons::InstallModZip`, `src/yuzu/configuration/configure_per_game_addons.cpp:81`).
   Select `/tmp/ziptest/mod.zip`.
3. Expected: the mod is installed with `romfs/a.txt`; no file or folder named `link` exists in
   the installed mod folder or in `<temp>/eden/unzip_mod`.

## Done criteria

ALL must hold:

- [ ] `./tools/build-local.sh` exits 0
- [ ] `grep -n "QFile::link" src/qt_common/util/compress.cpp` prints nothing
- [ ] `grep -c "isSymbolicLink" src/qt_common/util/compress.cpp` prints `1`
- [ ] `grep -rn "JlCompress::extractDir" src/qt_common src/yuzu` prints nothing
- [ ] `grep -c "QtCommon::Compress::extractDir" src/qt_common/util/mod.cpp` prints `1`
- [ ] `git status --short` lists only `compress.cpp`, `mod.cpp`, `content.cpp` and `plans/README.md`. The `plans/` folder may show as untracked (`?? plans/`); that is expected.
- [ ] `plans/README.md` status row updated

## STOP conditions

Stop and report back (do not improvise) if:

- The code at the three call sites or in `extractFile` does not match the excerpts.
- `grep -rn "JlCompress::extractDir" src` shows callers outside `mod.cpp` and `content.cpp`.
  Report them instead of changing them.
- The build fails for a reason other than a missing include.
- You find that a mod or firmware format handled here legitimately contains links.

## Maintenance notes

- `extractDir` in `compress.h` has the default argument `callback = {}`, and the function calls
  the callback without checking it. Calling it with the default crashes with
  `std::bad_function_call`. This plan passes an explicit callback everywhere; making the default
  safe (`if (callback)`) is a small follow-up.
- `removeFile` stops at the first path it cannot delete (`ret = ret && QFile::remove(...)`), so
  the list handed to it must only contain files that exist. That is why the link entry is skipped
  in `extractDir` before it is added to `extracted`, not inside `extractFile`.
- The containment check in `extractDir` is still text based (`startsWith`). It is sufficient as
  long as the extractor never creates links itself and the output folder is fresh, which is the
  case for the two temporary folders. The data import writes into the user's data folder; links
  the user placed there on purpose are followed, as before.
- The temporary folders `<temp>/eden/unzip_mod` and `<temp>/eden/firmware` have fixed names in a
  shared temp directory. On a multi-user machine another user could prepare them. A private
  `QTemporaryDir` would close that; not done here.
- If a newer QuaZip is ever pulled in and someone wants to go back to `JlCompress::extractDir`,
  check first how that version treats symbolic link entries.

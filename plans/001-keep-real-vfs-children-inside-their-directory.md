# Plan 001: Host file creation and deletion through `RealVfsDirectory` stays inside the directory

> **Executor instructions**: Follow this plan step by step. Run every
> verification command and confirm the expected result before moving to the
> next step. If anything in the "STOP conditions" section occurs, stop and
> report — do not improvise. When done, update the status row for this plan
> in `plans/README.md` — unless a reviewer dispatched you and told you they
> maintain the index.
>
> **Drift check (run first)**: `git diff --stat a6597b1ef2..HEAD -- src/core/file_sys/vfs/vfs_real.cpp src/tests/CMakeLists.txt`
> If any in-scope file changed since this plan was written, compare the
> "Current state" excerpts against the live code before proceeding; on a
> mismatch, treat it as a STOP condition.

## Status

- **Priority**: P1
- **Effort**: S
- **Risk**: LOW
- **Depends on**: none
- **Category**: security
- **Planned at**: commit `a6597b1ef2`, 2026-11-10

## Why this matters

`RealVfsDirectory` is the class that turns a virtual directory into real files on the user's
disk. Five of its functions build a host path by appending a child name without checking where
the result points. The child names are not always trusted: "Dump RomFS" in the game list copies
every file and folder name out of a game image (NSP, XCI, NCA), and those names are raw bytes
from the image. A crafted image with a name such as `../../something` makes the emulator create
or truncate files outside the dump folder, anywhere the user can write. The delete functions have
the same shape. After this plan every child path is checked against the directory before the
host file system is touched, and a test proves it.

## Current state

File: `src/core/file_sys/vfs/vfs_real.cpp` (about 600 lines). Namespace `FileSys`, with
`namespace FS = Common::FS;` and an anonymous namespace near the top that already holds the
containment check:

```cpp
// src/core/file_sys/vfs/vfs_real.cpp:61
bool IsWithinRoot(std::string_view root, std::string_view full_path) {
    if (root.empty())
        return true;

    if (full_path.size() < root.size() || full_path.substr(0, root.size()) != root)
        return false;

    return full_path.size() == root.size() || full_path[root.size()] == '/' || full_path[root.size()] == '\\';
}
```

The read functions use it correctly. This is the pattern to match:

```cpp
// src/core/file_sys/vfs/vfs_real.cpp:445
VirtualFile RealVfsDirectory::GetFileRelative(std::string_view relative_path) const {
    const auto full_path = FS::SanitizePath(path + '/' + std::string(relative_path));
    if (!FS::Exists(full_path) || FS::IsDir(full_path)
        || !IsWithinRoot(FS::SanitizePath(path), full_path)) {
        return nullptr;
    }
    return base.OpenFile(full_path, perms);
}
```

`FS::SanitizePath` (in `src/common/fs/path_util.cpp`) resolves `..` components by removing the
component before them. It does not reject them. So `SanitizePath("/a/b/../../x")` is `/x`.

The functions with the problem:

```cpp
// src/core/file_sys/vfs/vfs_real.cpp:471  -- creates parent folders BEFORE the containment check
VirtualFile RealVfsDirectory::CreateFileRelative(std::string_view relative_path) {
    const auto full_path = FS::SanitizePath(path + '/' + std::string(relative_path));
    if (!FS::CreateParentDirs(full_path) || !IsWithinRoot(FS::SanitizePath(path), full_path)) {
        return nullptr;
    }
    return base.CreateFile(full_path, perms);
}

// :479  -- no containment check at all
VirtualDir RealVfsDirectory::CreateDirectoryRelative(std::string_view relative_path) {
    const auto full_path = FS::SanitizePath(path + '/' + std::string(relative_path));
    return base.CreateDirectory(full_path, perms);
}

// :484
bool RealVfsDirectory::DeleteSubdirectoryRecursive(std::string_view name) {
    const auto full_path = FS::SanitizePath(this->path + '/' + std::string(name));
    return FS::RemoveDirRecursively(full_path);
}

// :540
VirtualDir RealVfsDirectory::CreateSubdirectory(std::string_view name) {
    const std::string subdir_path = (path + '/').append(name);
    return base.CreateDirectory(subdir_path, perms);
}

VirtualFile RealVfsDirectory::CreateFile(std::string_view name) {
    const std::string file_path = (path + '/').append(name);
    return base.CreateFile(file_path, perms);
}

bool RealVfsDirectory::DeleteSubdirectory(std::string_view name) {
    const std::string subdir_path = (path + '/').append(name);
    return FS::RemoveDir(subdir_path);
}

bool RealVfsDirectory::DeleteFile(std::string_view name) {
    const std::string file_path = (path + '/').append(name);
    return base.DeleteFile(file_path);
}
```

`base.CreateFile` and `base.CreateDirectory` (`RealVfsFilesystem`, same file, lines 144 and 204)
call `FS::SanitizePath` on whatever they get and then create it. `base.CreateFile` truncates a
file that already exists.

Where untrusted names reach these functions (do not change these callers, they are listed so you
understand the test in step 1):

- `src/core/file_sys/romfs.cpp:81-82` reads each RomFS entry name as raw bytes from the image.
- `src/core/file_sys/vfs/vfs.cpp:533` `VfsRawCopyD` calls `dest->CreateFile(file->GetName())`
  and `dest->CreateSubdirectory(dir->GetName())`.
- `src/yuzu/main_window.cpp:2659,2666` (`RomFSRawCopy`, the "Dump RomFS" action) does the same.
  It already returns `false` when a created file or directory is `nullptr`.

Conventions in this code base:

- 4 spaces, no tabs, lines up to about 100 columns, braces on the same line.
- Comments say why, in one or two lines. No comment that only repeats the code.
- Tests are Catch2, one `tests` executable. Every test `.cpp` is listed by hand in
  `src/tests/CMakeLists.txt`. `src/tests/core/vfs_vector.cpp` is a small example of a test file
  (header comment, includes, anonymous namespace, `TEST_CASE("...", "[core][vfs]")`).
- Temporary folders in tests: see `struct TempDir` in `src/tests/core/nup_writer.cpp:35-46`:

```cpp
struct TempDir {
    fs::path path = fs::temp_directory_path() /
                    ("nup-test-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TempDir() {
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};
```

## Commands you will need

Run all commands from the repository root.

| Purpose | Command | Expected on success |
|---|---|---|
| Configure a build with tests (once) | `./tools/build-local.sh --configure-only -DYUZU_TESTS=ON` | exit 0, `build-local/CMakeCache.txt` exists |
| Build the tests | `cmake --build build-local --target tests --parallel 6` | exit 0 |
| Run the new tests | `./build-local/bin/tests "[vfs_real]"` | `All tests passed` |
| Run the whole suite | `./build-local/bin/tests` | `All tests passed` |
| Mutation check (required by `AGENTS.md`) | `orchid-agent-test --build-dir build-local -- "[vfs_real]"` | exit 0 |

Notes:

- On the maintainer's PC a dependency cache exists. Add
  `-DCPM_SOURCE_CACHE=/home/leon/src/Eglantine/.cache/cpm` to the configure command if that folder
  exists, so nothing is downloaded again.
- Do not run `./tools/build-local.sh` again with options after the first configure; use the
  `cmake --build` line. Only one full build at a time on a machine.
- `orchid-agent-test` is a local tool of the maintainer. If the command does not exist on your
  machine, say so in your report; do not invent a replacement.

## Scope

**In scope** (the only files you should modify):

- `src/core/file_sys/vfs/vfs_real.cpp`
- `src/tests/core/vfs_real.cpp` (create)
- `src/tests/CMakeLists.txt` (one added line)
- `plans/README.md` (status row)

**Out of scope** (do NOT touch, even though they look related):

- `src/common/fs/path_util.cpp` (`SanitizePath`): hundreds of callers depend on its current
  behaviour.
- `src/core/file_sys/vfs/vfs.cpp`, `src/yuzu/main_window.cpp`, `src/qt_common/util/rom.cpp`: the
  callers already handle a `nullptr` result.
- `RealVfsDirectory::Rename` and `RealVfsFilesystem::*`: not part of this plan (see Maintenance
  notes).
- `src/core/file_sys/nup_writer.cpp`: it has its own stricter name check (`IsPlainName`) for
  archive extraction. Leave it.

## Git workflow

- Work in the checkout you were given. Do not create extra worktrees.
- One commit for the whole plan. Message style of this repository: a short subject with the area
  first, then a body that says what was wrong and how it was checked. Example subject from the
  log: `loader: serve sibling program RomFS of loose multi-program dumps`. Suggested subject here:
  `vfs: keep created and deleted children inside their real directory`.
- Do NOT push unless the operator told you to, or you work under this repository's `AGENTS.md`
  rules, which define when a push to `origin/main` is allowed (all checks green, and the Mario
  Tennis Fever match gate for emulator changes).

## Steps

### Step 1: Write the failing tests

Create `src/tests/core/vfs_real.cpp`. Copy the two-line licence header from
`src/tests/core/vfs_vector.cpp`. Includes you need: `<chrono>`, `<filesystem>`, `<fstream>`,
`<memory>`, `<string>`, `<string_view>`, `<vector>`, `<catch2/catch_test_macros.hpp>`,
`"core/file_sys/vfs/vfs.h"`, `"core/file_sys/vfs/vfs_real.h"`, `"core/file_sys/vfs/vfs_vector.h"`.

Open an anonymous namespace. Its first line is `namespace fs = std::filesystem;` (the `TempDir`
struct and the helper below use `fs::`). Then add a `TempDir` struct like the one shown above
(use the prefix `"vfs-real-test-"`) and a small helper that writes a file:

```cpp
void Put(const fs::path& file, std::string_view content) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << content;
}
```

Every test uses this layout: `t.path / "out"` is the directory under test, everything else below
`t.path` is "outside". Open the directory like this:

```cpp
FileSys::RealVfsFilesystem vfs;
fs::create_directories(t.path / "out");
const auto dir = vfs.OpenDirectory((t.path / "out").string(), FileSys::OpenMode::ReadWrite);
REQUIRE(dir != nullptr);
```

Write three test cases, all tagged `"[core][vfs_real]"`:

1. `"RealVfsDirectory refuses child names that leave the directory"`:
   - `dir->CreateFile("../escape.txt")` is `nullptr`, and `t.path / "escape.txt"` does not exist.
   - `dir->CreateSubdirectory("../evil")` is `nullptr`, and `t.path / "evil"` does not exist.
   - `dir->CreateSubdirectory("..")` is `nullptr`.
   - `dir->CreateFileRelative("../made/escape.txt")` is `nullptr`, and `t.path / "made"` does
     not exist (this proves no parent folder is created before the check).
   - `dir->CreateDirectoryRelative("../evil2")` is `nullptr`, and `t.path / "evil2"` does not
     exist.
   - Create `t.path / "victim.txt"` with `Put`. `dir->DeleteFile("../victim.txt")` is `false`
     and the file still exists.
   - Create `t.path / "victimdir" / "keep.txt"` with `Put`.
     `dir->DeleteSubdirectoryRecursive("../victimdir")` is `false` and `keep.txt` still exists.
   - Create the empty folder `t.path / "emptydir"`. `dir->DeleteSubdirectory("../emptydir")` is
     `false` and the folder still exists.
2. `"RealVfsDirectory creates and deletes plain children"` (guards against over-blocking):
   - `dir->CreateFile("ok.txt")` is not `nullptr` and `t.path / "out" / "ok.txt"` exists.
   - `dir->CreateSubdirectory("sub")` is not `nullptr` and the folder exists.
   - `dir->CreateFileRelative("a/b/c.txt")` is not `nullptr` and the file exists.
   - `dir->CreateDirectoryRelative("d/e")` is not `nullptr` and the folder exists.
   - `dir->DeleteFile("ok.txt")` is `true`; `dir->DeleteSubdirectory("sub")` is `true`;
     `dir->DeleteSubdirectoryRecursive("a")` is `true`; all three are gone afterwards.
3. `"A raw directory copy cannot write outside its target"`:
   - Build a source tree in memory, a directory that holds one file with a hostile name:

     ```cpp
     const FileSys::VirtualFile hostile =
         std::make_shared<FileSys::VectorVfsFile>(std::vector<u8>{1, 2, 3}, "../escape.bin");
     const FileSys::VirtualDir src = std::make_shared<FileSys::VectorVfsDirectory>(
         std::vector<FileSys::VirtualFile>{hostile}, std::vector<FileSys::VirtualDir>{}, "src");
     ```

   - `FileSys::VfsRawCopyD(src, dir)` is `false`, and `t.path / "escape.bin"` does not exist.

Add the line `    core/vfs_real.cpp` to the source list in `src/tests/CMakeLists.txt`, directly
after the existing line `    core/vfs_vector.cpp`.

**Verify**: `cmake --build build-local --target tests --parallel 6 && ./build-local/bin/tests "[vfs_real]"`
→ the build succeeds; test case 1 and test case 3 FAIL (files are created outside), test case 2
passes. If test case 1 or 3 already passes before you changed `vfs_real.cpp`, STOP (see STOP
conditions).

### Step 2: Add one containment helper and use it

In `src/core/file_sys/vfs/vfs_real.cpp`, inside the existing anonymous namespace, directly after
`IsWithinRoot`, add:

```cpp
// A child name can come from a parsed game image or from the guest, so the path it forms has to
// stay below the directory.
bool IsChildPath(const std::string& root, std::string_view name) {
    return IsWithinRoot(FS::SanitizePath(root),
                        FS::SanitizePath(root + '/' + std::string(name)));
}
```

Then change the seven functions. Keep everything else in them as it is. The first two already
compute `full_path` and keep using `IsWithinRoot` directly; the other five call the new helper,
written exactly as `IsChildPath(path, name)` (no `this->`), so the done criterion below counts 5.

- `CreateFileRelative`: test containment first, then create parents. Swap the two conditions:
  `if (!IsWithinRoot(FS::SanitizePath(path), full_path) || !FS::CreateParentDirs(full_path)) {`
- `CreateDirectoryRelative`: after computing `full_path`, add
  `if (!IsWithinRoot(FS::SanitizePath(path), full_path)) { return nullptr; }`
- `DeleteSubdirectoryRecursive`: first line `if (!IsChildPath(path, name)) { return false; }`
- `CreateSubdirectory`: first line `if (!IsChildPath(path, name)) { return nullptr; }`
- `CreateFile`: first line `if (!IsChildPath(path, name)) { return nullptr; }`
- `DeleteSubdirectory`: first line `if (!IsChildPath(path, name)) { return false; }`
- `DeleteFile`: first line `if (!IsChildPath(path, name)) { return false; }`

Do not change what is passed to `base.CreateFile`, `base.CreateDirectory`, `base.DeleteFile`,
`FS::RemoveDir` or `FS::RemoveDirRecursively`.

**Verify**: `cmake --build build-local --target tests --parallel 6 && ./build-local/bin/tests "[vfs_real]"`
→ `All tests passed`, 3 test cases.

### Step 3: Run the whole suite and the mutation check

**Verify**: `./build-local/bin/tests` → `All tests passed`.

**Verify**: `orchid-agent-test --build-dir build-local -- "[vfs_real]"` → exit 0. Exit 1 means
a mutant survived: read the report under `~/Orchid/orchid-issues/mutation/<run>/`, add the
missing assertion to the tests and run it again. Never lower the threshold.

## Test plan

- New file `src/tests/core/vfs_real.cpp` with the three test cases from step 1: the escape
  attempts (the regression this plan fixes), the plain-children case (no over-blocking), and the
  end-to-end raw copy.
- Structural pattern: `src/tests/core/vfs_vector.cpp` for the file shape,
  `src/tests/core/nup_writer.cpp:35-46` for the temporary folder.
- Verification: `./build-local/bin/tests "[vfs_real]"` → all pass, 3 test cases.

## Done criteria

ALL must hold:

- [ ] `./build-local/bin/tests "[vfs_real]"` prints `All tests passed` with 3 test cases
- [ ] `./build-local/bin/tests` prints `All tests passed`
- [ ] `orchid-agent-test --build-dir build-local -- "[vfs_real]"` exits 0 (or the report states the tool is not installed)
- [ ] `grep -c "IsChildPath(path, name)" src/core/file_sys/vfs/vfs_real.cpp` prints `5`
- [ ] `git status --short` lists only `src/core/file_sys/vfs/vfs_real.cpp`, `src/tests/core/vfs_real.cpp`, `src/tests/CMakeLists.txt` and `plans/README.md`. The `plans/` folder may show as untracked (`?? plans/`); that is expected.
- [ ] `plans/README.md` status row updated

## STOP conditions

Stop and report back (do not improvise) if:

- The functions in `vfs_real.cpp` do not match the excerpts in "Current state".
- In step 1, test case 1 or 3 passes before the fix. Then the behaviour described here is not
  what the code does, and the plan's premise is wrong.
- After step 2, test case 2 fails, or any other existing test fails (for example a `[nup]` or
  `[loose_directory]` test). That means the check rejects a legitimate path; report which path.
- A verification fails twice after a reasonable fix attempt.
- You find you would have to change `SanitizePath` or a caller to make a test pass.

## Maintenance notes

- The check is lexical. A symbolic link that already exists inside the directory can still lead
  outside. That is the user's own link and is not covered here.
- A `RealVfsDirectory` opened on the file system root (`/`) stores an empty `path`
  (`RemoveTrailingSlash`), and `IsWithinRoot` allows everything for an empty root. That is the
  same as in the existing read functions; nothing opens the root this way. Do not add a test
  there.
- An empty child name forms the directory's own path, which counts as inside. `CreateFile("")`
  then fails in the file system as before; the delete functions with an empty name act on the
  directory itself, also as before.
- `RealVfsDirectory::Rename(name)` builds `parent_path + '/' + name` without a check and was left
  alone on purpose: its callers pass names the emulator made itself. If guest-visible renames are
  ever routed through it, add the same check.
- A reviewer should look at one thing: that no legitimate caller passes a name that points
  outside on purpose. `grep -rn "CreateSubdirectory(\|CreateFile(" src --include=*.cpp` shows the
  callers; none of the ones found during planning did.

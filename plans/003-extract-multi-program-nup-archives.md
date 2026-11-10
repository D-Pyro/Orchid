# Plan 003: NUP archives of Switch 2 Edition dumps (several programs) can be extracted again

> **Executor instructions**: Follow this plan step by step. Run every
> verification command and confirm the expected result before moving to the
> next step. If anything in the "STOP conditions" section occurs, stop and
> report — do not improvise. When done, update the status row for this plan
> in `plans/README.md` — unless a reviewer dispatched you and told you they
> maintain the index.
>
> **Drift check (run first)**: `git diff --stat a6597b1ef2..HEAD -- src/core/file_sys/nup_writer.cpp src/core/file_sys/nup_writer.h src/tests/core/nup_writer.cpp`
> If any in-scope file changed since this plan was written, compare the
> "Current state" excerpts against the live code before proceeding; on a
> mismatch, treat it as a STOP condition.

## Status

- **Priority**: P1
- **Effort**: S
- **Risk**: LOW
- **Depends on**: none
- **Category**: bug
- **Planned at**: commit `a6597b1ef2`, 2026-11-10

## Why this matters

A `.nup` file is Orchid's archive format for a game dump. `PackDump` writes one, `ExtractNup`
turns it back into folders. Since commit `15cbd07a54` the packer stores a Switch 2 Edition dump
as *several* "base" packages: the program that boots plus its sibling programs (the Switch 2
Edition of a game reads the original game's data from program 0). The extractor still puts every
base package into the folder `base`, sees the second one as a duplicate and aborts with
"two packages belong in base". So every archive packed from a Switch 2 Edition dump, the titles
this project cares about most, cannot be extracted. After this plan such an archive extracts
into `program/<index>/` folders, which is a layout the packer and the loader already read.

## Current state

File: `src/core/file_sys/nup_writer.cpp` (namespace `FileSys::Nup`, helpers in an anonymous
namespace; `fs` is `std::filesystem`).

The extractor's folder choice and the duplicate check:

```cpp
// src/core/file_sys/nup_writer.cpp:1625
// The folder Discovery reads the package back from.
fs::path PackageFolder(const NupContainer::PackageInfo& package) {
    switch (package.kind) {
    case NupContainer::PackageKind::Base:
        return "base";
    case NupContainer::PackageKind::Update:
        return fs::path{"updates"} / std::to_string(package.version);
    case NupContainer::PackageKind::Addon:
        break;
    }
    return fs::path{"dlc"} / fmt::format("{:016X}", package.title_id) /
           std::to_string(package.version);
}
```

```cpp
// src/core/file_sys/nup_writer.cpp:1664  (inside Extractor::Run)
const auto& packages = nup.GetPackages();
std::vector<fs::path> folders;
for (size_t i = 0; i < packages.size(); ++i) {
    auto folder = PackageFolder(packages[i]);
    if (std::ranges::find(folders, folder) != folders.end()) {
        throw PackError(PackResult::InvalidSource,
                        "two packages belong in " + ToUtf8(folder));
    }
    folders.push_back(std::move(folder));
    ...
```

`PackageFolder` has exactly one caller (line 1667).

How the packer creates several base packages (same file, `Discovery::Run`):

```cpp
// src/core/file_sys/nup_writer.cpp:559
if (program_variant) {
    // A legacy base/update must not shadow the native edition's selected executable.
    const auto program = selected->parent_path().parent_path();
    base_title_id = AddPackage(KindBase, program, 0, {});
    // The other programs follow as further base packages, keyed by program index:
    // Switch 2 Editions read the original game data of program 0.
    std::vector<std::pair<u64, fs::path>> siblings;
    for (const auto& sibling : fs::directory_iterator(program.parent_path(), ec)) {
        const auto index = ParseDecimal(ToUtf8(sibling.path().filename()));
        const auto romfs = FindDirectory(sibling.path(), "romfs");
        if (index && *index <= 0xFF && sibling.path() != program && !romfs.empty() &&
            DirHasFiles(romfs)) {
            siblings.emplace_back(*index, sibling.path());
        }
    }
    std::ranges::sort(siblings);
    for (const auto& [index, path] : siblings) {
        AddPackage(KindBase, path, 0, {});
        plan.packages.back().title_id = GetBaseTitleIDWithProgramIndex(*base_title_id, index);
    }
}
```

So in such an archive: package 0 is the booting program with the title ID from its own NPDM
file; each further base package is a sibling whose title ID is "base title ID + program index".
The helpers are in `src/core/file_sys/common_funcs.h` and already usable in `nup_writer.cpp`
(`GetBaseTitleIDWithProgramIndex` is called there without a prefix):

```cpp
constexpr u64 BASE_TITLE_ID_MASK = 0xFFFFFFFFFFFFE000;
constexpr u64 GetBaseTitleID(u64 title_id) { return title_id & BASE_TITLE_ID_MASK; }
constexpr u64 GetBaseTitleIDWithProgramIndex(u64 title_id, u64 program_index) {
    return GetBaseTitleID(title_id) + program_index;
}
```

The loose-folder layout the packer and loader recognise for multi-program dumps
(`src/core/loader/loose_directory.h`, `ResolveLooseProgramPath`): a folder named `program`
directly in the dump root (or under `upgrade/<version>/`), with one subfolder per program index
(`0`, `1`), each holding `exefs/` and `romfs/`. Program `1` is preferred over `0` as the one that
boots. `IsProgramVariant` in `nup_writer.cpp:141` accepts index `0` or `1` below a folder named
`program`.

The header comment that describes the extract layout:

```cpp
// src/core/file_sys/nup_writer.h:76
// Writes every package of a NUP back to the folder layout PackDump reads: base/,
// updates/<version>/ and dlc/<titleid>/<version>/. Each file is checked against its stored
// SHA-256. `output` must be missing or empty; a failed or cancelled run removes what it wrote.
PackResult ExtractNup(const std::filesystem::path& archive, const std::filesystem::path& output,
                      const std::function<void(const PackProgress&)>& progress = {},
                      const std::atomic_bool* cancel = nullptr, std::string* error_out = nullptr);
```

Tests: `src/tests/core/nup_writer.cpp`, tag `[nup]`. Two existing tests are your patterns:

- `"NUP packs the preferred edition program with its family DLC"` (line 704) builds a Switch 2
  Edition dump and packs it. Its setup lines (704 to about 741, up to and including the
  `PackDump` call) are the exact way to produce an archive with two base packages. It asserts
  `nup.GetPackages().size() == 3` (base program 1, base program 0, one add-on).
- `"NUP extracts every package and repacks to the same archive"` (line 937) shows how to call
  `ExtractNup`, compare folder trees with `SourceTree(path)`, and repack.

Helpers in that test file you will use: `TempDir`, `Put(path, text)`, `ReadAll(path)`,
`Read(virtual_file)`, `Npdm(id)`, `NativeNpdm(id)`, `MakeDump(root)`, `SourceTree(dir)`,
`PackedTree(virtual_dir)`, the constant `TitleId`.

Conventions: 4 spaces, lines up to about 100 columns, comments explain why in one or two lines.

## Commands you will need

Run all commands from the repository root.

| Purpose | Command | Expected on success |
|---|---|---|
| Configure a build with tests (once) | `./tools/build-local.sh --configure-only -DYUZU_TESTS=ON` | exit 0 |
| Build the tests | `cmake --build build-local --target tests --parallel 6` | exit 0 |
| Run the NUP tests | `./build-local/bin/tests "[nup]"` | `All tests passed` |
| Run the whole suite | `./build-local/bin/tests` | `All tests passed` |
| Mutation check (required by `AGENTS.md`) | `orchid-agent-test --build-dir build-local -- "[nup]"` | exit 0 |

Notes: on the maintainer's PC add `-DCPM_SOURCE_CACHE=/home/leon/src/Eglantine/.cache/cpm` to the
configure command if that folder exists. Skip the configure if `build-local/CMakeCache.txt`
already has `YUZU_TESTS:BOOL=ON`. `orchid-agent-test` is a local tool of the maintainer; if it
does not exist on your machine, say so in the report.

## Scope

**In scope** (the only files you should modify):

- `src/core/file_sys/nup_writer.cpp` (`PackageFolder` and its one call site)
- `src/core/file_sys/nup_writer.h` (the comment above `ExtractNup`)
- `src/tests/core/nup_writer.cpp` (one new test case)
- `plans/README.md` (status row)

**Out of scope** (do NOT touch):

- `Discovery` (the packer side), `nup_container.cpp`, `src/core/loader/*`: the archive format and
  the loader stay as they are. Existing `.nup` files must keep working.
- The layout for archives with a single base package: it stays `base/`. The existing round-trip
  test compares the repacked archive byte for byte and must keep passing unchanged.
- The Qt dialogs that call `ExtractNup`.

## Git workflow

- One commit. Subject style of this repository: area first, for example
  `nup: extract archives and edit their metadata in place`. Suggested subject:
  `nup: extract multi-program archives into program/<index>`.
- The body says which archives failed before (any packed from a Switch 2 Edition dump since
  `15cbd07a54`) and how it was tested.
- Do NOT push unless the operator told you to or you work under this repository's `AGENTS.md`.

## Steps

### Step 1: Write the failing test

In `src/tests/core/nup_writer.cpp`, directly after the test case
`"NUP extracts every package and repacks to the same archive"`, add a new test case
`"NUP extracts a multi-program archive into program folders and repacks it"` with the tag
`"[nup]"`.

Setup: copy the dump-building lines from the test at line 704
(`"NUP packs the preferred edition program with its family DLC"`), from `TempDir t;` up to and
including the block that writes `programs / "1/control/Acd_1/control.nacp"`. Then:

```cpp
    const auto archive = t.path / "Game.nup";
    REQUIRE(FileSys::Nup::PackDump(root, archive) == FileSys::Nup::PackResult::Success);

    // The repacked archive records its folder name, so extract into one of the same name.
    const auto output = t.path / "out" / "Game";
    std::string error;
    const auto extracted = FileSys::Nup::ExtractNup(archive, output, {}, nullptr, &error);
    INFO(error);
    REQUIRE(extracted == FileSys::Nup::PackResult::Success);

    // Each program lands under its index; there is no single "base".
    REQUIRE_FALSE(fs::exists(output / "base"));
    REQUIRE(ReadAll(output / "program/1/exefs/main") == "native program 1");
    REQUIRE(ReadAll(output / "program/0/exefs/main") == "legacy program 0");
    REQUIRE(ReadAll(output / "program/0/romfs/legacy.bin") == "legacy data");
    REQUIRE(SourceTree(output / "dlc/0100ABCD00011001/0") ==
            SourceTree(root / "dlc/0100ABCD00011001/0"));

    // The extracted folder packs to the same packages again.
    const auto repacked = t.path / "out" / "Game.nup";
    REQUIRE(FileSys::Nup::PackDump(output, repacked) == FileSys::Nup::PackResult::Success);
    FileSys::RealVfsFilesystem real;
    NupContainer first{real.OpenFile(archive.string(), FileSys::OpenMode::Read)};
    NupContainer second{real.OpenFile(repacked.string(), FileSys::OpenMode::Read)};
    REQUIRE(second.GetStatus() == Loader::ResultStatus::Success);
    REQUIRE(second.GetPackages().size() == first.GetPackages().size());
    for (size_t i = 0; i < first.GetPackages().size(); ++i) {
        INFO("package " << i);
        REQUIRE(second.GetPackages()[i].kind == first.GetPackages()[i].kind);
        REQUIRE(second.GetPackages()[i].title_id == first.GetPackages()[i].title_id);
        REQUIRE(second.GetPackages()[i].version == first.GetPackages()[i].version);
        REQUIRE(PackedTree(second.GetExeFSDir(i)) == PackedTree(first.GetExeFSDir(i)));
        REQUIRE(PackedTree(second.GetRomFSDir(i)) == PackedTree(first.GetRomFSDir(i)));
    }
    REQUIRE(ReadAll(repacked) == ReadAll(archive));
```

Adjust only what the compiler demands (for example if `PackedTree` does not accept a null
directory for a package without ExeFS, guard that one comparison with a null check on both
sides and require both to be null or both non-null).

**Verify**: `cmake --build build-local --target tests --parallel 6 && ./build-local/bin/tests "NUP extracts a multi-program archive into program folders and repacks it"`
→ the test FAILS at `REQUIRE(extracted == ...Success)` and the printed `error` contains
`two packages belong in base`. If it fails earlier (in `PackDump`) or passes, STOP.

### Step 2: Give each program of a multi-program archive its own folder

In `src/core/file_sys/nup_writer.cpp` change `PackageFolder` to:

```cpp
// The folder Discovery reads the package back from. An archive with several base packages is a
// multi-program dump (a Switch 2 Edition next to the original game's program): each one goes to
// program/<index>, the index being the low bits of its title ID.
fs::path PackageFolder(const NupContainer::PackageInfo& package, bool multi_program) {
    switch (package.kind) {
    case NupContainer::PackageKind::Base:
        if (multi_program) {
            return fs::path{"program"} /
                   std::to_string(package.title_id - GetBaseTitleID(package.title_id));
        }
        return "base";
    case NupContainer::PackageKind::Update:
        return fs::path{"updates"} / std::to_string(package.version);
    case NupContainer::PackageKind::Addon:
        break;
    }
    return fs::path{"dlc"} / fmt::format("{:016X}", package.title_id) /
           std::to_string(package.version);
}
```

In `Extractor::Run`, directly after `const auto& packages = nup.GetPackages();`, add:

```cpp
        const bool multi_program =
            std::ranges::count(packages, NupContainer::PackageKind::Base,
                               &NupContainer::PackageInfo::kind) > 1;
```

and change the call to `PackageFolder(packages[i], multi_program)`. Leave the duplicate check
("two packages belong in ...") as it is: two programs with the same index are still an error.

`<algorithm>` is already included in this file (it uses `std::ranges::find` and
`std::ranges::sort`); if the compiler says `std::ranges::count` is unknown, add
`#include <algorithm>`.

**Verify**: `cmake --build build-local --target tests --parallel 6 && ./build-local/bin/tests "[nup]"`
→ `All tests passed`.

If everything passes except the last line of the new test,
`REQUIRE(ReadAll(repacked) == ReadAll(archive));`, while all package and tree comparisons above
it pass: delete that one line, keep the rest, and state in the commit message and in your report
that the repack is equal package by package but not byte for byte. Do not change production code
to force byte equality.

### Step 3: Update the header comment

In `src/core/file_sys/nup_writer.h`, change the comment above `ExtractNup` to:

```cpp
// Writes every package of a NUP back to the folder layout PackDump reads: base/ (or
// program/<index>/ for each program when the archive holds several), updates/<version>/ and
// dlc/<titleid>/<version>/. Each file is checked against its stored SHA-256. `output` must be
// missing or empty; a failed or cancelled run removes what it wrote.
```

**Verify**: `cmake --build build-local --target tests --parallel 6` → exit 0.

### Step 4: Whole suite and mutation check

**Verify**: `./build-local/bin/tests` → `All tests passed`.

**Verify**: `orchid-agent-test --build-dir build-local -- "[nup]"` → exit 0. On exit 1 read the
report under `~/Orchid/orchid-issues/mutation/<run>/`, add the assertion that kills the surviving
mutant (for example: an archive with ONE base package still extracts to `base/`, which the
existing round-trip test covers; check it is selected by your filter), and run again. Never lower
the threshold.

## Test plan

- One new test case in `src/tests/core/nup_writer.cpp`:
  `"NUP extracts a multi-program archive into program folders and repacks it"`. It covers the
  regression (extraction fails today), the folder layout, and that the extracted folder is a
  valid dump again.
- The existing test `"NUP extracts every package and repacks to the same archive"` guards the
  single-base layout (`base/`) and must pass unchanged.
- Verification: `./build-local/bin/tests "[nup]"` → all pass, one more test case than before.

## Done criteria

ALL must hold:

- [ ] `./build-local/bin/tests "[nup]"` prints `All tests passed`
- [ ] `./build-local/bin/tests` prints `All tests passed`
- [ ] `orchid-agent-test --build-dir build-local -- "[nup]"` exits 0 (or the report states the tool is not installed)
- [ ] `grep -c "PackageFolder(packages\[i\], multi_program)" src/core/file_sys/nup_writer.cpp` prints `1`
- [ ] `git diff --stat` shows no change to the existing test case at line 937 (only added lines in the test file)
- [ ] `git status --short` lists only `nup_writer.cpp`, `nup_writer.h`, the test file and `plans/README.md`. The `plans/` folder may show as untracked (`?? plans/`); that is expected.
- [ ] `plans/README.md` status row updated

## STOP conditions

Stop and report back (do not improvise) if:

- `PackageFolder` or `Extractor::Run` does not match the excerpts in "Current state".
- The step 1 test does not fail with "two packages belong in base".
- After step 2, `PackDump(output, repacked)` fails, or the repacked archive has a different
  number of packages or different title IDs. That means the `program/<index>` layout is not read
  back the way this plan assumes; report the actual package list of both archives.
- The existing test `"NUP extracts every package and repacks to the same archive"` fails.
- You find the fix needs a change in `Discovery`, `nup_container.cpp` or the loader.

## Maintenance notes

- The folder index comes from the title ID's low bits (`title_id - GetBaseTitleID(title_id)`).
  For sibling programs the packer sets exactly that. For the booting program the title ID comes
  from its NPDM; the plan assumes program N of an application has "base ID + N", which is how the
  loader keys sibling programs too (`FindLooseSiblingPrograms`). A dump whose booting program
  carries a plain base ID while its folder was `program/1` would collide with sibling 0 and still
  report "two packages belong in program/0". If a user log shows that message, the fix is to
  record the program index in the package metadata when packing.
- The packer only treats `program/0` and `program/1` as bootable (`IsProgramVariant`). An archive
  whose booting program has a higher index extracts fine but will not repack as an edition.
- Archives packed before `15cbd07a54` have one base package and are not affected.

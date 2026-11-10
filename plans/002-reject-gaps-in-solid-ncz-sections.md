# Plan 002: A solid NCZ with a gap between sections fails cleanly instead of reading past its buffer

> **Executor instructions**: Follow this plan step by step. Run every
> verification command and confirm the expected result before moving to the
> next step. If anything in the "STOP conditions" section occurs, stop and
> report — do not improvise. When done, update the status row for this plan
> in `plans/README.md` — unless a reviewer dispatched you and told you they
> maintain the index.
>
> **Drift check (run first)**: `git diff --stat a6597b1ef2..HEAD -- src/core/crypto/ncz_decompressor.cpp src/tests/core/ncz_decompressor.cpp`
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

NSZ and XCZ game files contain compressed NCA files ("NCZ"). When the user installs one, the
"solid" kind of NCZ (one zstd stream for the whole body) is decoded by `SolidNczFile` into a
byte vector. The decoder keeps a counter, `frontier`, for how many NCA bytes are decoded, and
`Read` trusts that the vector is at least that long. The section table comes straight from the
file. If two sections are not adjacent, the counter jumps ahead of the vector, and the next
`Read` copies bytes from behind the end of the vector: a heap over-read whose bytes end up in the
installed file, or a crash. Files made by the real `nsz` tool have adjacent sections, so only a
damaged or crafted file triggers it. After this plan such a file makes the read return 0 bytes
(the install fails with the normal error) and there is a first test for the solid decoder.

## Current state

File: `src/core/crypto/ncz_decompressor.cpp` (517 lines), namespace `Core::Crypto`, everything
below is in its anonymous namespace.

Constants and the section record (lines 28-45, `HeaderSize` is `0x4000`):

```cpp
struct Section {
    s64 offset, size;
    u64 crypto_type;
    Key128 key, counter;
    bool Ctr() const { return crypto_type == 3 || crypto_type == 4; }
};
```

`ParseHeader` (lines 66-121) reads `count` sections from the file. It checks the count and the
table size, nothing else: offsets and sizes are taken as they are. It only fills a gap *before
the first* section with a fake section.

The solid reader (lines 332-457):

```cpp
// :336
SolidNczFile(std::string name_, FileSys::VirtualFile source_,
             std::vector<Section> sections_, u64 payload_start_)
    : name{std::move(name_)}, source{std::move(source_)}, sections{std::move(sections_)},
      payload_start{payload_start_} {
    const auto raw_header = source->ReadBytes(HeaderSize, 0);
    data.assign(raw_header.begin(), raw_header.end());
    frontier = HeaderSize;
}

// :362
std::size_t Read(u8* out, std::size_t len, std::size_t off) const override {
    std::scoped_lock lock{mutex};
    const u64 nca_size = GetSize();
    if (off >= nca_size) return 0;
    len = static_cast<std::size_t>(std::min<u64>(len, nca_size - off));
    const u64 needed = off + len;
    if (needed > frontier && !DecodeTo(needed)) return 0;
    std::memcpy(out, data.data() + off, len);
    return len;
}
```

Inside `DecodeTo` (lines 377-443) the second loop walks the sections:

```cpp
for (; index < sections.size() && frontier < needed; ++index) {
    const auto& s = sections[index];
    const u64 begin = std::max<u64>(static_cast<u64>(s.offset), HeaderSize);
    const u64 end = std::max<u64>(static_cast<u64>(s.offset) + static_cast<u64>(s.size),
                                  begin);
    if (end <= frontier) continue;
    u64 pos = std::max<u64>(begin, frontier);

    std::optional<AESCipher<Key128>> cipher;
    if (s.Ctr()) cipher.emplace(s.key, Mode::CTR);

    while (pos < end && frontier < needed) {
        ...
        data.insert(data.end(), out_buf.begin(), out_buf.begin() + produced);
        pos += produced;
        frontier = pos;
    }
}
return frontier >= needed;
```

The bug: when `begin > frontier` (a gap), `pos` starts at `begin`. The decoded bytes are appended
at `data.size()`, which is the old `frontier`, but `frontier` is set to the new `pos`. From then
on `frontier > data.size()`, and `Read` copies from beyond the vector.

The error style in this function is a log line and `return false`, for example:

```cpp
if (chunk == 0) { LOG_ERROR(Crypto, "Solid NCZ: truncated"); return false; }
```

The solid reader is only used for installing: `DecompressNCZForInstall` (line 513), called from
`src/core/file_sys/registered_cache.cpp:838`. `DecompressNCZ` (used for playing) refuses solid
files.

Tests: `src/tests/core/ncz_decompressor.cpp` (238 lines, already listed in
`src/tests/CMakeLists.txt`). All existing tests build *block* NCZ files (with an `NCZBLOCK`
header). There is no test for a solid NCZ. Helpers you will reuse, already in that file's
anonymous namespace:

```cpp
constexpr std::size_t NcaHeaderSize = 0x4000;
constexpr std::size_t SectionSize = 0x4000;
void PutLe(std::vector<u8>& out, u64 value, std::size_t bytes);
void PutMagic(std::vector<u8>& out, const char (&magic)[9]);
void PutSection(std::vector<u8>& out, u64 offset, u64 size, u64 crypto_type);
```

A solid NCZ is: `0x4000` header bytes, the magic `NCZSECTN`, the section count as 8 bytes
little-endian, the section entries, then directly one zstd stream of the whole body (no
`NCZBLOCK` magic). `Common::Compression::CompressDataZSTDDefault(const u8*, std::size_t)` from
`common/zstd_compression.h` (already included in the test file) produces the stream.
Crypto type `1` means "no encryption", so the expected output is the plain body.

## Commands you will need

Run all commands from the repository root.

| Purpose | Command | Expected on success |
|---|---|---|
| Configure a build with tests (once) | `./tools/build-local.sh --configure-only -DYUZU_TESTS=ON` | exit 0 |
| Build the tests | `cmake --build build-local --target tests --parallel 6` | exit 0 |
| Run the NCZ tests | `./build-local/bin/tests "[ncz]"` | `All tests passed` |
| Run the whole suite | `./build-local/bin/tests` | `All tests passed` |
| Mutation check (required by `AGENTS.md`) | `orchid-agent-test --build-dir build-local -- "[ncz]"` | exit 0 |

Notes: on the maintainer's PC add `-DCPM_SOURCE_CACHE=/home/leon/src/Eglantine/.cache/cpm` to the
configure command if that folder exists. If `build-local/CMakeCache.txt` already exists with
`YUZU_TESTS:BOOL=ON`, skip the configure. `orchid-agent-test` is a local tool of the maintainer;
if it does not exist on your machine, say so in the report.

## Scope

**In scope** (the only files you should modify):

- `src/core/crypto/ncz_decompressor.cpp`
- `src/tests/core/ncz_decompressor.cpp`
- `plans/README.md` (status row)

**Out of scope** (do NOT touch):

- `ParseHeader` and the block reader (`BlockReader`, `BlockNczFile`): the block path has its own
  bounds checks and tests.
- `src/core/file_sys/registered_cache.cpp`: the installer already treats a short read as a
  failed install.
- Do not sort, merge or "repair" the section list. Overlapping sections are tolerated on purpose
  (`if (end <= frontier) continue;`), update files can have thousands of entries.

## Git workflow

- One commit. Subject style of this repository: area first, for example
  `nup: extract archives and edit their metadata in place`. Suggested subject:
  `ncz: fail a solid stream whose sections leave a gap`.
- The body says what went wrong (frontier ahead of the decoded bytes) and how it was tested.
- Do NOT push unless the operator told you to or you work under this repository's `AGENTS.md`.

## Steps

### Step 1: Add a helper and a passing test for a normal solid NCZ

In `src/tests/core/ncz_decompressor.cpp`, in the anonymous namespace after `MakeRawNcz`, add a
builder for solid files. `sections` is a list of `(offset, size)` pairs:

```cpp
// A solid NCZ: header, section table, then one zstd stream over the whole body.
SyntheticNcz MakeSolidNcz(std::span<const std::pair<u64, u64>> sections, std::size_t body_size) {
    SyntheticNcz out;
    out.header.assign(NcaHeaderSize, 0x5A);
    out.plain_body.resize(body_size);
    for (std::size_t i = 0; i < body_size; ++i) {
        out.plain_body[i] = static_cast<u8>(i * 13 + 1);
    }
    out.ncz = out.header;
    PutMagic(out.ncz, "NCZSECTN");
    PutLe(out.ncz, sections.size(), 8);
    for (const auto& [offset, size] : sections) {
        PutSection(out.ncz, offset, size, 1);
    }
    const auto stream =
        Common::Compression::CompressDataZSTDDefault(out.plain_body.data(), body_size);
    REQUIRE(!stream.empty());
    out.ncz.insert(out.ncz.end(), stream.begin(), stream.end());
    return out;
}
```

Add `#include <span>` to the includes at the top of the file, directly after `#include <memory>`.

Then add this test case at the end of the file (inside nothing; the other test cases are at file
scope):

```cpp
TEST_CASE("Solid NCZ decodes adjacent sections for install", "[core][ncz]") {
    const std::array<std::pair<u64, u64>, 2> sections{{
        {NcaHeaderSize, SectionSize},
        {NcaHeaderSize + SectionSize, SectionSize},
    }};
    const auto synthetic = MakeSolidNcz(sections, 2 * SectionSize);
    const auto source = std::make_shared<FileSys::VectorVfsFile>(synthetic.ncz, "solid.ncz");
    REQUIRE(Core::Crypto::DecompressNCZ(source, "solid.nca") == nullptr); // install only
    const auto nca = Core::Crypto::DecompressNCZForInstall(source, "solid.nca");
    REQUIRE(nca != nullptr);
    std::vector<u8> expected = synthetic.header;
    expected.insert(expected.end(), synthetic.plain_body.begin(), synthetic.plain_body.end());
    REQUIRE(nca->GetSize() == expected.size());
    REQUIRE(nca->ReadAllBytes() == expected);
}
```

**Verify**: `cmake --build build-local --target tests --parallel 6 && ./build-local/bin/tests "Solid NCZ decodes adjacent sections for install"`
→ `All tests passed`. This test must pass BEFORE any change to `ncz_decompressor.cpp`. If it
fails, STOP: the file format assumptions of this plan are wrong.

### Step 2: Add the failing test for a gap

Add a second test case:

```cpp
TEST_CASE("Solid NCZ with a gap between sections stops instead of reading past its data",
          "[core][ncz]") {
    // The second section starts one section size too late.
    const std::array<std::pair<u64, u64>, 2> sections{{
        {NcaHeaderSize, SectionSize},
        {NcaHeaderSize + 2 * SectionSize, SectionSize},
    }};
    const auto synthetic = MakeSolidNcz(sections, 2 * SectionSize);
    const auto source = std::make_shared<FileSys::VectorVfsFile>(synthetic.ncz, "gap.ncz");
    const auto nca = Core::Crypto::DecompressNCZForInstall(source, "gap.nca");
    REQUIRE(nca != nullptr);
    // The part before the gap is still readable.
    REQUIRE(nca->ReadBytes(SectionSize, NcaHeaderSize) ==
            Slice(synthetic.plain_body, 0, SectionSize));
    // Nothing at or behind the gap is.
    REQUIRE(nca->ReadBytes(SectionSize, NcaHeaderSize + SectionSize).empty());
    REQUIRE(nca->ReadBytes(SectionSize, NcaHeaderSize + 2 * SectionSize).empty());
    REQUIRE(nca->ReadBytes(1, NcaHeaderSize + 3 * SectionSize - 1).empty());
    // And the reader still serves the good part afterwards.
    REQUIRE(nca->ReadBytes(SectionSize, NcaHeaderSize) ==
            Slice(synthetic.plain_body, 0, SectionSize));
}
```

`Slice` already exists in the test file.

Then add a third test case. It pins something the current code tolerates and the fix must keep:
a section entry with size 0 holds no bytes, so it cannot leave a gap, wherever its offset points.

```cpp
TEST_CASE("Solid NCZ skips an empty section entry that lies ahead", "[core][ncz]") {
    const std::array<std::pair<u64, u64>, 3> sections{{
        {NcaHeaderSize, SectionSize},
        {NcaHeaderSize + 2 * SectionSize, 0},
        {NcaHeaderSize + SectionSize, SectionSize},
    }};
    const auto synthetic = MakeSolidNcz(sections, 2 * SectionSize);
    const auto source = std::make_shared<FileSys::VectorVfsFile>(synthetic.ncz, "empty.ncz");
    const auto nca = Core::Crypto::DecompressNCZForInstall(source, "empty.nca");
    REQUIRE(nca != nullptr);
    std::vector<u8> expected = synthetic.header;
    expected.insert(expected.end(), synthetic.plain_body.begin(), synthetic.plain_body.end());
    REQUIRE(nca->ReadAllBytes() == expected);
}
```

**Verify**: `cmake --build build-local --target tests --parallel 6 && ./build-local/bin/tests "[ncz]"`
→ exactly one test case fails: the gap test, at its first `.empty()` requirement
(`ReadBytes(SectionSize, NcaHeaderSize + SectionSize)` returns data). No crash is expected; the
read past the buffer only happens on a later read. The empty-section test and all other `[ncz]`
tests pass. If the gap test passes before the fix, or the empty-section test fails, STOP.

### Step 3: Fix `DecodeTo`

In `src/core/crypto/ncz_decompressor.cpp`, in `SolidNczFile::DecodeTo`, in the second `for` loop,
directly after the line `if (end <= frontier) continue;` add:

```cpp
            // The stream holds the body without holes, so a section with bytes that starts
            // behind the decoded end cannot be placed: `data` would fall behind `frontier`.
            if (begin > frontier && begin < end) {
                LOG_ERROR(Crypto, "Solid NCZ: gap before the section at {:#x}", begin);
                return false;
            }
```

`begin < end` means the section has bytes (`end` is never below `begin`, see the two lines above
the check). An empty entry is left to the existing code, which does nothing for it.

Match the indentation of the surrounding lines. The next line, `u64 pos = std::max<u64>(begin, frontier);`,
stays as it is.

**Verify**: `cmake --build build-local --target tests --parallel 6 && ./build-local/bin/tests "[ncz]"`
→ `All tests passed`.

### Step 4: Whole suite and mutation check

**Verify**: `./build-local/bin/tests` → `All tests passed`.

**Verify**: `orchid-agent-test --build-dir build-local -- "[ncz]"` → exit 0. On exit 1 read the
report under `~/Orchid/orchid-issues/mutation/<run>/`, add the assertion that kills the surviving
mutant, and run again. Never lower the threshold.

## Test plan

- `src/tests/core/ncz_decompressor.cpp` gains the helper `MakeSolidNcz` and three test cases:
  the normal solid file (first coverage of `SolidNczFile` at all), the gap file (the regression)
  and the empty section entry (behaviour the fix must not change).
- Pattern: the existing test `"NCZ block cache recovers after a colliding block fails to decode"`
  in the same file, which also checks that a failed read returns an empty vector and that good
  data is still served afterwards.
- Verification: `./build-local/bin/tests "[ncz]"` → `All tests passed (... in 7 test cases)`: the 4 existing ones and 3 new ones.

## Done criteria

ALL must hold:

- [ ] `./build-local/bin/tests "[ncz]"` prints `All tests passed` with 7 test cases
- [ ] `./build-local/bin/tests` prints `All tests passed`
- [ ] `orchid-agent-test --build-dir build-local -- "[ncz]"` exits 0 (or the report states the tool is not installed)
- [ ] `grep -c "Solid NCZ: gap before the section" src/core/crypto/ncz_decompressor.cpp` prints `1`
- [ ] `git status --short` lists only the two source files and `plans/README.md`. The `plans/` folder may show as untracked (`?? plans/`); that is expected.
- [ ] `plans/README.md` status row updated

## STOP conditions

Stop and report back (do not improvise) if:

- `SolidNczFile` or `DecodeTo` does not match the excerpts in "Current state".
- The step 1 test (normal solid file) fails before any production change.
- The step 2 gap test passes before the fix, or the step 2 empty-section test fails before or
  after the fix.
- After the fix a test outside `[ncz]` fails.
- You think the right fix is to zero-fill the gap or to reorder sections. That is a format
  decision for the maintainer, not part of this plan.

## Maintenance notes

- Invariant to keep: inside `SolidNczFile`, `data.size() == frontier` at all times. Any future
  change to `DecodeTo` (for example decoding to a temporary file instead of memory) must keep
  `Read` from trusting `frontier` alone.
- `GetSize()` adds `s.offset + s.size` as signed 64-bit values taken from the file. A hostile
  table can make it huge; reads then just fail in `DecodeTo`. No allocation depends on it.
- The solid decoder holds the whole decoded NCA in memory. That is existing behaviour and only
  used during install; it is not changed here.
- The section table is walked in file order and is not required to be sorted. A table whose
  sections with bytes are out of order never decoded correctly and is now refused with the gap
  message.
- If a real file from the `nsz` tool is ever found with a gap, the log line added here names the
  offset; the decision then is whether such gaps mean zero bytes.

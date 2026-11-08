// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "common/common_types.h"

// Packs a loose dump into a NUP container. Every package is kept: base/, each
// updates/<version>/ and each dlc/<titleid>/<n>/.
namespace FileSys::Nup {

struct PackProgress {
    u64 bytes_done{};
    u64 bytes_total{};
    u64 files_done{};
    u64 files_total{};
};

enum class PackResult { Success, Cancelled, InvalidSource, IoError };

// NupContainer keeps a zstd file in memory only while it fits half of its 512 MiB decompression
// cache; a larger one is decompressed to a temporary file on first read, which costs time and
// disk space. Files above this size are therefore stored raw.
constexpr u64 DefaultRawThreshold = 256ULL * 1024 * 1024;

// Application metadata written to the base package (or the first package when there is no base).
// In PackOptions an empty field keeps what the dump supplies; ProbeMetadata fills all of them.
struct Metadata {
    std::string title_name;
    // JPEG or PNG bytes; 256x256 JPEG as distributed on-console.
    std::vector<u8> icon;
    std::string display_version;
    // Where ProbeMetadata found display_version, e.g. "update 458752" or "base".
    std::string version_source;
};

struct PackOptions {
    // Low-storage mode: delete each source file once its extent is synced and journaled.
    bool delete_sources = false;
    u64 chunk_bytes = 256ULL * 1024 * 1024;
    int zstd_level = 3;
    // Files larger than this are always stored raw.
    u64 raw_threshold = DefaultRawThreshold;
    // Overrides for the name, icon and display version found in the dump.
    Metadata metadata;
};

// A loose dump holds exefs/main and main.npdm, directly or under base/ or updates/<version>/,
// and a romfs folder. Folder names are matched case-insensitively.
bool IsLooseDump(const std::filesystem::path& dir);
// The dump folder a game list entry's exefs/main (or exefs folder) belongs to, if it is a loose
// dump.
std::optional<std::filesystem::path> DumpRootFromMain(const std::filesystem::path& main);
// Reads the name, icon and display version a dump would be packed with. The version comes from
// the newest update that reports one, else the base.
Metadata ProbeMetadata(const std::filesystem::path& dump_root);
std::filesystem::path DefaultArchivePath(const std::filesystem::path& dump_root);

// Writes <archive>.part and renames it when done; an existing archive is never overwritten. A
// cancelled or failed low-storage run keeps its .part and .journal, and calling this again with
// the same arguments resumes it.
PackResult PackDump(const std::filesystem::path& dump_root, const std::filesystem::path& archive,
                    const PackOptions& options = {},
                    const std::function<void(const PackProgress&)>& progress = {},
                    const std::atomic_bool* cancel = nullptr, std::string* error_out = nullptr);

bool HasResumableJournal(const std::filesystem::path& archive);

// Writes every package of a NUP back to the folder layout PackDump reads: base/,
// updates/<version>/ and dlc/<titleid>/<version>/. Each file is checked against its stored
// SHA-256. `output` must be missing or empty; a failed or cancelled run removes what it wrote.
PackResult ExtractNup(const std::filesystem::path& archive, const std::filesystem::path& output,
                      const std::function<void(const PackProgress&)>& progress = {},
                      const std::atomic_bool* cancel = nullptr, std::string* error_out = nullptr);
// Where ExtractNup writes by default: the archive's name without .nup, next to it.
std::filesystem::path DefaultExtractPath(const std::filesystem::path& archive);

// The name, icon and display version the game list shows for a NUP.
std::optional<Metadata> ReadNupMetadata(const std::filesystem::path& archive);

// Changes a NUP's metadata the way PackOptions::metadata does when packing; empty fields keep
// what is stored. File data is kept as is: the archive is copied to <archive>.part (a reflink
// where the filesystem allows it), only its TOC and metadata are rewritten, and the copy
// replaces the archive once it parses.
PackResult EditNupMetadata(const std::filesystem::path& archive, const Metadata& metadata,
                           const std::function<void(const PackProgress&)>& progress = {},
                           const std::atomic_bool* cancel = nullptr,
                           std::string* error_out = nullptr);

} // namespace FileSys::Nup

// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string_view>
#include "common/logging.h"
#include "common/random.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/file_sys/common_funcs.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/romfs_factory.h"
#include "core/file_sys/vfs/vfs_layered.h"
#include "core/hle/kernel/k_page_table.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/deconstructed_rom_directory.h"
#include "core/loader/nso.h"

#ifdef HAS_NCE
#include "core/arm/nce/patcher.h"
#endif

#ifndef HAS_NCE
namespace Core::NCE {
class Patcher {};
} // namespace Core::NCE
#endif

namespace Loader {

struct PatchCollection {
    explicit PatchCollection(bool is_application_) : is_application{is_application_} {
        module_patcher_indices.fill(-1);
        patchers.emplace_back();
    }

    std::vector<Core::NCE::Patcher>* GetPatchers() {
        if (is_application && Settings::IsNceEnabled()) {
            return &patchers;
        }
        return nullptr;
    }

    size_t GetTotalPatchSize() const {
        size_t total_size{};
#ifdef HAS_NCE
        for (auto& patcher : patchers) {
            total_size += patcher.GetSectionSize();
        }
#endif
        return total_size;
    }

    void SaveIndex(size_t module) {
        module_patcher_indices[module] = static_cast<s32>(patchers.size() - 1);
    }

    s32 GetIndex(size_t module) const {
        return module_patcher_indices[module];
    }

    s32 GetLastIndex() const {
        return static_cast<s32>(patchers.size()) - 1;
    }

    bool is_application;
    std::vector<Core::NCE::Patcher> patchers;
    std::array<s32, 13> module_patcher_indices{};
};

namespace {

FileSys::VirtualDir FindLooseRomFSDirectory(const FileSys::VirtualDir& parent) {
    if (parent == nullptr) {
        return nullptr;
    }
    if (const auto dir = parent->GetSubdirectory("romfs")) {
        return dir;
    }
    for (const auto& dir : parent->GetSubdirectories()) {
        const auto name = dir->GetName();
        if (name.size() == 5 && std::equal(name.begin(), name.end(), "romfs", [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) == b;
            })) {
            return dir;
        }
    }
    return nullptr;
}

// Size of the main NSO inside an ExeFS directory (0 when absent); lets update tiers be
// skipped when they contain data of a different build than the running program.
u64 ExeFSMainSize(const FileSys::VirtualDir& dir) {
    if (dir == nullptr) {
        return 0;
    }
    for (const auto& candidate : dir->GetFiles()) {
        if (candidate->GetName() == "main") {
            return candidate->GetSize();
        }
    }
    return 0;
}

// Update version folders under an updates/ (or upgrade/) storage folder, highest version
// first; names that are not pure numbers (e.g. a title id) trail below every version as
// a last resort.
std::vector<std::pair<u64, FileSys::VirtualDir>> OrderedUpdateVersionDirs(
    const FileSys::VirtualDir& storage_dir) {
    std::vector<std::pair<u64, FileSys::VirtualDir>> out;
    if (storage_dir == nullptr) {
        return out;
    }
    for (const auto& candidate : storage_dir->GetSubdirectories()) {
        const std::string name = candidate->GetName();
        u64 version{0};
        const auto parsed = std::from_chars(name.data(), name.data() + name.size(), version);
        // from_chars stops at the first non digit and still reports success, so a
        // directory called "655360_old" or "999999.bak" would be accepted with its
        // leading digits. Require the whole name to be the number; anything else
        // only serves when no numeric version exists at all.
        if (parsed.ec != std::errc{} || parsed.ptr != name.data() + name.size()) {
            version = 0;
        }
        out.emplace_back(version, candidate);
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });
    return out;
}

// Loose ExeFS dumps carry no control data, but icon/nacp sidecars can be dropped next to the
// ExeFS or in the game folder above it (<game>/exefs/main, <game>/base/exefs/main and
// <game>/updates/<title id>/exefs/main layouts).
std::vector<FileSys::VirtualDir> FindSidecarDirs(const FileSys::VirtualDir& exefs_dir,
                                              bool prefer_edition = false) {
    const auto equals_ignore_case = [](const std::string& a, std::string_view b) {
        return a.size() == b.size() &&
               std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                   return std::tolower(static_cast<unsigned char>(x)) ==
                          std::tolower(static_cast<unsigned char>(y));
               });
    };

    std::vector<FileSys::VirtualDir> dirs{exefs_dir};
    auto dir = exefs_dir;
    for (std::size_t depth = 0; depth < 8; ++depth) {
        const auto parent = dir->GetParentDirectory();
        if (parent == nullptr) {
            break;
        }
        const auto& name = parent->GetName();
        if (const auto control = parent->GetSubdirectory("control")) {
            if (prefer_edition) {
                if (const auto edition = control->GetSubdirectory("Acd_1")) {
                    dirs.push_back(edition);
                }
            }
            dirs.push_back(control);
        }
        dirs.push_back(parent);
        const bool storage_level =
            equals_ignore_case(name, "base") || equals_ignore_case(name, "update") ||
            equals_ignore_case(name, "updates") || equals_ignore_case(name, "upgrade") ||
            equals_ignore_case(name, "program") ||
            // A consolidated cross-generation layout keeps the package next to the
            // version folder instead of inside a program/ wrapper (`updates/<version>/
            // program1`). It plays the same role, so the walk continues through it to
            // hook the update and base RomFS tiers.
            equals_ignore_case(name, "program1") ||
            (!name.empty() && name.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos);
        if (!storage_level) {
            break;
        }
        dir = parent;
    }
    return dirs;
}

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) ==
                      std::tolower(static_cast<unsigned char>(y));
           });
}

// The base/ folder of the dump holding exefs_dir: on the sidecar chain itself
// (base/program/1/... dumps) or next to the storage root.
FileSys::VirtualDir FindLooseBaseDirectory(const FileSys::VirtualDir& exefs_dir) {
    const auto sidecars = FindSidecarDirs(exefs_dir);
    for (const auto& dir : sidecars) {
        if (EqualsIgnoreCase(dir->GetName(), "base")) {
            return dir;
        }
    }
    const auto& storage = sidecars.back();
    return storage != nullptr ? storage->GetSubdirectory("base") : nullptr;
}

// Program index 0 of a running consolidated Switch 2 Edition package
// (<version>/program1 or <version>/program/program1): the Switch 1 program of the same
// version, whose RomFS is that version's romfs/ over base/romfs. A wrapped package uses a
// program/0 sibling when the dump has one. nullptr for any other program.
FileSys::VirtualDir FindEditionProgramZeroRomFS(const FileSys::VirtualDir& exefs_dir) {
    const auto package = exefs_dir != nullptr ? exefs_dir->GetParentDirectory() : nullptr;
    if (package == nullptr || !EqualsIgnoreCase(package->GetName(), "program1")) {
        return nullptr;
    }
    auto version = package->GetParentDirectory();
    if (version != nullptr && EqualsIgnoreCase(version->GetName(), "program")) {
        if (const auto zero = FindLooseRomFSDirectory(version->GetSubdirectory("0"))) {
            return zero;
        }
        version = version->GetParentDirectory();
    }
    std::vector<FileSys::VirtualDir> tiers;
    for (const auto& tier : {FindLooseRomFSDirectory(version),
                             FindLooseRomFSDirectory(FindLooseBaseDirectory(exefs_dir))}) {
        if (tier != nullptr) {
            tiers.push_back(tier);
        }
    }
    if (tiers.size() <= 1) {
        return tiers.empty() ? nullptr : tiers.front();
    }
    return FileSys::LayeredVfsDirectory::MakeLayeredDirectory(std::move(tiers));
}

FileSys::VirtualDir FindSubdirectory(const FileSys::VirtualDir& parent, std::string_view name) {
    if (parent == nullptr) {
        return nullptr;
    }
    if (const auto direct = parent->GetSubdirectory(std::string{name})) {
        return direct;
    }
    for (const auto& subdirectory : parent->GetSubdirectories()) {
        if (EqualsIgnoreCase(subdirectory->GetName(), name)) {
            return subdirectory;
        }
    }
    return nullptr;
}

FileSys::VirtualFile PackageMainFile(const FileSys::VirtualDir& package) {
    const auto exefs = FindSubdirectory(package, "exefs");
    if (exefs == nullptr || exefs->GetFile("main.npdm") == nullptr) {
        return nullptr;
    }
    return exefs->GetFile("main");
}

FileSys::VirtualFile ProgramIndexMain(const FileSys::VirtualDir& programs, std::string_view index) {
    return PackageMainFile(FindSubdirectory(programs, index));
}

// program1 can be consolidated directly in a version folder, nested in program/, or represented
// as numbered program/1. Resolve the same shapes regardless of whether `package` is the version
// folder or the program wrapper.
FileSys::VirtualFile EditionMainInPackage(const FileSys::VirtualDir& package) {
    if (const auto direct = PackageMainFile(FindSubdirectory(package, "program1"))) {
        return direct;
    }
    const auto programs = EqualsIgnoreCase(package != nullptr ? package->GetName() : "", "program")
                              ? package
                              : FindSubdirectory(package, "program");
    if (const auto wrapped = PackageMainFile(FindSubdirectory(programs, "program1"))) {
        return wrapped;
    }
    return ProgramIndexMain(programs, "1");
}

FileSys::VirtualFile Switch1MainInPackage(const FileSys::VirtualDir& package) {
    const auto programs = EqualsIgnoreCase(package != nullptr ? package->GetName() : "", "program")
                              ? package
                              : FindSubdirectory(package, "program");
    if (const auto wrapped = ProgramIndexMain(programs, "0")) {
        return wrapped;
    }
    return PackageMainFile(package);
}

bool IsWithinDirectory(FileSys::VirtualDir dir, const FileSys::VirtualDir& parent) {
    if (parent == nullptr) {
        return false;
    }
    const auto parent_path = parent->GetFullPath();
    while (dir != nullptr) {
        if (dir->GetFullPath() == parent_path) {
            return true;
        }
        dir = dir->GetParentDirectory();
    }
    return false;
}

} // namespace

FileSys::VirtualFile FindLooseProgramMain(const FileSys::VirtualFile& main,
                                          std::size_t program_index, bool* base_romfs_only) {
    if (base_romfs_only != nullptr) {
        *base_romfs_only = false;
    }
    const auto exefs = main != nullptr ? main->GetContainingDirectory() : nullptr;
    const auto package = exefs != nullptr ? exefs->GetParentDirectory() : nullptr;
    if (package == nullptr) {
        return nullptr;
    }
    const auto package_parent = package->GetParentDirectory();
    const bool numbered_program = package_parent != nullptr &&
                                  EqualsIgnoreCase(package_parent->GetName(), "program");
    const bool current_edition = EqualsIgnoreCase(package->GetName(), "program1") ||
                                 (numbered_program && EqualsIgnoreCase(package->GetName(), "1"));

    if (program_index == 0) {
        if (!current_edition) {
            // A program already selected as index 0 stays selected on an explicit same-index
            // restart. If it is the base program, however, find the edition update's matching
            // Switch 1 executable or mark that its RomFS must remain base-only.
            const auto base = FindLooseBaseDirectory(exefs);
            if (!IsWithinDirectory(exefs, base)) {
                return main;
            }
            const auto storage = FindSidecarDirs(exefs).back();
            for (const auto name : {std::string_view{"upgrade"}, std::string_view{"updates"}}) {
                const auto updates = FindSubdirectory(storage, name);
                for (const auto& [version, version_dir] : OrderedUpdateVersionDirs(updates)) {
                    if (EditionMainInPackage(version_dir) == nullptr) {
                        continue;
                    }
                    if (const auto switch1 = Switch1MainInPackage(version_dir)) {
                        return switch1;
                    }
                    if (base_romfs_only != nullptr) {
                        *base_romfs_only = true;
                    }
                    return main;
                }
            }
            return main;
        }

        // The Switch 1 program of the same version: a program/0 sibling, the version folder's
        // own exefs/, or the base program when the dump keeps no updated one.
        auto version = numbered_program ? package_parent->GetParentDirectory()
                                        : package->GetParentDirectory();
        if (numbered_program) {
            if (const auto zero = ProgramIndexMain(package_parent, "0")) {
                return zero;
            }
        } else if (const auto programs = FindSubdirectory(version, "program")) {
            if (const auto zero = ProgramIndexMain(programs, "0")) {
                return zero;
            }
        }
        if (const auto legacy = PackageMainFile(version)) {
            return legacy;
        }
        const auto base = Switch1MainInPackage(FindLooseBaseDirectory(exefs));
        if (base != nullptr && base_romfs_only != nullptr) {
            *base_romfs_only = true;
        }
        return base;
    }

    if (program_index == 1) {
        if (current_edition) {
            return main;
        }
        if (const auto edition = EditionMainInPackage(package)) {
            return edition;
        }
        if (numbered_program) {
            if (const auto edition = EditionMainInPackage(package_parent)) {
                return edition;
            }
        }
        const auto storage = FindSidecarDirs(exefs).back();
        // Match ResolveLooseProgramPath: upgrade/ is checked before updates/, and each storage
        // tier is searched newest-first. EditionMainInPackage handles direct and wrapped forms.
        for (const auto name : {std::string_view{"upgrade"}, std::string_view{"updates"}}) {
            const auto updates = FindSubdirectory(storage, name);
            for (const auto& [version, version_dir] : OrderedUpdateVersionDirs(updates)) {
                if (const auto edition = EditionMainInPackage(version_dir)) {
                    return edition;
                }
            }
        }
    }
    return nullptr;
}

namespace {

// RomFS for a loose ExeFS, mounted as a cascading fallback over the dump's tiers:
//
//   1. program tier: <package>/romfs, the companion folder of the running exefs/
//      folder (program/1/romfs for Switch 2 Edition dumps, often structurally empty
//      because a cross-gen upgrade only adds code)
//   2. update tier:  updates/<newest version>/romfs (the legacy upgrade/ alias works too)
//   3. base tier:    base/romfs, the foundational application data
//
// The tiers mount into one tree with LayeredVfsDirectory: each lookup checks the tiers in
// order, and only reports nullptr (an asset failure) once no tier holds the path. Stage
// data of Switch 2 Edition titles lives in the update or base trees, so a placeholder
// program romfs must never act as an isolated volume root. Nothing is restructured or
// copied anywhere: the layered mount aliases the tier files and the built image keeps
// lazy views into them.
//
// Exception: a consolidated program1 package with its own romfs/ and a program 0 beside it
// mounts that romfs alone, as on hardware; the game reaches the Switch 1 data through
// program index 0 (FindLooseSiblingPrograms). Games probe for this: Super Mario Bros. Wonder
// mounts program 0 and its upgrade pack only when content:/System/AddressTable is missing,
// and without them its Switch 2 heap table is absent and boot ends in a null allocation.
FileSys::VirtualFile FindLooseRomFS(const FileSys::VirtualDir& exefs_dir,
                                   bool base_romfs_only = false) {
    if (exefs_dir == nullptr) {
        return nullptr;
    }
    for (const auto& f : exefs_dir->GetFiles()) {
        if (f->GetExtension() == "romfs") {
            return f;
        }
    }

    const auto parent = exefs_dir->GetParentDirectory();
    if (parent == nullptr) {
        return nullptr;
    }

    const auto equals_ignore_case = [](const std::string& a, std::string_view b) {
        return a.size() == b.size() &&
               std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                   return std::tolower(static_cast<unsigned char>(x)) ==
                          std::tolower(static_cast<unsigned char>(y));
               });
    };

    // 1. Program tier: the romfs/ extraction folder next to the running exefs/ folder.
    FileSys::VirtualDir program_romfs = FindLooseRomFSDirectory(parent);
    const bool own_volume =
        program_romfs != nullptr && FindEditionProgramZeroRomFS(exefs_dir) != nullptr;

    // 2. and 3. live at the storage root of the game, which the sidecar walk finds for
    // both <game>/program/1/... and <game>/base/program/1/... dump layouts.
    FileSys::VirtualDir update_romfs;
    FileSys::VirtualDir base_dir;
    const auto sidecars = FindSidecarDirs(exefs_dir);
    const auto& storage = sidecars.back();
    if (storage != nullptr && !own_volume) {
        // 2. Update tier: newest version folder holding a romfs. A tier is skipped, not
        // fatal, when its build differs from the running program: running a 1.0 program
        // against 2.0.1 data boots and then writes to unmapped memory in a loop
        // (Pokopia: 2012 invalid byte writes). With tiers below, the next tier can still
        // backfill its own data instead of refusing the RomFS outright.
        if (!base_romfs_only) {
            for (const auto name : {std::string_view{"updates"}, std::string_view{"upgrade"}}) {
                const auto update_storage = storage->GetSubdirectory(name);
                bool found_update = false;
                for (const auto& [version, version_dir] :
                     OrderedUpdateVersionDirs(update_storage)) {
                    const auto candidate = FindLooseRomFSDirectory(version_dir);
                    if (candidate == nullptr) {
                        continue;
                    }
                    const auto update_main_size =
                        ExeFSMainSize(version_dir->GetSubdirectory("exefs"));
                    const auto running_main_size = ExeFSMainSize(exefs_dir);
                    // A program kept inside this version folder (updates/<version>/program1) is
                    // the native edition built on top of that update's data, so its RomFS tier
                    // always belongs; the size check only guards a program from elsewhere.
                    // The wrapped layout updates/<version>/program/program1 has one more level.
                    auto program_dir = parent->GetParentDirectory();
                    if (program_dir != nullptr &&
                        equals_ignore_case(program_dir->GetName(), "program")) {
                        program_dir = program_dir->GetParentDirectory();
                    }
                    const bool own_version =
                        program_dir != nullptr && version_dir != nullptr &&
                        program_dir->GetFullPath() == version_dir->GetFullPath();
                    if (!own_version && running_main_size != 0 && update_main_size != 0 &&
                        running_main_size != update_main_size) {
                        LOG_ERROR(Loader,
                                  "Update RomFS (version {}) belongs to a different build "
                                  "(running main {} bytes, update main {} bytes); skipping the "
                                  "update tier",
                                  version, running_main_size, update_main_size);
                        continue;
                    }
                    update_romfs = candidate;
                    found_update = true;
                    break;
                }
                if (found_update) {
                    break;
                }
            }
        }

        // 3. Base tier.
        base_dir = FindLooseBaseDirectory(exefs_dir);
    }
    if (own_volume) {
        LOG_INFO(Loader, "Mounting the edition program RomFS {} on its own; program 0 serves the "
                         "Switch 1 data",
                 program_romfs->GetFullPath());
    }

    std::vector<FileSys::VirtualDir> tiers;
    const auto add_tier = [&tiers](const FileSys::VirtualDir& tier, const char* description) {
        if (tier == nullptr) {
            return;
        }
        if (std::find(tiers.begin(), tiers.end(), tier) != tiers.end()) {
            // The same folder can surface through several resolutions (e.g. a flat
            // base dump reaches the base tier through both routes).
            return;
        }
        LOG_INFO(Loader, "Layering the {} RomFS tier from {}", description, tier->GetFullPath());
        tiers.push_back(tier);
    };
    add_tier(program_romfs, "program");
    add_tier(update_romfs, "update");
    add_tier(FindLooseRomFSDirectory(base_dir), "base");

    if (tiers.empty()) {
        // Handing a null RomFS to the title makes OpenDataStorageByCurrentProcess fail
        // and the game panic with an opaque 0x3fffff instead of saying where the data is.
        LOG_ERROR(Loader, "No RomFS tier could be resolved from {} (program, update or base)",
                  exefs_dir->GetFullPath());
        return nullptr;
    }

    const std::size_t tier_count = tiers.size();

    // Building it walks every file and keeps an object for each (title 1: 206k files, ~150
    // MiB), so loaders of the same directory share one build while any of them holds it.
    static std::mutex cache_mutex;
    static std::map<std::string, std::weak_ptr<FileSys::VfsFile>> cache;
    std::scoped_lock lk{cache_mutex};

    // The cache key carries the tier count: one process can serve different fallback
    // compositions over the same program tier (update only vs. update + base).
    const auto mounted = FileSys::LayeredVfsDirectory::MakeLayeredDirectory(std::move(tiers));
    if (mounted == nullptr) {
        return nullptr;
    }
    const auto cache_key = fmt::format("layers={}:{}", tier_count, mounted->GetFullPath());
    auto& cached = cache[cache_key];
    if (auto built = cached.lock()) {
        return built;
    }

    LOG_INFO(Loader, "Composing the RomFS as a layered mount over {} tier(s)", tier_count);
    const auto started = std::chrono::steady_clock::now();
    auto built = FileSys::CreateRomFS(mounted);
    LOG_INFO(Loader, "Loose RomFS image ready: elapsed_ms={}",
             std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::steady_clock::now() - started)
                 .count());
    cached = built;
    return built;
}

// The newest three-digit tag in a directory's file names of the form <prefix>NNN.<anything>.
std::string NewestProductTag(const FileSys::VirtualDir& dir, std::string_view prefix) {
    std::string best;
    if (dir == nullptr) {
        return best;
    }
    for (const auto& file : dir->GetFiles()) {
        const std::string file_name = file->GetName();
        if (!file_name.starts_with(prefix) || file_name.size() < prefix.size() + 4) {
            continue;
        }
        const std::string_view digits = std::string_view{file_name}.substr(prefix.size(), 3);
        if (file_name[prefix.size() + 3] != '.' ||
            !std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; })) {
            continue;
        }
        if (std::string version{digits}; version > best) {
            best = version;
        }
    }
    return best;
}

// The display version of a dump without control data, from its RomFS: Nintendo EPD titles name
// their resource size tables after it (System/Resource/ResourceSizeTable.Product.180.* in Mario
// Kart World 1.8.0), and some name their game data list instead (GameData/GameDataList.Product.320.*
// in Donkey Kong Bananza 3.2.0). A hotfix can keep its minor version's tables (Product.110 in a
// 1.1.1).
std::string RomFSProductVersion(const FileSys::VirtualDir& romfs_dir) {
    if (romfs_dir == nullptr) {
        return {};
    }
    const auto system_dir = romfs_dir->GetSubdirectory("System");
    std::string best = NewestProductTag(
        system_dir != nullptr ? system_dir->GetSubdirectory("Resource") : nullptr,
        "ResourceSizeTable.Product.");
    if (const auto game_data = NewestProductTag(romfs_dir->GetSubdirectory("GameData"),
                                                "GameDataList.Product.");
        game_data > best) {
        best = game_data;
    }
    if (best.empty()) {
        return {};
    }
    return fmt::format("{}.{}.{}", best[0], best[1], best[2]);
}

} // namespace

// Extracted add-on content of a loose dump: <game>/dlc/<aoc title id>/<version>/romfs. Switch 2
// Edition upgrade packs ship this way (often just a placeholder file), and the game only unlocks
// once aoc:u lists them. Dumpers name the pack after the NX application (0100...) even when the
// program is the Switch 2 build (0400...), so the platform byte follows the running program.
std::vector<std::pair<u64, FileSys::VirtualDir>> FindLooseAOC(const FileSys::VirtualDir& exefs_dir,
                                                              u64 program_id) {
    std::vector<std::pair<u64, FileSys::VirtualDir>> out;
    const auto game_dir = FindSidecarDirs(exefs_dir).back();
    const auto dlc_dir = game_dir != nullptr ? game_dir->GetSubdirectory("dlc") : nullptr;
    if (dlc_dir == nullptr) {
        return out;
    }
    for (const auto& aoc_dir : dlc_dir->GetSubdirectories()) {
        const auto& name = aoc_dir->GetName();
        if (name.size() != 16 || name.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
            continue;
        }
        const u64 aoc_id = FileSys::WithPlatformOf(std::stoull(name, nullptr, 16), program_id);
        if (FileSys::GetBaseTitleID(aoc_id) != FileSys::GetBaseTitleID(program_id)) {
            continue;
        }
        // The newest version folder wins.
        FileSys::VirtualDir romfs_dir;
        u64 best_version = 0;
        for (const auto& version_dir : aoc_dir->GetSubdirectories()) {
            const auto& version = version_dir->GetName();
            if (version.empty() || version.find_first_not_of("0123456789") != std::string::npos) {
                continue;
            }
            auto candidate = version_dir->GetSubdirectory("romfs");
            if (candidate == nullptr) {
                const auto base = version_dir->GetSubdirectory("base");
                for (const auto& package : {version_dir, base}) {
                    if (package == nullptr) {
                        continue;
                    }
                    candidate = package->GetSubdirectory("romfs");
                    if (candidate == nullptr) {
                        const auto programs = package->GetSubdirectory("program");
                        const auto zero = programs != nullptr ? programs->GetSubdirectory("0")
                                                              : nullptr;
                        candidate = zero != nullptr ? zero->GetSubdirectory("romfs") : nullptr;
                    }
                    if (candidate != nullptr) {
                        break;
                    }
                }
            }
            // A patched add-on ships next to its base as <version>/updates/<patch>/program/0/romfs
            // (Xenoblade 2 keeps its whole game data in one). Dumps hold the full patched RomFS,
            // so the newest patch replaces the base instead of layering on it.
            if (const auto updates = version_dir->GetSubdirectory("updates")) {
                u64 best_patch = 0;
                for (const auto& patch_dir : updates->GetSubdirectories()) {
                    const auto& patch = patch_dir->GetName();
                    if (patch.empty() || patch.find_first_not_of("0123456789") != std::string::npos) {
                        continue;
                    }
                    const auto programs = patch_dir->GetSubdirectory("program");
                    const auto zero = programs != nullptr ? programs->GetSubdirectory("0") : nullptr;
                    const auto patched = zero != nullptr ? zero->GetSubdirectory("romfs") : nullptr;
                    if (patched != nullptr && std::stoull(patch) >= best_patch) {
                        candidate = patched;
                        best_patch = std::stoull(patch);
                    }
                }
            }
            if (candidate != nullptr && (romfs_dir == nullptr || std::stoull(version) >= best_version)) {
                romfs_dir = candidate;
                best_version = std::stoull(version);
            }
        }
        if (romfs_dir == nullptr) {
            continue;
        }
        LOG_INFO(Loader, "Loose add-on content {:016X} (v{}) from {}", aoc_id, best_version,
                 aoc_dir->GetFullPath());
        out.emplace_back(aoc_id, romfs_dir);
    }
    return out;
}

// The sibling programs of a loose multi-program dump: <...>/program/<n>/exefs is running, every
// other <...>/program/<m>/romfs is program index m of the same application.
std::vector<std::pair<u64, FileSys::VirtualDir>> FindLooseSiblingPrograms(
    const FileSys::VirtualDir& exefs_dir, u64 program_id) {
    std::vector<std::pair<u64, FileSys::VirtualDir>> out;
    if (auto zero = FindEditionProgramZeroRomFS(exefs_dir)) {
        out.emplace_back(FileSys::GetBaseTitleIDWithProgramIndex(program_id, 0), std::move(zero));
        return out;
    }
    const auto is_index = [](const std::string& name) {
        return !name.empty() && name.size() <= 3 &&
               name.find_first_not_of("0123456789") == std::string::npos;
    };
    const auto own = exefs_dir != nullptr ? exefs_dir->GetParentDirectory() : nullptr;
    const auto programs = own != nullptr ? own->GetParentDirectory() : nullptr;
    if (programs == nullptr || programs->GetName() != "program" || !is_index(own->GetName())) {
        return out;
    }
    for (const auto& sibling : programs->GetSubdirectories()) {
        const auto& name = sibling->GetName();
        if (!is_index(name) || name == own->GetName()) {
            continue;
        }
        if (const auto romfs = FindLooseRomFSDirectory(sibling)) {
            out.emplace_back(FileSys::GetBaseTitleIDWithProgramIndex(program_id, std::stoull(name)),
                             romfs);
        }
    }
    return out;
}

AppLoader_DeconstructedRomDirectory::AppLoader_DeconstructedRomDirectory(
    FileSys::VirtualFile file_, bool override_update_, bool base_romfs_only_)
    : AppLoader(std::move(file_))
    , override_update(override_update_)
    , base_romfs_only(base_romfs_only_)
{
    const auto file_dir = file->GetContainingDirectory();
    // Title ID
    const auto npdm = file_dir->GetFile("main.npdm");
    if (npdm != nullptr) {
        const auto res = metadata.Load(npdm);
        if (res == ResultStatus::Success)
            title_id = metadata.GetTitleID();
    }

    const auto sidecar_dirs = FindSidecarDirs(file_dir, metadata.IsSwitch2());

    // Icon
    FileSys::VirtualFile icon_file = nullptr;
    for (const auto& sidecar_dir : sidecar_dirs) {
        for (const auto& language : FileSys::LANGUAGE_NAMES) {
            icon_file = sidecar_dir->GetFile("icon_" + std::string(language) + ".dat");
            if (icon_file != nullptr) {
                break;
            }
        }
        if (icon_file != nullptr) {
            icon_data = icon_file->ReadAllBytes();
            break;
        }
    }

    if (icon_data.empty()) {
        // Any png, jpeg, or bmp file
        for (const auto& sidecar_dir : sidecar_dirs) {
            const auto& files = sidecar_dir->GetFiles();
            const auto icon_iter =
                std::find_if(files.begin(), files.end(), [](const FileSys::VirtualFile& f) {
                    return f->GetExtension() == "png" || f->GetExtension() == "jpg" ||
                           f->GetExtension() == "bmp" || f->GetExtension() == "jpeg";
                });
            if (icon_iter != files.end()) {
                icon_data = (*icon_iter)->ReadAllBytes();
                break;
            }
        }
    }

    // Metadata
    FileSys::VirtualFile nacp_file = nullptr;
    for (const auto& sidecar_dir : sidecar_dirs) {
        nacp_file = sidecar_dir->GetFile("control.nacp");
        if (nacp_file != nullptr) {
            break;
        }
        const auto& files = sidecar_dir->GetFiles();
        const auto nacp_iter =
            std::find_if(files.begin(), files.end(),
                         [](const FileSys::VirtualFile& f) { return f->GetExtension() == "nacp"; });
        if (nacp_iter != files.end()) {
            nacp_file = *nacp_iter;
            break;
        }
    }

    if (nacp_file != nullptr) {
        control.emplace(nacp_file);
        name = control->GetApplicationName();
    }

    romfs_exefs_dir = file_dir;
    if (const auto title_dir = file_dir->GetParentDirectory()) {
        romfs_version = RomFSProductVersion(FindLooseRomFSDirectory(title_dir));
    }
}

AppLoader_DeconstructedRomDirectory::AppLoader_DeconstructedRomDirectory(
    FileSys::VirtualDir directory, bool override_update_)
    : AppLoader(directory->GetFile("main"))
    , dir(std::move(directory))
    , override_update(override_update_)
{
    romfs_exefs_dir = dir;
}

// Loaders made only for metadata (the game list, title lookups) never need the RomFS, and several
// of them were alive with a whole copy each.
const FileSys::VirtualFile& AppLoader_DeconstructedRomDirectory::LooseRomFS() {
    if (!romfs_resolved) {
        romfs_resolved = true;
        romfs = FindLooseRomFS(romfs_exefs_dir, base_romfs_only);
    }
    return romfs;
}

FileType AppLoader_DeconstructedRomDirectory::IdentifyType(const FileSys::VirtualFile& dir_file) {
    if (FileSys::IsDirectoryExeFS(dir_file->GetContainingDirectory())) {
        return FileType::DeconstructedRomDirectory;
    }

    return FileType::Error;
}

AppLoader_DeconstructedRomDirectory::LoadResult AppLoader_DeconstructedRomDirectory::Load(
    Kernel::KProcess& process, Core::System& system) {
    if (is_loaded) {
        return {ResultStatus::ErrorAlreadyLoaded, {}};
    }

    // Loaded directly from a loose ExeFS (not wrapped by the NCA/NSP loaders, which register
    // the process with the filesystem controller themselves).
    const bool standalone = dir == nullptr;
    if (dir == nullptr) {
        if (file == nullptr) {
            return {ResultStatus::ErrorNullFile, {}};
        }

        dir = file->GetContainingDirectory();
    }

    // Read meta to determine title ID
    FileSys::VirtualFile npdm = dir->GetFile("main.npdm");
    if (npdm == nullptr) {
        return {ResultStatus::ErrorMissingNPDM, {}};
    }

    const ResultStatus result = metadata.Load(npdm);
    if (result != ResultStatus::Success) {
        return {result, {}};
    }

    const u64 addon_application_id = control && control->GetDLCBaseTitleId() != 0
                                         ? control->GetDLCBaseTitleId()
                                         : metadata.GetTitleID();
    std::vector<FileSys::BootContent> boot_content;
    if (standalone) {
        for (auto& [id, data] : FindLooseAOC(dir, addon_application_id)) {
            boot_content.push_back({id, FileSys::ContentRecordType::Data, std::move(data)});
        }
        for (auto& [id, program] : FindLooseSiblingPrograms(dir, metadata.GetTitleID())) {
            boot_content.push_back({id, FileSys::ContentRecordType::Program, std::move(program)});
        }
    }
    FileSys::SetBootContent(std::move(boot_content));

    if (override_update) {
        const FileSys::PatchManager patch_manager(
            metadata.GetTitleID(), system.GetFileSystemController(), system.GetContentProvider());
        dir = patch_manager.PatchExeFS(dir);
    }

    // Reread in case PatchExeFS affected the main.npdm
    npdm = dir->GetFile("main.npdm");
    if (npdm == nullptr) {
        return {ResultStatus::ErrorMissingNPDM, {}};
    }

    const ResultStatus result2 = metadata.Reload(npdm);
    if (result2 != ResultStatus::Success) {
        return {result2, {}};
    }
    metadata.Print();

    // Loose-ExeFS loads bypass the NCA/NSP loaders, which are the ones that normally apply
    // LayeredFS to the RomFS; apply it here so mods in user/load/<titleid>/ work too.
    if (LooseRomFS() != nullptr) {
        const FileSys::PatchManager patch_manager(metadata.GetTitleID(),
                                                  system.GetFileSystemController(),
                                                  system.GetContentProvider());
        auto patched = patch_manager.PatchRomFS(nullptr, romfs);
        if (patched != nullptr) {
            romfs = std::move(patched);
            LOG_INFO(Loader, "Applied RomFS patches (LayeredFS) for title {:016X}",
                     metadata.GetTitleID());
        }
    }

    // Enable NCE only for applications with 39-bit address space.
    const bool is_39bit =
        metadata.GetAddressSpaceType() == FileSys::ProgramAddressSpaceType::Is39Bit;
    const bool is_application = metadata.GetPoolPartition() == FileSys::PoolPartition::Application;
    Settings::SetNceEnabled(is_39bit);

    const std::array static_modules = {"rtld",    "main",    "subsdk0", "subsdk1", "subsdk2",
                                       "subsdk3", "subsdk4", "subsdk5", "subsdk6", "subsdk7",
                                       "subsdk8", "subsdk9", "sdk"};

    std::size_t code_size{};

    // Define an nce patch context for each potential module.
    PatchCollection patch_ctx{is_application};

    // Use the NSO module loader to figure out the code layout
    for (size_t i = 0; i < static_modules.size(); i++) {
        const auto& module = static_modules[i];
        const FileSys::VirtualFile module_file{dir->GetFile(module)};
        if (!module_file) {
            continue;
        }

        const bool should_pass_arguments = std::strcmp(module, "rtld") == 0;
        const auto tentative_next_load_addr = AppLoader_NSO::LoadModule(
            process, system, *module_file, code_size, should_pass_arguments, false, {},
            patch_ctx.GetPatchers(), patch_ctx.GetLastIndex());
        if (!tentative_next_load_addr) {
            return {ResultStatus::ErrorLoadingNSO, {}};
        }

        patch_ctx.SaveIndex(i);
        code_size = *tentative_next_load_addr;
    }

    // Enable direct memory mapping in case of NCE.
    const u64 fastmem_base = [&]() -> size_t {
        if (is_application && Settings::IsNceEnabled()) {
            auto& buffer = system.DeviceMemory().buffer;
            buffer.EnableDirectMappedAddress();
            return reinterpret_cast<u64>(buffer.VirtualBasePointer());
        }
        return 0;
    }();

    // Add patch size to the total module size
    code_size += patch_ctx.GetTotalPatchSize();

    // TODO: this is bad form of ASLR, it sucks
    std::uintptr_t aslr_offset = ((::Settings::values.rng_seed_enabled.GetValue()
        ? ::Settings::values.rng_seed.GetValue() : Common::Random::Random64(0)) << 12) & 0xfff000;

    // Setup the process code layout
    if (process.LoadFromMetadata(system.Kernel(), metadata, code_size, fastmem_base, aslr_offset).IsError()) {
        return {ResultStatus::ErrorUnableToParseKernelMetadata, {}};
    }

    // Load NSO modules
    modules.clear();
    const VAddr base_address{GetInteger(process.GetEntryPoint())};
    VAddr next_load_addr{base_address};
    const FileSys::PatchManager pm{metadata.GetTitleID(), system.GetFileSystemController(),
                                   system.GetContentProvider()};
    for (size_t i = 0; i < static_modules.size(); i++) {
        const auto& module = static_modules[i];
        const FileSys::VirtualFile module_file{dir->GetFile(module)};
        if (!module_file) {
            continue;
        }

        const VAddr load_addr{next_load_addr};
        const bool should_pass_arguments = std::strcmp(module, "rtld") == 0;
        const auto tentative_next_load_addr = AppLoader_NSO::LoadModule(
            process, system, *module_file, load_addr, should_pass_arguments, true, pm,
            patch_ctx.GetPatchers(), patch_ctx.GetIndex(i));
        if (!tentative_next_load_addr) {
            return {ResultStatus::ErrorLoadingNSO, {}};
        }

        next_load_addr = *tentative_next_load_addr;
        modules.insert_or_assign(load_addr, module);
        LOG_DEBUG(Loader, "loaded module {} @ {:#x}", module, load_addr);
    }

    if (standalone) {
        // Without this, fsp-srv SetCurrentProcess fails with ResultTargetNotFound.
        system.GetFileSystemController().RegisterProcess(
            process.GetProcessId(), metadata.GetTitleID(),
            std::make_shared<FileSys::RomFSFactory>(*this, system.GetContentProvider(),
                                                    system.GetFileSystemController()));
    }

    is_loaded = true;
    return {ResultStatus::Success,
            LoadParameters{metadata.GetMainThreadPriority(), metadata.GetMainThreadStackSize()}};
}

ResultStatus AppLoader_DeconstructedRomDirectory::ReadRomFS(FileSys::VirtualFile& out_dir) {
    if (LooseRomFS() == nullptr) {
        return ResultStatus::ErrorNoRomFS;
    }

    out_dir = romfs;
    return ResultStatus::Success;
}

ResultStatus AppLoader_DeconstructedRomDirectory::ReadIcon(std::vector<u8>& out_buffer) {
    if (icon_data.empty()) {
        return ResultStatus::ErrorNoIcon;
    }

    out_buffer = icon_data;
    return ResultStatus::Success;
}

ResultStatus AppLoader_DeconstructedRomDirectory::ReadProgramMetadata(FileSys::ProgramMetadata& out) {
    const auto npdm = dir != nullptr ? dir->GetFile("main.npdm") : nullptr;
    if (npdm == nullptr) {
        return ResultStatus::ErrorMissingNPDM;
    }

    if (const auto result = metadata.Load(npdm); result != ResultStatus::Success) {
        return result;
    }

    out = metadata;
    return ResultStatus::Success;
}

ResultStatus AppLoader_DeconstructedRomDirectory::ReadProgramId(u64& out_program_id) {
    out_program_id = title_id;
    return ResultStatus::Success;
}

ResultStatus AppLoader_DeconstructedRomDirectory::ReadTitle(std::string& out_title) {
    if (name.empty()) {
        return ResultStatus::ErrorNoControl;
    }

    out_title = name;
    return ResultStatus::Success;
}

ResultStatus AppLoader_DeconstructedRomDirectory::ReadControlData(FileSys::NACP& out_control) {
    if (!control) {
        return ResultStatus::ErrorNoControl;
    }
    out_control = *control;
    return ResultStatus::Success;
}

ResultStatus AppLoader_DeconstructedRomDirectory::ReadDisplayVersion(std::string& version) {
    if (control && !control->GetVersionString().empty()) {
        version = control->GetVersionString();
        return ResultStatus::Success;
    }
    if (!romfs_version.empty()) {
        version = romfs_version;
        return ResultStatus::Success;
    }
    return ResultStatus::ErrorNoControl;
}

bool AppLoader_DeconstructedRomDirectory::IsRomFSUpdatable() const {
    return false;
}

ResultStatus AppLoader_DeconstructedRomDirectory::ReadNSOModules(Modules& out_modules) {
    if (!is_loaded) {
        return ResultStatus::ErrorNotInitialized;
    }

    out_modules = this->modules;
    return ResultStatus::Success;
}

} // namespace Loader

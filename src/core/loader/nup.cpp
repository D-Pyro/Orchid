// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/loader/nup.h"

#include <algorithm>
#include <cstring>

#include "common/logging.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/file_sys/nup_container.h"
#include "core/file_sys/common_funcs.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/program_metadata.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/romfs_factory.h"
#include "core/file_sys/vfs/vfs_vector.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/deconstructed_rom_directory.h"

namespace Loader {

namespace {

using FileSys::NupContainer;

// NACP stores 16 uncompressed language entries; map BCP-47 tags onto them.
struct LanguageSlot {
    std::string_view tag;
    std::size_t index;
};

constexpr std::array<LanguageSlot, 18> LanguageSlots{{
    {"en-us", 0},   {"en-gb", 1},  {"ja", 2},     {"fr", 3},     {"de", 4},
    {"es-419", 5},  {"es", 6},     {"it", 7},     {"nl", 8},     {"fr-ca", 9},
    {"pt", 10},     {"ru", 11},    {"ko", 12},    {"zh-hant", 13}, {"zh-hans", 14},
    {"pt-br", 15},  {"pl", 16},    {"th", 17},
}};

void CopyNameToEntry(FileSys::LanguageEntry& entry, std::string_view name) {
    const std::size_t length = std::min(name.size(), entry.application_name.size() - 1);
    std::memcpy(entry.application_name.data(), name.data(), length);
    entry.application_name[length] = '\0';
}

void CopyDeveloperToEntry(FileSys::LanguageEntry& entry, std::string_view developer) {
    const std::size_t length = std::min(developer.size(), entry.developer_name.size() - 1);
    std::memcpy(entry.developer_name.data(), developer.data(), length);
    entry.developer_name[length] = '\0';
}

std::unique_ptr<FileSys::NACP> BuildNacp(const NupContainer::PackageInfo& base,
                                         const std::string& version) {
    if (!base.nacp.empty()) {
        return std::make_unique<FileSys::NACP>(
            std::make_shared<FileSys::VectorVfsFile>(base.nacp, "control.nacp"));
    }
    // A package without a title name is still named after the dump it was made from.
    const std::string& effective_title =
        !base.title_name.empty() ? base.title_name : base.source_dump;
    if (effective_title.empty() && base.localized_titles.empty() && base.publisher.empty() &&
        version.empty()) {
        return nullptr;
    }
    auto nacp = std::make_unique<FileSys::NACP>();
    nacp->raw.supported_languages = static_cast<FileSys::SupportedLanguage>(0x3FFFF);

    for (const auto& [lang, name] : base.localized_titles) {
        for (const auto& slot : LanguageSlots) {
            // The uncompressed RawNACP only stores 16 entries; Polish/Thai exist
            // only in the compressed format, so they fall back to the default.
            if (slot.index < 16 && slot.tag == lang) {
                CopyNameToEntry(nacp->raw.language_entries.language_entries[slot.index], name);
            }
        }
    }
    if (!effective_title.empty()) {
        CopyNameToEntry(nacp->raw.language_entries.language_entries[0], effective_title);
    }
    if (!base.publisher.empty()) {
        for (auto& entry : nacp->raw.language_entries.language_entries) {
            CopyDeveloperToEntry(entry, base.publisher);
        }
    }
    if (!version.empty()) {
        const std::size_t length = std::min(version.size(), nacp->raw.version_string.size() - 1);
        std::memcpy(nacp->raw.version_string.data(), version.data(), length);
        nacp->raw.version_string[length] = '\0';
    }
    nacp->raw.save_data_owner_id = base.title_id;

    nacp->language_entries.resize(16);
    std::memcpy(nacp->language_entries.data(), nacp->raw.language_entries.language_entries.data(),
                sizeof(nacp->raw.language_entries.language_entries));
    return nacp;
}

std::string DecodeVersion(u32 version) {
    if (version == 0) {
        return {};
    }
    return fmt::format("{}.{}.{}", version >> 16, (version >> 8) & 0xFF, version & 0xFF);
}

} // namespace

NupBootProgram SelectNupBootProgram(FileSys::NupContainer& nup,
                                    std::optional<std::size_t> switched_index) {
    NupBootProgram boot;
    const auto base_index = nup.FindBasePackage();
    boot.update_index = nup.FindEnabledUpdatePackage();

    // The program comes from the base package; a standalone update container
    // (SPEC §4.1.2) carries a complete patched tree and boots on its own.
    std::optional<std::size_t> exefs_index;
    if (base_index && nup.GetExeFSDir(*base_index) != nullptr) {
        boot.program_index = base_index;
        exefs_index = boot.update_index && nup.GetExeFSDir(*boot.update_index) != nullptr
                          ? boot.update_index
                          : base_index;
    } else if (boot.update_index && nup.GetExeFSDir(*boot.update_index) != nullptr) {
        LOG_INFO(Loader,
                 "NUP container is a standalone update; booting the update program directly");
        boot.program_index = boot.update_index;
        exefs_index = boot.update_index;
    }
    if (!exefs_index) {
        boot.program_index.reset();
        return boot;
    }
    boot.exefs = nup.GetExeFSDir(*exefs_index);
    boot.edition_romfs = nup.GetEditionRomFSDir(*exefs_index);

    // ExecuteProgram into index 0 of a Switch 2 Edition runs its Switch 1 program: the legacy
    // ExeFS packed next to program1, else the base program with the base data it was built for.
    if (switched_index == std::size_t{0} && boot.edition_romfs != nullptr) {
        boot.edition_romfs = nullptr;
        if (auto legacy = nup.GetLegacyExeFSDir(*exefs_index)) {
            boot.exefs = std::move(legacy);
        } else if (base_index && *base_index != *exefs_index) {
            boot.exefs = nup.GetExeFSDir(*base_index);
            boot.base_romfs_only = true;
        } else {
            boot.exefs = nullptr;
        }
        boot.switch1_program = boot.exefs != nullptr;
    }
    return boot;
}

AppLoader_NUP::AppLoader_NUP(FileSys::VirtualFile file_, std::optional<std::size_t> switched_index)
    : AppLoader(std::move(file_)), nup(std::make_unique<NupContainer>(file)) {
    if (nup->GetStatus() != ResultStatus::Success) {
        return;
    }

    auto boot = SelectNupBootProgram(*nup, switched_index);
    update_index = boot.update_index;
    if (!boot.program_index || boot.exefs == nullptr) {
        LOG_ERROR(Loader, "NUP container has no package with a usable ExeFS");
        return;
    }

    const auto& program = nup->GetPackages()[*boot.program_index];
    base_title_id = program.title_id;
    // A consolidated Switch 2 Edition keeps the Switch 1 package as the base and stores the
    // running program in the update. Use that package's ID for normal boots; the index-0 path
    // below replaces it with the legacy ExeFS's program ID.
    if (boot.edition_romfs != nullptr && boot.update_index) {
        base_title_id = nup->GetPackages()[*boot.update_index].title_id;
    }
    has_program = true;

    const auto exefs = boot.exefs;
    if (exefs->GetFile("main") == nullptr || exefs->GetFile("main.npdm") == nullptr) {
        LOG_ERROR(Loader, "NUP package has no usable ExeFS");
        return;
    }
    secondary_loader = std::make_unique<AppLoader_DeconstructedRomDirectory>(exefs, true);
    edition_romfs = std::move(boot.edition_romfs);
    base_romfs_only = boot.base_romfs_only;
    if (edition_romfs != nullptr) {
        LOG_INFO(Loader, "NUP Switch 2 Edition program: mounting program1/romfs on its own, "
                         "program 0 serves the merged base and update data");
    }
    if (boot.switch1_program) {
        // The Switch 1 program runs under its own program ID (its saves and add-ons).
        FileSys::ProgramMetadata metadata;
        base_title_id = metadata.Load(exefs->GetFile("main.npdm")) == ResultStatus::Success
                            ? metadata.GetTitleID()
                            : FileSys::GetBaseTitleIDWithProgramIndex(base_title_id, 0);
        LOG_INFO(Loader, "NUP program index 0: booting the Switch 1 program {:016X}{}",
                 base_title_id,
                 base_romfs_only ? " from the base package (the update ExeFS is not packed)" : "");
    }

    if (update_index) {
        const auto& update = nup->GetPackages()[*update_index];
        display_version = update.display_version.empty() ? DecodeVersion(update.version)
                                                         : update.display_version;
    } else {
        display_version = program.display_version.empty() ? DecodeVersion(program.version)
                                                          : program.display_version;
    }
    nacp_file = BuildNacp(program, display_version);
    if (!program.nacp.empty()) {
        title_override = program.title_name;
    }
    if (nacp_file != nullptr && !program.icon.empty()) {
        icon_file = std::make_shared<FileSys::VectorVfsFile>(program.icon, "icon.dat");
    } else if (nacp_file != nullptr) {
        for (const auto& [lang, icon] : program.localized_icons) {
            if (lang == "en-us") {
                icon_file = std::make_shared<FileSys::VectorVfsFile>(icon, "icon_" + lang + ".dat");
                break;
            }
        }
        if (icon_file == nullptr && !program.localized_icons.empty()) {
            icon_file = std::make_shared<FileSys::VectorVfsFile>(
                program.localized_icons.front().second, "icon.dat");
        }
    }
}

AppLoader_NUP::~AppLoader_NUP() = default;

FileType AppLoader_NUP::IdentifyType(const FileSys::VirtualFile& nup_file) {
    return FileSys::NupContainer::IsNupFile(nup_file) ? FileType::NUP : FileType::Error;
}

AppLoader::LoadResult AppLoader_NUP::Load(Kernel::KProcess& process, Core::System& system) {
    if (is_loaded) {
        return {ResultStatus::ErrorAlreadyLoaded, {}};
    }
    if (nup->GetStatus() != ResultStatus::Success) {
        return {nup->GetStatus(), {}};
    }
    if (!has_program || secondary_loader == nullptr) {
        return {ResultStatus::ErrorNUPNoBasePackage, {}};
    }

    const auto result = secondary_loader->Load(process, system);
    if (result.first != ResultStatus::Success) {
        return result;
    }

    // The secondary ExeFS loader resets loose content. Publish the NUP entries only
    // after it succeeds, before the guest can enumerate or open add-on content.
    // Switch 2 edition programs can retain their Switch 1 application's DLC IDs.
    const u64 addon_application_id = nacp_file && nacp_file->GetDLCBaseTitleId() != 0
                                         ? FileSys::GetBaseTitleID(nacp_file->GetDLCBaseTitleId())
                                         : base_title_id;
    std::vector<FileSys::BootContent> boot_content;
    if (!FileSys::IsAddonDisabled(base_title_id, "DLC")) {
        for (auto& [id, romfs] : nup->GetAddonRomFS(addon_application_id)) {
            boot_content.push_back({id, FileSys::ContentRecordType::Data, std::move(romfs)});
        }
    }
    for (auto& [id, romfs] : nup->GetSiblingProgramRomFS()) {
        boot_content.push_back({id, FileSys::ContentRecordType::Program, std::move(romfs)});
    }
    if (edition_romfs != nullptr) {
        if (auto legacy = MergedLegacyRomFS()) {
            boot_content.push_back({FileSys::GetBaseTitleIDWithProgramIndex(base_title_id, 0),
                                    FileSys::ContentRecordType::Program, std::move(legacy)});
        }
    }
    FileSys::SetBootContent(std::move(boot_content));

    system.GetFileSystemController().RegisterProcess(
        process.GetProcessId(), base_title_id,
        std::make_shared<FileSys::RomFSFactory>(*this, system.GetContentProvider(),
                                                system.GetFileSystemController()));

    is_loaded = true;
    return result;
}

ResultStatus AppLoader_NUP::VerifyIntegrity(
    std::function<bool(size_t, size_t)> progress_callback) {
    if (nup->GetStatus() != ResultStatus::Success) {
        return nup->GetStatus();
    }
    return nup->VerifyIntegrity(std::move(progress_callback));
}

std::vector<FileSys::Patch> AppLoader_NUP::ReadEmbeddedPatches() const {
    std::vector<FileSys::Patch> patches;
    if (nup->GetStatus() != ResultStatus::Success) {
        return patches;
    }
    const auto& packages = nup->GetPackages();
    const auto base_index = nup->FindBasePackage();
    const bool nx_base = base_index && (packages[*base_index].title_id >> 56) == 0x01;

    // Every update is listed, newest first, so the Add-Ons tab can pick an older one or none.
    std::vector<std::size_t> updates;
    for (std::size_t i = 0; i < packages.size(); ++i) {
        if (packages[i].kind == NupContainer::PackageKind::Update) {
            updates.push_back(i);
        }
    }
    std::ranges::sort(updates, [&](std::size_t a, std::size_t b) {
        return packages[a].version > packages[b].version;
    });
    for (const auto index : updates) {
        const auto& package = packages[index];
        // A Switch 2 build packed as the update of a Switch 1 base (Super Mario Galaxy 2) is the
        // game's Switch 2 Edition; switching it off boots the Switch 1 version.
        const bool edition = nx_base && (package.title_id >> 56) == 0x04;
        patches.push_back({.enabled = update_index == index,
                           .name = edition ? "Switch 2 Edition" : "Update",
                           .version = package.display_version.empty()
                                          ? DecodeVersion(package.version)
                                          : package.display_version,
                           .type = FileSys::PatchType::Update,
                           .program_id = base_title_id,
                           .title_id = package.title_id,
                           .source = FileSys::PatchSource::Embedded,
                           .numeric_version = package.version});
    }
    // A Switch 2 Edition program (0400... ending in its program index): shown only, booting the
    // Switch 1 program instead is not supported.
    if (has_program && (base_title_id >> 56) == 0x04 &&
        FileSys::GetBaseTitleID(base_title_id) != base_title_id) {
        patches.push_back({.enabled = true,
                           .name = "Switch 2 Edition",
                           .version = {},
                           .type = FileSys::PatchType::Edition,
                           .program_id = base_title_id,
                           .title_id = base_title_id,
                           .source = FileSys::PatchSource::Embedded});
    }
    const u64 application_id = nacp_file && nacp_file->GetDLCBaseTitleId() != 0
                                   ? FileSys::GetBaseTitleID(nacp_file->GetDLCBaseTitleId())
                                   : base_title_id;
    const bool dlc_enabled = !FileSys::IsAddonDisabled(base_title_id, "DLC");
    for (const auto index : nup->FindAddonPackages(application_id)) {
        const auto& package = packages[index];
        patches.push_back({.enabled = dlc_enabled,
                           .name = "DLC",
                           .version = fmt::format("{}", package.title_id & 0x7FF),
                           .type = FileSys::PatchType::DLC,
                           .program_id = application_id,
                           .title_id = package.title_id,
                           .source = FileSys::PatchSource::Embedded,
                           .numeric_version = package.version});
    }
    return patches;
}

FileSys::VirtualDir AppLoader_NUP::MergedLegacyRomFS() {
    const auto base_index = nup->FindBasePackage();
    const auto base_romfs = base_index ? nup->GetRomFSDir(*base_index) : FileSys::VirtualDir{};
    const auto update_romfs = update_index && !base_romfs_only ? nup->GetRomFSDir(*update_index)
                                                               : FileSys::VirtualDir{};
    // A standalone update carries the complete merged tree; a base+update pair
    // overlays the update onto the base (SPEC §4.1.4).
    return FileSys::NupContainer::MergeShadowDirs(base_romfs, update_romfs);
}

ResultStatus AppLoader_NUP::ReadRomFS(FileSys::VirtualFile& out_file) {
    // A Switch 2 Edition program sees only its own RomFS, as on hardware: games probe it to
    // decide whether to mount program 0 and the upgrade pack (Super Mario Bros. Wonder).
    const auto merged = edition_romfs != nullptr ? edition_romfs : MergedLegacyRomFS();
    if (merged == nullptr) {
        return ResultStatus::ErrorNoRomFS;
    }
    out_file = FileSys::CreateRomFS(merged);
    if (out_file == nullptr) {
        return ResultStatus::ErrorNoRomFS;
    }
    return ResultStatus::Success;
}

bool AppLoader_NUP::IsRomFSUpdatable() const {
    // The update is already merged into the exposed RomFS.
    return false;
}

ResultStatus AppLoader_NUP::ReadProgramId(u64& out_program_id) {
    if (!has_program) {
        return ResultStatus::ErrorNotInitialized;
    }
    out_program_id = base_title_id;
    return ResultStatus::Success;
}

ResultStatus AppLoader_NUP::ReadProgramIds(std::vector<u64>& out_program_ids) {
    if (!has_program) {
        return ResultStatus::ErrorNotInitialized;
    }
    out_program_ids.push_back(base_title_id);
    return ResultStatus::Success;
}

ResultStatus AppLoader_NUP::ReadIcon(std::vector<u8>& buffer) {
    if (icon_file == nullptr) {
        return ResultStatus::ErrorNoIcon;
    }
    buffer = icon_file->ReadAllBytes();
    return ResultStatus::Success;
}

ResultStatus AppLoader_NUP::ReadTitle(std::string& title) {
    if (nacp_file == nullptr) {
        return ResultStatus::ErrorNoControl;
    }
    title = title_override.empty() ? nacp_file->GetApplicationName() : title_override;
    if (title.empty()) {
        return ResultStatus::ErrorNoControl;
    }
    return ResultStatus::Success;
}

ResultStatus AppLoader_NUP::ReadDisplayVersion(std::string& version) {
    if (nacp_file == nullptr) {
        return ResultStatus::ErrorNoControl;
    }
    // Keep authentic control bytes intact while honoring the selected update and UI overrides.
    version = display_version.empty() ? nacp_file->GetVersionString() : display_version;
    return ResultStatus::Success;
}

ResultStatus AppLoader_NUP::ReadControlData(FileSys::NACP& control) {
    if (nacp_file == nullptr) {
        return ResultStatus::ErrorNoControl;
    }
    control = *nacp_file;
    return ResultStatus::Success;
}

ResultStatus AppLoader_NUP::ReadNSOModules(Modules& modules) {
    if (secondary_loader == nullptr) {
        return ResultStatus::ErrorNotInitialized;
    }
    return secondary_loader->ReadNSOModules(modules);
}

} // namespace Loader

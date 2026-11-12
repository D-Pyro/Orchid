// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <optional>
#include <string>

#include "core/file_sys/nup_container.h"
#include "core/loader/loader.h"

namespace FileSys {
class NACP;
class NupContainer;
} // namespace FileSys

namespace Loader {

/// The program a NUP boots and the RomFS it mounts.
struct NupBootProgram {
    std::optional<std::size_t> program_index; ///< Package whose title the program runs under
    std::optional<std::size_t> update_index;  ///< Enabled update, layered over the base
    FileSys::VirtualDir exefs;                ///< The ExeFS that boots
    FileSys::VirtualDir edition_romfs;        ///< A Switch 2 Edition program's own RomFS
    bool switch1_program{};                   ///< ExecuteProgram into the Switch 1 program
    bool base_romfs_only{};                   ///< That program is the base one, without update
};

/// Chooses the boot ExeFS; `switched_index` is the program index an ExecuteProgram restart
/// switches to (see SwitchedProgramIndex).
NupBootProgram SelectNupBootProgram(FileSys::NupContainer& nup,
                                    std::optional<std::size_t> switched_index);

/// Loads a NUP (NX Package) container: loose ExeFS/RomFS trees for a base
/// program plus its updates in one file. The newest update package is layered
/// over the base; add-on content RomFS trees are registered with the DLC service.
class AppLoader_NUP final : public AppLoader {
public:
    explicit AppLoader_NUP(FileSys::VirtualFile file_,
                           std::optional<std::size_t> switched_index = std::nullopt);
    ~AppLoader_NUP() override;

    /**
     * Identifies whether or not the given file is a NUP container.
     *
     * @param nup_file The file to identify.
     *
     * @return FileType::NUP, or FileType::Error if the file is not a NUP.
     */
    static FileType IdentifyType(const FileSys::VirtualFile& nup_file);

    FileType GetFileType() const override {
        return FileType::NUP;
    }

    LoadResult Load(Kernel::KProcess& process, Core::System& system) override;

    ResultStatus VerifyIntegrity(std::function<bool(size_t, size_t)> progress_callback) override;

    std::vector<FileSys::Patch> ReadEmbeddedPatches() const override;

    ResultStatus ReadRomFS(FileSys::VirtualFile& out_file) override;
    bool IsRomFSUpdatable() const override;
    ResultStatus ReadProgramId(u64& out_program_id) override;
    ResultStatus ReadProgramIds(std::vector<u64>& out_program_ids) override;
    ResultStatus ReadIcon(std::vector<u8>& buffer) override;
    ResultStatus ReadTitle(std::string& title) override;
    ResultStatus ReadDisplayVersion(std::string& version) override;
    ResultStatus ReadControlData(FileSys::NACP& control) override;
    ResultStatus ReadNSOModules(Modules& modules) override;

private:
    /// The base RomFS with the selected update overlaid.
    FileSys::VirtualDir MergedLegacyRomFS();

    std::unique_ptr<FileSys::NupContainer> nup;
    std::unique_ptr<AppLoader> secondary_loader;
    /// The update layered over the base: the newest one not switched off in the Add-Ons tab.
    std::optional<std::size_t> update_index;
    /// The booting Switch 2 Edition program's own RomFS (program1/romfs), when packed that way.
    /// It is mounted alone; the merged base/update data is served as program index 0.
    FileSys::VirtualDir edition_romfs;
    /// Booting the base program because the update's Switch 1 ExeFS is not packed: its RomFS
    /// leaves the update out so code and data match.
    bool base_romfs_only{};

    std::unique_ptr<FileSys::NACP> nacp_file;
    FileSys::VirtualFile icon_file;
    u64 base_title_id{};
    std::string display_version;
    std::string title_override;
    bool has_program{};
};

} // namespace Loader

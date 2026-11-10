// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

#include "common/common_types.h"

namespace Common {

struct MemoryInfo {
    u64 TotalPhysicalMemory{};
    u64 TotalSwapMemory{};
};

/**
 * Gets the memory info of the host system
 * @return Reference to a MemoryInfo struct with the physical and swap memory sizes in bytes
 */
[[nodiscard]] const MemoryInfo& GetMemInfo();

/// What this process uses of the resources a host can run out of. Values the platform cannot
/// report stay 0.
struct HostResources {
    u64 process_resident{};   ///< Working set / resident set
    u64 process_commit{};     ///< Windows: private commit charge
    u64 host_commit_free{};   ///< Windows: commit this process can still get
    u64 host_commit_limit{};  ///< Windows: RAM plus pagefile
    u64 host_physical_free{}; ///< Windows: unused RAM
    u32 gdi_objects{};        ///< Windows: a process may hold 10,000
    u32 user_objects{};       ///< Windows: a process may hold 10,000
    u32 handles{};            ///< Windows: kernel handles
};

[[nodiscard]] HostResources QueryHostResources();

/// One log line for a crash report: issue 140's process ended in Qt because CreateDIBSection
/// failed, which names neither the resource that ran out nor how fast it went.
[[nodiscard]] std::string DescribeHostResources(const HostResources& resources);

} // namespace Common

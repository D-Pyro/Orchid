// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "common/common_types.h"
#include "common/container/unordered_map.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/page_table.h"
#include "common/scope_exit.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/device_memory.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_process_page_table.h"
#include "core/hle/kernel/kernel.h"
#include "core/memory.h"
#include "video_core/buffer_cache/memory_tracker_base.h"
#include "video_core/gpu.h"
#include "video_core/host1x/gpu_device_memory_manager.h"
#include "video_core/memory_manager.h"
#include "video_core/nx2.h"
#include "video_core/renderer_null/null_rasterizer.h"

namespace {
using Range = std::pair<u64, u64>;

constexpr u64 PAGE = 4096;
constexpr u64 WORD = 4096 * 64;
constexpr u64 HIGH_PAGE_BITS = 22;
constexpr u64 HIGH_PAGE_SIZE = 1ULL << HIGH_PAGE_BITS;

constexpr DAddr BASE_ADDRESS = 16 * HIGH_PAGE_SIZE;

class RasterizerInterface {
public:
    void UpdatePagesCachedCount(DAddr addr, size_t size, s32 delta) {
        ++update_calls;
        Apply(addr, size, delta);
    }

    void UpdatePagesCachedBatch(std::span<const std::pair<DAddr, size_t>> ranges, s32 delta) {
        if (ranges.empty()) {
            return;
        }
        ++update_calls;
        for (const auto& [addr, size] : ranges) {
            Apply(addr, size, delta);
        }
    }

    [[nodiscard]] size_t UpdateCalls() const noexcept { return update_calls; }
    [[nodiscard]] const std::vector<std::tuple<DAddr, u64, int>>& UpdateCallsList() const noexcept { return calls; }

    [[nodiscard]] int Count(DAddr addr) const noexcept {
        const auto it = page_table.find(addr >> Core::DEVICE_PAGEBITS);
        return it == page_table.end() ? 0 : it->second;
    }

    [[nodiscard]] unsigned Count() const noexcept {
        unsigned count = 0;
        for (const auto& [index, value] : page_table) {
            count += value;
        }
        return count;
    }

private:
    void Apply(DAddr addr, size_t size, s32 delta) {
        calls.emplace_back(addr, size, delta);
        const u64 page_start{addr >> Core::DEVICE_PAGEBITS};
        const u64 page_end{(addr + size + Core::DEVICE_PAGESIZE - 1) >> Core::DEVICE_PAGEBITS};
        for (u64 page = page_start; page < page_end; ++page) {
            int& value = page_table[page];
            value += delta;
            if (value == 0) {
                page_table.erase(page);
            } else if (value < 0) {
                throw std::logic_error{"negative page"};
            }
        }
    }

    ::Common::unordered_map<u64, int> page_table;
    std::vector<std::tuple<DAddr, u64, int>> calls;
    size_t update_calls = 0;
};

} // Anonymous namespace

using MemoryTracker = VideoCommon::MemoryTrackerBase<RasterizerInterface>;

TEST_CASE("MemoryTracker: Small region", "[video_core]") {
    RasterizerInterface rasterizer;
    std::optional<MemoryTracker> memory_track(rasterizer);
    REQUIRE(rasterizer.Count() == 0);
    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS, WORD);
    REQUIRE(rasterizer.Count() == WORD / PAGE);
    REQUIRE(memory_track->ModifiedCpuRegion(BASE_ADDRESS, WORD) == Range{0, 0});

    memory_track->MarkRegionAsCpuModified(BASE_ADDRESS + PAGE, 1);
    REQUIRE(memory_track->ModifiedCpuRegion(BASE_ADDRESS, WORD) == Range{BASE_ADDRESS + PAGE * 1, BASE_ADDRESS + PAGE * 2});
}

TEST_CASE("MemoryTracker: Large region", "[video_core]") {
    RasterizerInterface rasterizer;
    std::optional<MemoryTracker> memory_track(rasterizer);
    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS, WORD * 32);
    memory_track->MarkRegionAsCpuModified(BASE_ADDRESS + 4096, WORD * 4);
    REQUIRE(memory_track->ModifiedCpuRegion(BASE_ADDRESS, WORD + PAGE * 2) == Range{BASE_ADDRESS + PAGE, BASE_ADDRESS + WORD + PAGE * 2});
    REQUIRE(memory_track->ModifiedCpuRegion(BASE_ADDRESS + PAGE * 2, PAGE * 6) == Range{BASE_ADDRESS + PAGE * 2, BASE_ADDRESS + PAGE * 8});
    REQUIRE(memory_track->ModifiedCpuRegion(BASE_ADDRESS, WORD * 32) == Range{BASE_ADDRESS + PAGE, BASE_ADDRESS + WORD * 4 + PAGE});
    REQUIRE(memory_track->ModifiedCpuRegion(BASE_ADDRESS + WORD * 4, PAGE) == Range{BASE_ADDRESS + WORD * 4, BASE_ADDRESS + WORD * 4 + PAGE});
    REQUIRE(memory_track->ModifiedCpuRegion(BASE_ADDRESS + WORD * 3 + PAGE * 63, PAGE) == Range{BASE_ADDRESS + WORD * 3 + PAGE * 63, BASE_ADDRESS + WORD * 4});

    memory_track->MarkRegionAsCpuModified(BASE_ADDRESS + WORD * 5 + PAGE * 6, PAGE);
    memory_track->MarkRegionAsCpuModified(BASE_ADDRESS + WORD * 5 + PAGE * 8, PAGE);
    REQUIRE(memory_track->ModifiedCpuRegion(BASE_ADDRESS + WORD * 5, WORD) == Range{BASE_ADDRESS + WORD * 5 + PAGE * 6, BASE_ADDRESS + WORD * 5 + PAGE * 9});

    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS + WORD * 5 + PAGE * 8, PAGE);
    REQUIRE(memory_track->ModifiedCpuRegion(BASE_ADDRESS + WORD * 5, WORD) == Range{BASE_ADDRESS + WORD * 5 + PAGE * 6, BASE_ADDRESS + WORD * 5 + PAGE * 7});

    memory_track->MarkRegionAsCpuModified(BASE_ADDRESS + PAGE, WORD * 31 + PAGE * 63);
    REQUIRE(memory_track->ModifiedCpuRegion(BASE_ADDRESS, WORD * 32) == Range{BASE_ADDRESS + PAGE, BASE_ADDRESS + WORD * 32});

    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS + PAGE * 4, PAGE);
    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS + PAGE * 6, PAGE);

    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS, WORD * 32);
    REQUIRE(memory_track->ModifiedCpuRegion(BASE_ADDRESS, WORD * 32) == Range{0, 0});
}

TEST_CASE("MemoryTracker: Rasterizer counting", "[video_core]") {
    RasterizerInterface rasterizer;
    std::optional<MemoryTracker> memory_track(rasterizer);
    REQUIRE(rasterizer.Count() == 0);
    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS, PAGE);
    REQUIRE(rasterizer.Count() == 1);
    memory_track->MarkRegionAsCpuModified(BASE_ADDRESS, PAGE * 2);
    REQUIRE(rasterizer.Count() == 0);
    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS, PAGE);
    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS + PAGE, PAGE);
    REQUIRE(rasterizer.Count() == 2);
    memory_track->MarkRegionAsCpuModified(BASE_ADDRESS, PAGE * 2);
    REQUIRE(rasterizer.Count() == 0);
}

TEST_CASE("MemoryTracker: Basic range", "[video_core]") {
    RasterizerInterface rasterizer;
    std::optional<MemoryTracker> memory_track(rasterizer);
    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS, WORD);
    memory_track->MarkRegionAsCpuModified(BASE_ADDRESS, PAGE);
    int num = 0;
    memory_track->ForEachUploadRange(BASE_ADDRESS, WORD, [&](u64 offset, u64 size) {
        REQUIRE(offset == BASE_ADDRESS);
        REQUIRE(size == PAGE);
        ++num;
    });
    REQUIRE(num == 1U);
}

TEST_CASE("MemoryTracker: Border upload", "[video_core]") {
    RasterizerInterface rasterizer;
    std::optional<MemoryTracker> memory_track(rasterizer);
    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS, WORD * 2);
    memory_track->MarkRegionAsCpuModified(BASE_ADDRESS + WORD - PAGE, PAGE * 2);
    memory_track->ForEachUploadRange(BASE_ADDRESS, WORD * 2, [](u64 offset, u64 size) {
        REQUIRE(offset == BASE_ADDRESS + WORD - PAGE);
        REQUIRE(size == PAGE * 2);
    });
}

TEST_CASE("MemoryTracker: Border upload range", "[video_core]") {
    RasterizerInterface rasterizer;
    std::optional<MemoryTracker> memory_track(rasterizer);
    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS, WORD * 2);
    memory_track->MarkRegionAsCpuModified(BASE_ADDRESS + WORD - PAGE, PAGE * 2);
    memory_track->ForEachUploadRange(BASE_ADDRESS + WORD - PAGE, PAGE * 2, [](u64 offset, u64 size) {
        REQUIRE(offset == BASE_ADDRESS + WORD - PAGE);
        REQUIRE(size == PAGE * 2);
    });
    memory_track->MarkRegionAsCpuModified(BASE_ADDRESS + WORD - PAGE, PAGE * 2);
    memory_track->ForEachUploadRange(BASE_ADDRESS + WORD - PAGE, PAGE, [](u64 offset, u64 size) {
        REQUIRE(offset == BASE_ADDRESS + WORD - PAGE);
        REQUIRE(size == PAGE);
    });
    memory_track->ForEachUploadRange(BASE_ADDRESS + WORD, PAGE, [](u64 offset, u64 size) {
        REQUIRE(offset == BASE_ADDRESS + WORD);
        REQUIRE(size == PAGE);
    });
}

TEST_CASE("MemoryTracker: Border upload partial range", "[video_core]") {
    RasterizerInterface rasterizer;
    std::optional<MemoryTracker> memory_track(rasterizer);
    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS, WORD * 2);
    memory_track->MarkRegionAsCpuModified(BASE_ADDRESS + WORD - PAGE, PAGE * 2);
    memory_track->ForEachUploadRange(BASE_ADDRESS + WORD - 1, 2, [](u64 offset, u64 size) {
        REQUIRE(offset == BASE_ADDRESS + WORD - PAGE);
        REQUIRE(size == PAGE * 2);
    });
    memory_track->MarkRegionAsCpuModified(BASE_ADDRESS + WORD - PAGE, PAGE * 2);
    memory_track->ForEachUploadRange(BASE_ADDRESS + WORD - 1, 1, [](u64 offset, u64 size) {
        REQUIRE(offset == BASE_ADDRESS + WORD - PAGE);
        REQUIRE(size == PAGE);
    });
    memory_track->ForEachUploadRange(BASE_ADDRESS + WORD + 50, 1, [](u64 offset, u64 size) {
        REQUIRE(offset == BASE_ADDRESS + WORD);
        REQUIRE(size == PAGE);
    });
}

TEST_CASE("MemoryTracker: Partial word uploads", "[video_core]") {
    RasterizerInterface rasterizer;
    std::optional<MemoryTracker> memory_track(rasterizer);
    int num = 0;
    memory_track->ForEachUploadRange(BASE_ADDRESS, WORD, [&](u64 offset, u64 size) {
        REQUIRE(offset == BASE_ADDRESS);
        REQUIRE(size == WORD);
        ++num;
    });
    REQUIRE(num == 1);
    memory_track->ForEachUploadRange(BASE_ADDRESS + WORD, WORD, [&](u64 offset, u64 size) {
        REQUIRE(offset == BASE_ADDRESS + WORD);
        REQUIRE(size == WORD);
        ++num;
    });
    REQUIRE(num == 2);
    memory_track->ForEachUploadRange(BASE_ADDRESS + 0x79000, 0x24000, [&](u64 offset, u64 size) {
        REQUIRE(offset == BASE_ADDRESS + WORD * 2);
        REQUIRE(size == PAGE * 0x1d);
        ++num;
    });
    REQUIRE(num == 3);
}

TEST_CASE("MemoryTracker: Partial page upload", "[video_core]") {
    RasterizerInterface rasterizer;
    std::optional<MemoryTracker> memory_track(rasterizer);
    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS, WORD);
    int num = 0;
    memory_track->MarkRegionAsCpuModified(BASE_ADDRESS + PAGE * 2, PAGE);
    memory_track->MarkRegionAsCpuModified(BASE_ADDRESS + PAGE * 9, PAGE);
    memory_track->ForEachUploadRange(BASE_ADDRESS, PAGE * 3, [&](u64 offset, u64 size) {
        REQUIRE(offset == BASE_ADDRESS + PAGE * 2);
        REQUIRE(size == PAGE);
        ++num;
    });
    REQUIRE(num == 1);
    memory_track->ForEachUploadRange(BASE_ADDRESS + PAGE * 7, PAGE * 3, [&](u64 offset, u64 size) {
        REQUIRE(offset == BASE_ADDRESS + PAGE * 9);
        REQUIRE(size == PAGE);
        ++num;
    });
    REQUIRE(num == 2);
}

TEST_CASE("MemoryTracker: Partial page upload with multiple words on the right") {
    RasterizerInterface rasterizer;
    std::optional<MemoryTracker> memory_track(rasterizer);
    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS, WORD * 9);
    memory_track->MarkRegionAsCpuModified(BASE_ADDRESS + PAGE * 13, WORD * 7);
    int num = 0;
    memory_track->ForEachUploadRange(BASE_ADDRESS + PAGE * 10, WORD * 7, [&](u64 offset, u64 size) {
        REQUIRE(offset == BASE_ADDRESS + PAGE * 13);
        REQUIRE(size == WORD * 7 - PAGE * 3);
        ++num;
    });
    REQUIRE(num == 1);
    memory_track->ForEachUploadRange(BASE_ADDRESS + PAGE, WORD * 8, [&](u64 offset, u64 size) {
        REQUIRE(offset == BASE_ADDRESS + WORD * 7 + PAGE * 10);
        REQUIRE(size == PAGE * 3);
        ++num;
    });
    REQUIRE(num == 2);
}

TEST_CASE("MemoryTracker: Partial page upload with multiple words on the left", "[video_core]") {
    RasterizerInterface rasterizer;
    std::optional<MemoryTracker> memory_track(rasterizer);
    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS, WORD * 8);
    memory_track->MarkRegionAsCpuModified(BASE_ADDRESS + PAGE * 13, WORD * 7);
    int num = 0;
    memory_track->ForEachUploadRange(BASE_ADDRESS + PAGE * 16, WORD * 7, [&](u64 offset, u64 size) {
        REQUIRE(offset == BASE_ADDRESS + PAGE * 16);
        REQUIRE(size == WORD * 7 - PAGE * 3);
        ++num;
    });
    REQUIRE(num == 1);
    memory_track->ForEachUploadRange(BASE_ADDRESS + PAGE, WORD, [&](u64 offset, u64 size) {
        REQUIRE(offset == BASE_ADDRESS + PAGE * 13);
        REQUIRE(size == PAGE * 3);
        ++num;
    });
    REQUIRE(num == 2);
}

TEST_CASE("MemoryTracker: Partial page upload with multiple words in the middle", "[video_core]") {
    RasterizerInterface rasterizer;
    std::optional<MemoryTracker> memory_track(rasterizer);
    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS, WORD * 8);
    memory_track->MarkRegionAsCpuModified(BASE_ADDRESS + PAGE * 13, PAGE * 140);
    int num = 0;
    memory_track->ForEachUploadRange(BASE_ADDRESS + PAGE * 16, WORD, [&](u64 offset, u64 size) {
        REQUIRE(offset == BASE_ADDRESS + PAGE * 16);
        REQUIRE(size == WORD);
        ++num;
    });
    REQUIRE(num == 1);
    memory_track->ForEachUploadRange(BASE_ADDRESS, WORD, [&](u64 offset, u64 size) {
        REQUIRE(offset == BASE_ADDRESS + PAGE * 13);
        REQUIRE(size == PAGE * 3);
        ++num;
    });
    REQUIRE(num == 2);
    memory_track->ForEachUploadRange(BASE_ADDRESS, WORD * 8, [&](u64 offset, u64 size) {
        REQUIRE(offset == BASE_ADDRESS + WORD + PAGE * 16);
        REQUIRE(size == PAGE * 73);
        ++num;
    });
    REQUIRE(num == 3);
}

TEST_CASE("MemoryTracker: Empty right bits", "[video_core]") {
    RasterizerInterface rasterizer;
    std::optional<MemoryTracker> memory_track(rasterizer);
    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS, WORD * 2048);
    memory_track->MarkRegionAsCpuModified(BASE_ADDRESS + WORD - PAGE, PAGE * 2);
    memory_track->ForEachUploadRange(BASE_ADDRESS, WORD * 2048, [](u64 offset, u64 size) {
        REQUIRE(offset == BASE_ADDRESS + WORD - PAGE);
        REQUIRE(size == PAGE * 2);
    });
}

TEST_CASE("MemoryTracker: Out of bound ranges 1", "[video_core]") {
    RasterizerInterface rasterizer;
    std::optional<MemoryTracker> memory_track(rasterizer);
    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS - WORD, 3 * WORD);
    memory_track->MarkRegionAsCpuModified(BASE_ADDRESS, PAGE);
    REQUIRE(rasterizer.Count() == (3 * WORD - PAGE) / PAGE);
    int num = 0;
    memory_track->ForEachUploadRange(BASE_ADDRESS - WORD, WORD, [&](u64 offset, u64 size) { ++num; });
    memory_track->ForEachUploadRange(BASE_ADDRESS + WORD, WORD, [&](u64 offset, u64 size) { ++num; });
    memory_track->ForEachUploadRange(BASE_ADDRESS - PAGE, PAGE, [&](u64 offset, u64 size) { ++num; });
    REQUIRE(num == 0);
    memory_track->ForEachUploadRange(BASE_ADDRESS - PAGE, PAGE * 2, [&](u64 offset, u64 size) { ++num; });
    REQUIRE(num == 1);
    memory_track->MarkRegionAsCpuModified(BASE_ADDRESS, WORD);
    REQUIRE(rasterizer.Count() == 2 * WORD / PAGE);
}

TEST_CASE("MemoryTracker: Out of bound ranges 2", "[video_core]") {
    RasterizerInterface rasterizer;
    std::optional<MemoryTracker> memory_track(rasterizer);
    REQUIRE_NOTHROW(memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS + 0x22000, PAGE));
    REQUIRE_NOTHROW(memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS + 0x28000, PAGE));
    REQUIRE(rasterizer.Count() == 2);
    REQUIRE_NOTHROW(memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS + 0x21100, PAGE - 0x100));
    REQUIRE(rasterizer.Count() == 3);
    REQUIRE_NOTHROW(memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS - PAGE, PAGE * 2));
    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS - PAGE * 3, PAGE * 2);
    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS - PAGE * 2, PAGE * 2);
    REQUIRE(rasterizer.Count() == 7);
}

TEST_CASE("MemoryTracker: Out of bound ranges 3", "[video_core]") {
    RasterizerInterface rasterizer;
    std::optional<MemoryTracker> memory_track(rasterizer);
    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS, 0x310720);
    REQUIRE(rasterizer.Count(BASE_ADDRESS) == 1);
    REQUIRE(rasterizer.Count(BASE_ADDRESS + PAGE) == 1);
    REQUIRE(rasterizer.Count(BASE_ADDRESS + WORD) == 1);
    REQUIRE(rasterizer.Count(BASE_ADDRESS + WORD + PAGE) == 1);
}

TEST_CASE("MemoryTracker: Sparse regions 1", "[video_core]") {
    RasterizerInterface rasterizer;
    std::optional<MemoryTracker> memory_track(rasterizer);
    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS, WORD);
    memory_track->MarkRegionAsCpuModified(BASE_ADDRESS + PAGE * 1, PAGE);
    memory_track->MarkRegionAsCpuModified(BASE_ADDRESS + PAGE * 3, PAGE * 4);
    memory_track->ForEachUploadRange(BASE_ADDRESS, WORD, [i = 0](u64 offset, u64 size) mutable {
        static constexpr std::array<u64, 2> offsets{BASE_ADDRESS + PAGE, BASE_ADDRESS + PAGE * 3};
        static constexpr std::array<u64, 2> sizes{PAGE, PAGE * 4};
        REQUIRE(offset == offsets.at(i));
        REQUIRE(size == sizes.at(i));
        ++i;
    });
}

TEST_CASE("MemoryTracker: Sparse regions 2", "[video_core]") {
    RasterizerInterface rasterizer;
    std::optional<MemoryTracker> memory_track(rasterizer);
    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS, PAGE * 0x23);
    REQUIRE(rasterizer.Count() == 0x23);
    memory_track->MarkRegionAsCpuModified(BASE_ADDRESS + PAGE * 0x1B, PAGE);
    memory_track->MarkRegionAsCpuModified(BASE_ADDRESS + PAGE * 0x21, PAGE);
    memory_track->ForEachUploadRange(BASE_ADDRESS, PAGE * 0x23, [i = 0](u64 offset, u64 size) mutable {
        static constexpr std::array<u64, 3> offsets{BASE_ADDRESS + PAGE * 0x1B, BASE_ADDRESS + PAGE * 0x21};
        static constexpr std::array<u64, 3> sizes{PAGE, PAGE};
        REQUIRE(offset == offsets.at(i));
        REQUIRE(size == sizes.at(i));
        ++i;
    });
}

TEST_CASE("MemoryTracker: Single page modified range", "[video_core]") {
    RasterizerInterface rasterizer;
    std::optional<MemoryTracker> memory_track(rasterizer);
    REQUIRE(memory_track->IsRegionCpuModified(BASE_ADDRESS, PAGE));
    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS, PAGE);
    REQUIRE(!memory_track->IsRegionCpuModified(BASE_ADDRESS, PAGE));
}

TEST_CASE("MemoryTracker: Two page modified range", "[video_core]") {
    RasterizerInterface rasterizer;
    std::optional<MemoryTracker> memory_track(rasterizer);
    REQUIRE(memory_track->IsRegionCpuModified(BASE_ADDRESS, PAGE));
    REQUIRE(memory_track->IsRegionCpuModified(BASE_ADDRESS + PAGE, PAGE));
    REQUIRE(memory_track->IsRegionCpuModified(BASE_ADDRESS, PAGE * 2));
    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS, PAGE);
    REQUIRE(!memory_track->IsRegionCpuModified(BASE_ADDRESS, PAGE));
}

TEST_CASE("MemoryTracker: Multi word modified ranges", "[video_core]") {
    for (int offset = 0; offset < 4; ++offset) {
        const DAddr address = BASE_ADDRESS + WORD * offset;
        RasterizerInterface rasterizer;
        std::optional<MemoryTracker> memory_track(rasterizer);
        REQUIRE(memory_track->IsRegionCpuModified(address, PAGE));
        REQUIRE(memory_track->IsRegionCpuModified(address + PAGE * 48, PAGE));
        REQUIRE(memory_track->IsRegionCpuModified(address + PAGE * 56, PAGE));

        memory_track->UnmarkRegionAsCpuModified(address + PAGE * 32, PAGE);
        REQUIRE(memory_track->IsRegionCpuModified(address + PAGE, WORD));
        REQUIRE(memory_track->IsRegionCpuModified(address + PAGE * 31, PAGE));
        REQUIRE(!memory_track->IsRegionCpuModified(address + PAGE * 32, PAGE));
        REQUIRE(memory_track->IsRegionCpuModified(address + PAGE * 33, PAGE));
        REQUIRE(memory_track->IsRegionCpuModified(address + PAGE * 31, PAGE * 2));
        REQUIRE(memory_track->IsRegionCpuModified(address + PAGE * 32, PAGE * 2));

        memory_track->UnmarkRegionAsCpuModified(address + PAGE * 33, PAGE);
        REQUIRE(!memory_track->IsRegionCpuModified(address + PAGE * 32, PAGE * 2));
    }
}

TEST_CASE("MemoryTracker: Single page in large region", "[video_core]") {
    RasterizerInterface rasterizer;
    std::optional<MemoryTracker> memory_track(rasterizer);
    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS, WORD * 16);
    REQUIRE(!memory_track->IsRegionCpuModified(BASE_ADDRESS, WORD * 16));

    memory_track->MarkRegionAsCpuModified(BASE_ADDRESS + WORD * 12 + PAGE * 8, PAGE);
    REQUIRE(memory_track->IsRegionCpuModified(BASE_ADDRESS, WORD * 16));
    REQUIRE(!memory_track->IsRegionCpuModified(BASE_ADDRESS + WORD * 10, WORD * 2));
    REQUIRE(memory_track->IsRegionCpuModified(BASE_ADDRESS + WORD * 11, WORD * 2));
    REQUIRE(memory_track->IsRegionCpuModified(BASE_ADDRESS + WORD * 12, WORD * 2));
    REQUIRE(memory_track->IsRegionCpuModified(BASE_ADDRESS + WORD * 12 + PAGE * 4, PAGE * 8));
    REQUIRE(memory_track->IsRegionCpuModified(BASE_ADDRESS + WORD * 12 + PAGE * 6, PAGE * 8));
    REQUIRE(!memory_track->IsRegionCpuModified(BASE_ADDRESS + WORD * 12 + PAGE * 6, PAGE));
    REQUIRE(memory_track->IsRegionCpuModified(BASE_ADDRESS + WORD * 12 + PAGE * 7, PAGE * 2));
    REQUIRE(memory_track->IsRegionCpuModified(BASE_ADDRESS + WORD * 12 + PAGE * 8, PAGE * 2));
}

TEST_CASE("MemoryTracker: Wrap word regions") {
    RasterizerInterface rasterizer;
    std::optional<MemoryTracker> memory_track(rasterizer);
    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS, WORD * 32);
    memory_track->MarkRegionAsCpuModified(BASE_ADDRESS + PAGE * 63, PAGE * 2);
    REQUIRE(memory_track->IsRegionCpuModified(BASE_ADDRESS, WORD * 2));
    REQUIRE(!memory_track->IsRegionCpuModified(BASE_ADDRESS + PAGE * 62, PAGE));
    REQUIRE(memory_track->IsRegionCpuModified(BASE_ADDRESS + PAGE * 63, PAGE));
    REQUIRE(memory_track->IsRegionCpuModified(BASE_ADDRESS + PAGE * 64, PAGE));
    REQUIRE(memory_track->IsRegionCpuModified(BASE_ADDRESS + PAGE * 63, PAGE * 2));
    REQUIRE(memory_track->IsRegionCpuModified(BASE_ADDRESS + PAGE * 63, PAGE * 8));
    REQUIRE(memory_track->IsRegionCpuModified(BASE_ADDRESS + PAGE * 60, PAGE * 8));

    REQUIRE(!memory_track->IsRegionCpuModified(BASE_ADDRESS + PAGE * 127, WORD * 16));
    memory_track->MarkRegionAsCpuModified(BASE_ADDRESS + PAGE * 127, PAGE);
    REQUIRE(memory_track->IsRegionCpuModified(BASE_ADDRESS + PAGE * 127, WORD * 16));
    REQUIRE(memory_track->IsRegionCpuModified(BASE_ADDRESS + PAGE * 127, PAGE));
    REQUIRE(!memory_track->IsRegionCpuModified(BASE_ADDRESS + PAGE * 126, PAGE));
    REQUIRE(memory_track->IsRegionCpuModified(BASE_ADDRESS + PAGE * 126, PAGE * 2));
    REQUIRE(!memory_track->IsRegionCpuModified(BASE_ADDRESS + PAGE * 128, WORD * 16));
}

TEST_CASE("MemoryTracker: Unaligned page region query") {
    RasterizerInterface rasterizer;
    std::optional<MemoryTracker> memory_track(rasterizer);
    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS, WORD);
    memory_track->MarkRegionAsCpuModified(BASE_ADDRESS + 4000, 1000);
    REQUIRE(memory_track->IsRegionCpuModified(BASE_ADDRESS, PAGE));
    REQUIRE(memory_track->IsRegionCpuModified(BASE_ADDRESS + PAGE, PAGE));
    REQUIRE(memory_track->IsRegionCpuModified(BASE_ADDRESS + 4000, 1000));
    REQUIRE(memory_track->IsRegionCpuModified(BASE_ADDRESS + 4000, 1));
}

TEST_CASE("MemoryTracker: Cached write") {
    RasterizerInterface rasterizer;
    std::optional<MemoryTracker> memory_track(rasterizer);
    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS, WORD);
    memory_track->CachedCpuWrite(BASE_ADDRESS + PAGE, BASE_ADDRESS + PAGE);
    REQUIRE(!memory_track->IsRegionCpuModified(BASE_ADDRESS + PAGE, PAGE));
    memory_track->FlushCachedWrites();
    REQUIRE(memory_track->IsRegionCpuModified(BASE_ADDRESS + PAGE, PAGE));
    memory_track->MarkRegionAsCpuModified(BASE_ADDRESS, WORD);
    REQUIRE(rasterizer.Count() == 0);
}

TEST_CASE("MemoryTracker: Multiple cached write") {
    RasterizerInterface rasterizer;
    std::optional<MemoryTracker> memory_track(rasterizer);
    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS, WORD);
    memory_track->CachedCpuWrite(BASE_ADDRESS + PAGE, PAGE);
    memory_track->CachedCpuWrite(BASE_ADDRESS + PAGE * 3, PAGE);
    REQUIRE(!memory_track->IsRegionCpuModified(BASE_ADDRESS + PAGE, PAGE));
    REQUIRE(!memory_track->IsRegionCpuModified(BASE_ADDRESS + PAGE * 3, PAGE));
    memory_track->FlushCachedWrites();
    REQUIRE(memory_track->IsRegionCpuModified(BASE_ADDRESS + PAGE, PAGE));
    REQUIRE(memory_track->IsRegionCpuModified(BASE_ADDRESS + PAGE * 3, PAGE));
    memory_track->MarkRegionAsCpuModified(BASE_ADDRESS, WORD);
    REQUIRE(rasterizer.Count() == 0);
}

TEST_CASE("MemoryTracker: Cached write unmarked") {
    RasterizerInterface rasterizer;
    std::optional<MemoryTracker> memory_track(rasterizer);
    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS, WORD);
    memory_track->CachedCpuWrite(BASE_ADDRESS + PAGE, PAGE);
    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS + PAGE, PAGE);
    REQUIRE(!memory_track->IsRegionCpuModified(BASE_ADDRESS + PAGE, PAGE));
    memory_track->FlushCachedWrites();
    REQUIRE(memory_track->IsRegionCpuModified(BASE_ADDRESS + PAGE, PAGE));
    memory_track->MarkRegionAsCpuModified(BASE_ADDRESS, WORD);
    REQUIRE(rasterizer.Count() == 0);
}

TEST_CASE("MemoryTracker: Cached write iterated") {
    RasterizerInterface rasterizer;
    std::optional<MemoryTracker> memory_track(rasterizer);
    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS, WORD);
    memory_track->CachedCpuWrite(BASE_ADDRESS + PAGE, PAGE);
    int num = 0;
    memory_track->ForEachUploadRange(BASE_ADDRESS, WORD, [&](u64 offset, u64 size) { ++num; });
    REQUIRE(num == 0);
    REQUIRE(!memory_track->IsRegionCpuModified(BASE_ADDRESS + PAGE, PAGE));
    memory_track->FlushCachedWrites();
    REQUIRE(memory_track->IsRegionCpuModified(BASE_ADDRESS + PAGE, PAGE));
    memory_track->MarkRegionAsCpuModified(BASE_ADDRESS, WORD);
    REQUIRE(rasterizer.Count() == 0);
}

TEST_CASE("MemoryTracker: Cached write downloads") {
    RasterizerInterface rasterizer;
    std::optional<MemoryTracker> memory_track(rasterizer);
    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS, WORD);
    REQUIRE(rasterizer.Count() == 64);
    memory_track->CachedCpuWrite(BASE_ADDRESS + PAGE, PAGE);
    REQUIRE(rasterizer.Count() == 63);
    memory_track->MarkRegionAsGpuModified(BASE_ADDRESS + PAGE, PAGE);
    int num = 0;
    memory_track->ForEachDownloadRangeAndClear(BASE_ADDRESS, WORD, [&](u64 offset, u64 size) { ++num; });
    REQUIRE(num == 0);
    num = 0;
    memory_track->ForEachUploadRange(BASE_ADDRESS, WORD, [&](u64 offset, u64 size) { ++num; });
    REQUIRE(num == 0);
    REQUIRE(!memory_track->IsRegionCpuModified(BASE_ADDRESS + PAGE, PAGE));
    REQUIRE(memory_track->IsRegionGpuModified(BASE_ADDRESS + PAGE, PAGE));
    memory_track->FlushCachedWrites();
    REQUIRE(memory_track->IsRegionCpuModified(BASE_ADDRESS + PAGE, PAGE));
    REQUIRE(!memory_track->IsRegionGpuModified(BASE_ADDRESS + PAGE, PAGE));
    memory_track->MarkRegionAsCpuModified(BASE_ADDRESS, WORD);
    REQUIRE(rasterizer.Count() == 0);
}

TEST_CASE("MemoryTracker: cached writes preserve tracking counts") {
    RasterizerInterface rasterizer;
    std::optional<MemoryTracker> memory_track(rasterizer);
    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS, WORD * 2);
    REQUIRE(rasterizer.UpdateCalls() == 1);
    REQUIRE(rasterizer.UpdateCallsList().size() == 1);
    REQUIRE(rasterizer.UpdateCallsList().front() == std::tuple{BASE_ADDRESS, WORD * 2, 1});

    memory_track->CachedCpuWrite(BASE_ADDRESS + PAGE, PAGE);
    memory_track->CachedCpuWrite(BASE_ADDRESS + PAGE * 2, PAGE);
    memory_track->CachedCpuWrite(BASE_ADDRESS + PAGE * 4, PAGE);
    REQUIRE(rasterizer.Count() == 125);
    REQUIRE(rasterizer.Count(BASE_ADDRESS + PAGE) == 0);
    REQUIRE(rasterizer.Count(BASE_ADDRESS + PAGE * 2) == 0);
    REQUIRE(rasterizer.Count(BASE_ADDRESS + PAGE * 3) == 1);
    REQUIRE(rasterizer.Count(BASE_ADDRESS + PAGE * 4) == 0);
    const auto update_calls = rasterizer.UpdateCalls();
    memory_track->FlushCachedWrites();
    REQUIRE(rasterizer.UpdateCalls() == update_calls);
    REQUIRE(rasterizer.Count() == 125);
    REQUIRE(memory_track->IsRegionCpuModified(BASE_ADDRESS + PAGE, PAGE));
    REQUIRE(memory_track->IsRegionCpuModified(BASE_ADDRESS + PAGE * 2, PAGE));
    REQUIRE_FALSE(memory_track->IsRegionCpuModified(BASE_ADDRESS + PAGE * 3, PAGE));
    REQUIRE(memory_track->IsRegionCpuModified(BASE_ADDRESS + PAGE * 4, PAGE));
    memory_track->MarkRegionAsCpuModified(BASE_ADDRESS, WORD * 2);
    REQUIRE(rasterizer.Count() == 0);
}

TEST_CASE("MemoryTracker: FlushCachedWrites batching") {
    RasterizerInterface rasterizer;
    std::optional<MemoryTracker> memory_track(rasterizer);
    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS, WORD * 2);
    memory_track->CachedCpuWrite(BASE_ADDRESS + PAGE, PAGE);
    memory_track->CachedCpuWrite(BASE_ADDRESS + PAGE * 2, PAGE);
    memory_track->CachedCpuWrite(BASE_ADDRESS + PAGE * 4, PAGE);
    // Cached writes stop tracking their pages right away; tracking them again leaves the
    // pending cached writes for FlushCachedWrites to report
    REQUIRE(rasterizer.Count() == WORD * 2 / PAGE - 3);
    memory_track->UnmarkRegionAsCpuModified(BASE_ADDRESS, WORD * 2);
    REQUIRE(rasterizer.Count() == WORD * 2 / PAGE);
    const size_t calls_before_flush = rasterizer.UpdateCalls();
    const size_t ranges_before_flush = rasterizer.UpdateCallsList().size();
    memory_track->FlushCachedWrites();
    // Now we expect a single batch call (coalesced ranges) to the device memory manager
    REQUIRE(rasterizer.UpdateCalls() == calls_before_flush + 1);
    const auto& calls = rasterizer.UpdateCallsList();
    REQUIRE(calls.size() == ranges_before_flush + 2);
    REQUIRE(calls[ranges_before_flush] == std::tuple<DAddr, u64, int>{BASE_ADDRESS + PAGE, PAGE * 2, -1});
    REQUIRE(calls[ranges_before_flush + 1] == std::tuple<DAddr, u64, int>{BASE_ADDRESS + PAGE * 4, PAGE, -1});
    REQUIRE(rasterizer.Count() == WORD * 2 / PAGE - 3);
}

TEST_CASE("DeviceMemoryManager: UpdatePagesCachedBatch basic") {
    Core::DeviceMemory device_memory;
    Tegra::MaxwellDeviceMemoryManager manager(device_memory);
    // empty should be a no-op
    std::vector<std::pair<DAddr, size_t>> empty;
    manager.UpdatePagesCachedBatch(empty, 1);

    // small ranges should be accepted and not crash
    std::vector<std::pair<DAddr, size_t>> ranges;
    ranges.emplace_back(0, Core::Memory::YUZU_PAGESIZE);
    ranges.emplace_back(Core::Memory::YUZU_PAGESIZE, Core::Memory::YUZU_PAGESIZE);
    manager.UpdatePagesCachedBatch(ranges, 1);
    SUCCEED("UpdatePagesCachedBatch executed without error");
}

TEST_CASE("MemoryTracker: repeated indirect rewrites keep historical command-page tracking",
          "[video_core][nx2_command_sync][mkw_sync]") {
    Tegra::Nx2ClearGpuWrittenPages();
    RasterizerInterface rasterizer;
    auto tracker = std::make_unique<MemoryTracker>(rasterizer);
    tracker->UnmarkRegionAsCpuModified(BASE_ADDRESS, PAGE);
    const auto translate = [](GPUVAddr address) -> std::optional<DAddr> {
        return BASE_ADDRESS + address - 0x8600000000ULL;
    };
    REQUIRE_FALSE(Tegra::Nx2HasGpuWrittenPage(0x8600000000ULL, 5 * sizeof(u32), translate));
    for (u32 frame = 0; frame < 3; ++frame) {
        tracker->MarkRegionAsGpuModified(BASE_ADDRESS, 5 * sizeof(u32));
        Tegra::Nx2MarkGpuWritten(BASE_ADDRESS, 5 * sizeof(u32));
        REQUIRE(Tegra::Nx2HasGpuWrittenPage(0x8600000000ULL, 5 * sizeof(u32), translate));
        REQUIRE(tracker->IsRegionGpuModified(BASE_ADDRESS, 5 * sizeof(u32)));
        std::vector<Range> downloads;
        tracker->ForEachDownloadRangeAndClear(BASE_ADDRESS, PAGE, [&](u64 address, u64 size) {
            downloads.emplace_back(address, size);
        });
        REQUIRE(downloads == std::vector<Range>{{BASE_ADDRESS, PAGE}});
        REQUIRE_FALSE(tracker->IsRegionGpuModified(BASE_ADDRESS, 5 * sizeof(u32)));
        REQUIRE(Tegra::Nx2HasGpuWrittenPage(0x8600000000ULL, 5 * sizeof(u32), translate));
        downloads.clear();
        tracker->ForEachDownloadRangeAndClear(BASE_ADDRESS, PAGE, [&](u64 address, u64 size) {
            downloads.emplace_back(address, size);
        });
        REQUIRE(downloads.empty());
    }
    tracker->MarkRegionAsCpuModified(BASE_ADDRESS, PAGE);
    REQUIRE(rasterizer.Count() == 0);
}

TEST_CASE("DeviceMemoryManager unsafe unmapped reads preserve zero fill",
          "[video_core][unmapped_trace]") {
    Core::DeviceMemory device_memory;
    Tegra::MaxwellDeviceMemoryManager manager(device_memory);
    for (const size_t size : {size_t{69632}, size_t{20480}}) {
        std::vector<u8> data(size, 0xa5);
        manager.ReadBlockUnsafe(0x10000, data.data(), data.size());
        REQUIRE(std::ranges::all_of(data, [](u8 byte) { return byte == 0; }));
    }
}

// Run in a separate process with EDEN_TEST_UNMAPPED_LOG_DIR set. The log output is
// checked by the integration runner in both trace modes, after the old cap is exhausted.
TEST_CASE("DeviceMemoryManager trace after warning cap", "[.][unmapped_trace_integration]") {
    const char* log_dir = std::getenv("EDEN_TEST_UNMAPPED_LOG_DIR");
    REQUIRE(log_dir != nullptr);
    Common::FS::SetEdenPath(Common::FS::EdenPath::LogDir, std::filesystem::path{log_dir});
    Common::Log::Initialize();
    Common::Log::SetColorConsoleBackendEnabled(true);

    Core::DeviceMemory device_memory;
    Tegra::MaxwellDeviceMemoryManager manager(device_memory);
    std::vector<u8> warmup(16 * 4096, 0xa5);
    manager.ReadBlockUnsafe(0x10000, warmup.data(), warmup.size());
    REQUIRE(std::ranges::all_of(warmup, [](u8 byte) { return byte == 0; }));
    for (int i = 0; i < 2; ++i) {
        std::vector<u8> data(69632, 0xa5);
        manager.ReadBlockUnsafe(0x30000, data.data(), data.size());
        REQUIRE(std::ranges::all_of(data, [](u8 byte) { return byte == 0; }));
    }

    Core::System system;
    Tegra::GPU gpu(system, false, false);
    Null::RasterizerNull rasterizer(gpu);
    Tegra::MemoryManager gpu_memory(system, manager);
    gpu_memory.BindRasterizer(&rasterizer);
    gpu_memory.Map(0x100000, 0x40000, 2 * 65536);
    for (int i = 0; i < 2; ++i) {
        std::vector<u8> data(24576, 0xa5);
        // The first big-page chunk is 20480 bytes; the outer request is 24576.
        gpu_memory.ReadBlockUnsafe(0x10b000, data.data(), data.size());
        REQUIRE(std::ranges::all_of(data, [](u8 byte) { return byte == 0; }));
    }
    // Exercise a real process page table with one mapped page followed by an unmapped page.
    Settings::values.use_multi_core.SetValue(false);
    system.Initialize();
    Kernel::KProcess process(system.Kernel());
    SCOPE_EXIT {
        system.Kernel().UnregisterKernelObject(&process);
    };
    auto& cpu_memory = process.GetMemory();
    auto& page_table = process.GetPageTable().GetImpl();
    page_table.Resize(32, 12);
    cpu_memory.SetCurrentPageTable(process);
    constexpr u64 physical_offset = 0x8000;
    cpu_memory.MapMemoryRegion(page_table, Common::ProcessAddress{0x200000}, 4096,
                               Common::PhysicalAddress{Core::DramMemoryMap::Base + physical_offset},
                               Common::MemoryPermission::ReadWrite, false);
    system.DeviceMemory().buffer.ClearBackingRegion(physical_offset, 4096, 0x42);
    Tegra::MaxwellDeviceMemoryManager mapped_manager(system.DeviceMemory());
    const auto asid = mapped_manager.RegisterProcess(&cpu_memory);
    mapped_manager.Map(0x70000, 0x200000, 4096, asid);
    std::vector<u8> mixed(8192, 0xa5);
    mapped_manager.ReadBlockUnsafe(0x70000, mixed.data(), mixed.size());
    REQUIRE(std::all_of(mixed.begin(), mixed.begin() + 4096, [](u8 byte) { return byte == 0x42; }));
    REQUIRE(std::all_of(mixed.begin() + 4096, mixed.end(), [](u8 byte) { return byte == 0; }));
    Common::Log::Stop();
}

TEST_CASE("MemoryTracker: preflushable iteration clips unaligned writes across manager boundaries",
          "[video_core][selective_download]") {
    RasterizerInterface rasterizer;
    MemoryTracker tracker(rasterizer);
    constexpr VAddr boundary = 8 * 1024 * 1024;
    constexpr u64 page = 4096;
    tracker.MarkRegionAsPreflushable(boundary - page, 2 * page);
    tracker.MarkRegionAsPreflushable(boundary + 3 * page, page);
    const auto collect = [&](VAddr start, u64 size) {
        std::vector<std::pair<VAddr, u64>> ranges;
        tracker.ForEachPreflushableRange(start, size, [&](VAddr address, u64 length) {
            ranges.emplace_back(address, length);
        });
        return ranges;
    };
    // Both boundary pages are marked, but the write intersects only these exact bytes.
    const std::vector<std::pair<VAddr, u64>> expected{
        {boundary - 13, 13}, {boundary, 17}};
    CHECK(collect(boundary - 13, 30) == expected);
    CHECK(collect(boundary - 13, 30) == expected); // Marks persist after enumeration.
    CHECK(collect(boundary + page, 2 * page).empty()); // An unmarked hole stays excluded.
    CHECK(collect(boundary + 3 * page + 7, 19) ==
          std::vector<std::pair<VAddr, u64>>{{boundary + 3 * page + 7, 19}});
    tracker.UnmarkRegionAsPreflushable(boundary - page, 2 * page);
    CHECK(collect(boundary - 13, 30).empty());
}

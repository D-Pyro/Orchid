// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include "common/common_types.h"
#include "common/literals.h"

namespace VideoCommon {
struct TextureCacheMemoryThresholds {
    u64 expected, critical, minimum;
};

// Budget-driven callers have already reserved driver/allocator pressure headroom.
// Retain the legacy ceiling when there is no reliable, scoped device budget.
constexpr TextureCacheMemoryThresholds ClampTextureCacheThresholds(
    TextureCacheMemoryThresholds limits, bool budget_driven, u64 legacy_ceiling) {
    using namespace Common::Literals;
    if (!budget_driven)
        limits.critical = (std::min)(limits.critical, legacy_ceiling);
    limits.expected =
        (std::min)(limits.expected, limits.critical - (std::min)(limits.critical, u64{768_MiB}));
    limits.minimum =
        (std::min)(limits.minimum, limits.expected - (std::min)(limits.expected, u64{1_GiB}));
    return limits;
}

/// Copies of colour targets their own draws sample are kept per image shape. Dynamic resolution
/// gives every frame size its own shape (Super Mario Bros. Wonder: 420 sizes in nine minutes, about
/// 6 GiB of copies), so only `cap` are kept. `shadows` is ordered least recently used first.
/// Returns the index of the copy to free before making a new one: the oldest one the current draw
/// does not bind, once `cap` exist. Returns `std::dynamic_extent` when nothing needs to (or can) be freed.
inline constexpr std::size_t MaxFeedbackShadows = 8;
template <typename Id>
constexpr std::size_t FeedbackShadowToEvict(std::span<const Id> shadows, std::span<const Id> in_use,
                                            std::size_t cap = MaxFeedbackShadows) {
    if (shadows.size() < cap) {
        return std::dynamic_extent;
    }
    for (std::size_t i = 0; i < shadows.size(); ++i) {
        if (std::ranges::find(in_use, shadows[i]) == in_use.end()) {
            return i;
        }
    }
    return std::dynamic_extent;
}
} // namespace VideoCommon

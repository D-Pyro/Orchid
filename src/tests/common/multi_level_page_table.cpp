// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <utility>

#ifdef __linux__
#include <sys/mman.h>
#endif

#include <catch2/catch_test_macros.hpp>

#include "common/multi_level_page_table.h"

namespace {

using Table = Common::MultiLevelPageTable<u32>;

constexpr std::size_t AddressBits = 32;
constexpr std::size_t FirstLevelBits = 8;
constexpr std::size_t PageBits = 12;
constexpr u64 AddressSpaceEnd = 1ULL << AddressBits;
constexpr u64 FirstLevelSize = 1ULL << (AddressBits - FirstLevelBits);

Table MakeTable() {
    return Table{AddressBits, FirstLevelBits, PageBits};
}

} // namespace

TEST_CASE("Page table reserves a range ending at the end of the address space",
          "[common][page_table]") {
    auto table = MakeTable();
    table.ReserveRange(AddressSpaceEnd - FirstLevelSize, FirstLevelSize);

    const std::size_t last_page = (AddressSpaceEnd >> PageBits) - 1;
    table[last_page] = 7;
    CHECK(table[last_page] == 7);
}

TEST_CASE("Page table reserves ranges that end on a first level boundary", "[common][page_table]") {
    auto table = MakeTable();
    table.ReserveRange(0, FirstLevelSize);
    table.ReserveRange(FirstLevelSize, FirstLevelSize);
    table[0] = 1;
    table[FirstLevelSize >> PageBits] = 2;
    CHECK(table[0] == 1);
    CHECK(table[FirstLevelSize >> PageBits] == 2);
}

TEST_CASE("Page table ignores an empty range at the end of the address space",
          "[common][page_table]") {
    auto table = MakeTable();
    table.ReserveRange(AddressSpaceEnd, 0);
    SUCCEED();
}

#ifdef __linux__
TEST_CASE("Page table moved from another releases its mapping", "[common][page_table]") {
    void* base = nullptr;
    {
        auto first = MakeTable();
        Table second{std::move(first)};
        base = second.data();
    }
    unsigned char resident = 0;
    CHECK(mincore(base, 1, &resident) != 0);
}
#endif

#ifdef __linux__
TEST_CASE("Page table assigned over releases the mapping it had", "[common][page_table]") {
    auto first = MakeTable();
    void* const replaced = first.data();
    first = MakeTable();
    unsigned char resident = 0;
    CHECK(mincore(replaced, 1, &resident) != 0);
}
#endif

TEST_CASE("Page table keeps its mapping through a move", "[common][page_table]") {
    auto first = MakeTable();
    first[3] = 5;

    Table second{std::move(first)};
    CHECK(second[3] == 5);

    auto third = MakeTable();
    third = std::move(second);
    CHECK(third[3] == 5);
}

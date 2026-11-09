// SPDX-License-Identifier: GPL-3.0-or-later

#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/object_pool.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"

namespace {
struct Counted {
    static inline int live = 0;

    explicit Counted(int value_) : value{value_} {
        ++live;
    }
    ~Counted() {
        --live;
    }

    int value;
};
} // Anonymous namespace

TEST_CASE("ObjectPool::Free destroys every object across chunks", "[shader]") {
    Counted::live = 0;
    {
        Shader::ObjectPool<Counted> pool{4};
        for (int index = 0; index < 10; ++index) {
            REQUIRE(pool.Create(index)->value == index);
        }
        REQUIRE(Counted::live == 10);
        pool.Free();
        REQUIRE(Counted::live == 0);
    }
    REQUIRE(Counted::live == 0);
}

TEST_CASE("ObjectPool stays usable after Free", "[shader]") {
    Counted::live = 0;
    {
        Shader::ObjectPool<Counted> pool{4};
        for (int index = 0; index < 6; ++index) {
            (void)pool.Create(index);
        }
        pool.Free();
        Counted* const first{pool.Create(100)};
        for (int index = 0; index < 8; ++index) {
            (void)pool.Create(index);
        }
        REQUIRE(first->value == 100);
        REQUIRE(Counted::live == 9);
        pool.ReleaseContents();
        REQUIRE(Counted::live == 0);
        (void)pool.Create(7);
        REQUIRE(Counted::live == 1);
    }
    REQUIRE(Counted::live == 0);
}

TEST_CASE("ObjectPool compute storage is released only for synchronous compilation", "[shader]") {
    for (const bool parallel : {false, true}) {
        Vulkan::ShaderPools pools;
        Shader::IR::Program program;
        auto* const block = pools.block.Create(pools.inst);
        program.blocks.push_back(block);
        auto* const flow_block = pools.flow_block.Create();
        flow_block->begin = Shader::Maxwell::Location{120};
        std::vector<u32> code{1, 2, 3};
        pools.ReleaseComputeContents(program, code, parallel);
        REQUIRE(program.blocks.empty() == !parallel);
        REQUIRE(code.empty() == !parallel);
        if (!parallel) {
            REQUIRE(code.capacity() == 0);
        } else {
            REQUIRE(program.blocks.front() == block);
            REQUIRE(code[2] == 3);
        }
        // The CFG keeps references to flow blocks through the driver call.
        REQUIRE(flow_block->begin == Shader::Maxwell::Location{120});
        REQUIRE(pools.block.Create(pools.inst) != nullptr);
    }
}

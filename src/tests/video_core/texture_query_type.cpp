// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/environment.h"
#include "shader_recompiler/frontend/ir/ir_emitter.h"
#include "shader_recompiler/frontend/ir/program.h"
#include "shader_recompiler/host_translate_info.h"
#include "shader_recompiler/ir_opt/passes.h"

namespace {
using namespace Shader;

// Every bound descriptor reads as `descriptor_type`; an empty TIC entry, as seen while a scene is
// being set up, reads as 1D.
class DescriptorEnvironment final : public Environment {
public:
    explicit DescriptorEnvironment(TextureType descriptor_type_)
        : descriptor_type{descriptor_type_} {}
    u64 ReadInstruction(u32) override { return 0; }
    u32 ReadCbufValue(u32, u32 offset) override { return offset; }
    TextureType ReadTextureType(u32) override { return descriptor_type; }
    TexturePixelFormat ReadTexturePixelFormat(u32) override {
        return TexturePixelFormat::A8B8G8R8_UNORM;
    }
    bool IsTexturePixelFormatInteger(u32) override { return false; }
    u32 ReadViewportTransformState() override { return 1; }
    u32 TextureBoundBuffer() const override { return 2; }
    u32 LocalMemorySize() const override { return 0; }
    u32 SharedMemorySize() const override { return 0; }
    std::array<u32, 3> WorkgroupSize() const override { return {1, 1, 1}; }
    bool HasHLEMacroState() const override { return false; }
    std::optional<ReplaceConstant> GetReplaceConstBuffer(u32, u32) override {
        return std::nullopt;
    }
    void Dump(u64, u64) override {}

private:
    TextureType descriptor_type;
};

// Runs the texture pass over one size query on `query_handle` plus one sample (or storage image
// read) of `sample_type` on handle 0x520, and returns the type the query ended up with.
TextureType QueryTypeNextTo(TextureType sample_type, u32 query_handle, TextureType descriptor_type,
                            bool storage_image = false) {
    DescriptorEnvironment env{descriptor_type};
    ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    IR::Program program;
    program.blocks = {&block};
    program.post_order_blocks = {&block};
    IR::IREmitter ir{block};
    const IR::Value query{
        ir.ImageQueryDimension(ir.Imm32(query_handle), ir.Imm32(0U), ir.Imm1(false))};
    IR::TextureInstInfo info{};
    info.type.Assign(sample_type);
    if (storage_image) {
        const IR::Value coords{ir.CompositeConstruct(ir.Imm32(0U), ir.Imm32(0U))};
        (void)ir.ImageRead(ir.Imm32(0x520U), coords, info);
    } else if (sample_type == TextureType::Color1D || sample_type == TextureType::Buffer) {
        (void)ir.ImageSampleImplicitLod(ir.Imm32(0x520U), ir.Imm32(0.5f), {}, {}, {}, info);
    } else {
        const IR::Value coords{ir.CompositeConstruct(ir.Imm32(0.5f), ir.Imm32(0.5f))};
        (void)ir.ImageSampleImplicitLod(ir.Imm32(0x520U), coords, {}, {}, {}, info);
    }

    HostTranslateInfo host_info;
    host_info.ApplyDescriptorLimitPolicy();
    Optimization::TexturePass(env, program, host_info);
    return query.Inst()->Flags<IR::TextureInstInfo>().type.Value();
}
} // Anonymous namespace

// Pokemon Legends Z-A: the tone-map shader queried the size of the scene colour as 1D (height 0)
// and sampled it as 2D, so the whole cutscene showed the top line of the scene.
TEST_CASE("A size query takes the texture type of a sample on the same handle",
          "[video_core][texture]") {
    constexpr TextureType empty{TextureType::Color1D};
    REQUIRE(QueryTypeNextTo(TextureType::Color2D, 0x520, empty) == TextureType::Color2D);
    REQUIRE(QueryTypeNextTo(TextureType::ColorCube, 0x520, empty) == TextureType::ColorCube);
}

TEST_CASE("A size query keeps the descriptor type without a usable sample",
          "[video_core][texture]") {
    constexpr TextureType bound{TextureType::ColorArray2D};
    // Another handle says nothing about this one.
    REQUIRE(QueryTypeNextTo(TextureType::Color2D, 0x528, bound) == bound);
    // A 1D sample may address a texture buffer: only the descriptor tells.
    REQUIRE(QueryTypeNextTo(TextureType::Color1D, 0x520, bound) == bound);
    REQUIRE(QueryTypeNextTo(TextureType::Buffer, 0x520, bound) == bound);
    // A storage image is a different kind of descriptor.
    REQUIRE(QueryTypeNextTo(TextureType::Color2D, 0x520, bound, true) == bound);
}

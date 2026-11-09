#include "TestHarness.h"

#include <cstdio>
#include <filesystem>
#include <string>

#include "litematicabds/Mapping.h"
#include "litematicabds/Schematic.h"

using namespace litematicabds;

namespace {

std::filesystem::path fixturePath() {
    return std::filesystem::path("tests") / "fixtures" / "tiny.litematic";
}

SchematicBlock const* findBlock(Schematic const& schematic, Vec3i pos) {
    for (auto const& block : schematic.blocks) {
        if (block.pos == pos) return &block;
    }
    return nullptr;
}

} // namespace

LIT_TEST(litematic_load_fixture) {
    BlockMapper mapper;
    auto        loaded = loadLitematicFile(fixturePath(), mapper);
    LIT_CHECK(loaded.ok());
    if (!loaded.ok()) {
        std::printf("  load error: %s\n", loaded.error().message.c_str());
        return;
    }

    auto const& result    = loaded.value();
    auto const& schematic = *result.schematic;
    LIT_CHECK_EQ(schematic.meta.name, std::string("Tiny"));
    LIT_CHECK_EQ(schematic.meta.author, std::string("fixture"));
    LIT_CHECK_EQ(schematic.meta.size, Vec3i(3, 2, 2));
    LIT_CHECK_EQ(schematic.meta.totalBlocks, std::uint64_t(6));
    LIT_CHECK_EQ(schematic.blocks.size(), std::size_t(6));
    LIT_CHECK_EQ(schematic.surface.size(), schematic.blocks.size());
    LIT_CHECK_EQ(schematic.meta.regionCount, std::int32_t(1));

    auto const* stone = findBlock(schematic, {0, 0, 0});
    LIT_CHECK(stone != nullptr);
    if (stone != nullptr) {
        LIT_CHECK_EQ(schematic.palette[stone->palette].javaName, std::string("minecraft:stone"));
        LIT_CHECK_EQ(
            schematic.palette[stone->palette].bedrockState,
            std::string("minecraft:stone[stone_type=stone]")
        );
    }

    auto const* log = findBlock(schematic, {1, 0, 0});
    LIT_CHECK(log != nullptr);
    if (log != nullptr) {
        LIT_CHECK_EQ(
            schematic.palette[log->palette].bedrockState,
            std::string("minecraft:oak_log[pillar_axis=y]")
        );
    }

    LIT_CHECK(findBlock(schematic, {2, 1, 1}) != nullptr);
    LIT_CHECK(findBlock(schematic, {0, 0, 1}) == nullptr);
}

LIT_TEST(litematic_writer_roundtrip) {
    BlockMapper mapper;

    SaveRequest request;
    request.name        = "roundtrip";
    request.author      = "tests";
    request.description = "writer roundtrip";
    request.size        = Vec3i(2, 2, 2);
    request.blocks.push_back({{0, 0, 0}, {"minecraft:stone", ""}});
    request.blocks.push_back({{1, 0, 0}, {"minecraft:oak_stairs", "upside_down_bit=1,weirdo_direction=3"}});
    request.blocks.push_back({{1, 1, 1}, {"minecraft:oak_log", "pillar_axis=y"}});

    auto path = std::filesystem::path("tests") / "out" / "roundtrip.litematic";
    std::filesystem::remove(path);
    std::vector<std::string> warnings;
    auto                     written = writeLitematicFile(path, request, mapper, &warnings);
    LIT_CHECK(written.ok());
    LIT_CHECK(std::filesystem::exists(path));

    auto loaded = loadLitematicFile(path, mapper);
    LIT_CHECK(loaded.ok());
    if (!loaded.ok()) return;
    auto const& schematic = *loaded.value().schematic;
    LIT_CHECK_EQ(schematic.meta.name, std::string("roundtrip"));
    LIT_CHECK_EQ(schematic.meta.author, std::string("tests"));
    LIT_CHECK_EQ(schematic.meta.size, Vec3i(2, 2, 2));
    LIT_CHECK_EQ(schematic.blocks.size(), std::size_t(3));

    auto const* stairs = findBlock(schematic, {1, 0, 0});
    LIT_CHECK(stairs != nullptr);
    if (stairs != nullptr) {
        auto const& entry = schematic.palette[stairs->palette];
        LIT_CHECK_EQ(entry.javaName, std::string("minecraft:oak_stairs"));
        LIT_CHECK(entry.javaProperties.find("facing=north") != std::string::npos);
        LIT_CHECK(entry.javaProperties.find("half=top") != std::string::npos);
    }
}

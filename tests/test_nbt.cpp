#include "TestHarness.h"

#include <cstring>
#include <string>
#include <vector>

#include "litematicabds/BlockState.h"
#include "litematicabds/Nbt.h"
#include "litematicabds/Types.h"
#include "../core/util/Gzip.h"

using namespace litematicabds;
using namespace litematicabds::nbt;

LIT_TEST(block_state_parse) {
    auto state = parseBlockState("minecraft:oak_stairs[facing=north,half=bottom]");
    LIT_CHECK(state.has_value());
    LIT_CHECK_EQ(state->name, std::string("minecraft:oak_stairs"));
    LIT_CHECK_EQ(state->properties, std::string("facing=north,half=bottom"));
    LIT_CHECK_EQ(state->toString(), std::string("minecraft:oak_stairs[facing=north,half=bottom]"));

    auto canonical = parseBlockState("minecraft:stone[z=2,a=1,a=3]");
    LIT_CHECK(canonical.has_value());
    LIT_CHECK_EQ(canonical->properties, std::string("a=3,z=2"));

    LIT_CHECK(parseBlockState("").has_value() == false);
    LIT_CHECK((BlockState{"minecraft:cave_air", ""}.isAir()));
    LIT_CHECK(!(BlockState{"minecraft:stone", ""}.isAir()));
    LIT_CHECK(stateValueEquals("true", "1"));
    LIT_CHECK(stateValueEquals("01", "1"));
    LIT_CHECK(!stateValueEquals("a", "b"));
}

LIT_TEST(types_rotation) {
    Rotation rotation{};
    LIT_CHECK(parseRotation("90", rotation));
    LIT_CHECK(rotation == Rotation::CW90);
    LIT_CHECK(parseRotation("CW270", rotation));
    LIT_CHECK(rotation == Rotation::CW270);
    LIT_CHECK(!parseRotation("45", rotation));

    Vec3i const size{3, 4, 5};
    LIT_CHECK(transformLocal({0, 0, 0}, size, Rotation::CW90, Mirror::None) == Vec3i(4, 0, 0));
    LIT_CHECK(transformLocal({2, 0, 0}, size, Rotation::CW180, Mirror::None) == Vec3i(0, 0, 4));
    LIT_CHECK(rotatedSize(size, Rotation::CW90) == Vec3i(5, 4, 3));
}

LIT_TEST(gzip_roundtrip) {
    std::string text;
    for (int i = 0; i < 2000; ++i) text += "litematica-bds-";
    std::vector<std::uint8_t> input(text.begin(), text.end());

    auto compressed = util::gzipCompress(input);
    LIT_CHECK(compressed.ok());
    LIT_CHECK(util::looksGzip(compressed.value()));

    auto restored = util::decompress(compressed.value());
    LIT_CHECK(restored.ok());
    LIT_CHECK_EQ(restored.value().size(), input.size());
    LIT_CHECK(std::memcmp(restored.value().data(), input.data(), input.size()) == 0);
}

LIT_TEST(nbt_roundtrip) {
    Compound metadata;
    metadata.set("Name", std::string("demo"));
    metadata.set("Author", std::string("test"));
    metadata.set("TimeCreated", static_cast<std::int64_t>(123456789));

    Compound region;
    region.set("Position", Tag::makeCompound(Compound{}));
    region.set("Size", Tag::makeIntArray({3, 2, 1}));
    region.set("BlockStates", Tag::makeLongArray({0x1122334455667788ull, 0x99AABBCCDDEEFF00ull}));

    Compound paletteEntry;
    paletteEntry.set("Name", std::string("minecraft:stone"));
    region.set("BlockStatePalette", Tag::makeList(Type::Compound, {Tag::makeCompound(paletteEntry)}));

    Compound regions;
    regions.set("main", Tag::makeCompound(region));

    Compound root;
    root.set("Version", 6);
    root.set("Metadata", Tag::makeCompound(metadata));
    root.set("Regions", Tag::makeCompound(regions));

    auto bytes = serialize("", Tag::makeCompound(root));
    LIT_CHECK(!bytes.empty());

    auto parsed = parse(bytes);
    LIT_CHECK(parsed.ok());
    LIT_CHECK(parsed.value().isCompound());
    auto const& parsedRoot = parsed.value().asCompound();
    LIT_CHECK_EQ(parsedRoot.at("Version").asInt64(), static_cast<std::int64_t>(6));

    auto const& parsedMeta = parsedRoot.at("Metadata").asCompound();
    LIT_CHECK_EQ(parsedMeta.at("Name").asString(), std::string("demo"));
    LIT_CHECK_EQ(parsedMeta.at("TimeCreated").asInt64(), static_cast<std::int64_t>(123456789));

    auto const& parsedRegion = parsedRoot.at("Regions").asCompound().at("main").asCompound();
    auto const& longs        = parsedRegion.at("BlockStates").asLongArray();
    LIT_CHECK_EQ(longs.size(), std::size_t(2));
    LIT_CHECK_EQ(longs[0], 0x1122334455667788ull);
    LIT_CHECK_EQ(longs[1], 0x99AABBCCDDEEFF00ull);

    auto const& palette = parsedRegion.at("BlockStatePalette").asList();
    LIT_CHECK_EQ(palette.items.size(), std::size_t(1));
    LIT_CHECK_EQ(palette.items[0].asCompound().at("Name").asString(), std::string("minecraft:stone"));
}

LIT_TEST(nbt_truncated_is_error) {
    Compound root;
    root.set("Name", std::string("this is a longer string to truncate"));
    auto bytes = serialize("", Tag::makeCompound(root));
    LIT_CHECK(bytes.size() > 8);

    auto parsed = parse(std::span<std::uint8_t const>(bytes.data(), bytes.size() - 5));
    LIT_CHECK(!parsed.ok());
}

LIT_TEST(nbt_depth_limit) {
    Tag inner = Tag::makeCompound(Compound{});
    for (int i = 0; i < 200; ++i) {
        Compound outer;
        outer.set("child", inner);
        inner = Tag::makeCompound(outer);
    }
    auto bytes = serialize("", inner);
    ParseOptions options;
    options.maxDepth = 32;
    auto parsed = parse(bytes, options);
    LIT_CHECK(!parsed.ok());
}

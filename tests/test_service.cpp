#include "TestHarness.h"

#include <filesystem>
#include <string>
#include <unordered_map>

#include "litematicabds/Render.h"
#include "litematicabds/Service.h"

using namespace litematicabds;

namespace {

struct FakeSink : IGhostSink {
    std::unordered_map<Vec3i, std::string, Vec3iHash> blocks;
    int                                              clearCalls{0};

    bool setBlock(std::string_view, Vec3i pos, std::string_view state) override {
        blocks[pos] = std::string(state);
        return true;
    }
    bool clearBlock(std::string_view, Vec3i pos) override {
        blocks.erase(pos);
        ++clearCalls;
        return true;
    }
};

std::filesystem::path serviceRoot() { return std::filesystem::path("tests") / "out" / "service"; }

void prepareServiceDirs() {
    std::error_code ec;
    std::filesystem::remove_all(serviceRoot(), ec);
    std::filesystem::create_directories(serviceRoot() / "schematics", ec);
    std::filesystem::copy_file(
        std::filesystem::path("tests") / "fixtures" / "tiny.litematic",
        serviceRoot() / "schematics" / "tiny.litematic",
        std::filesystem::copy_options::overwrite_existing,
        ec
    );
}

CommandContext playerContext() {
    CommandContext context;
    context.player    = "Steve";
    context.position  = {0.5, 64.0, 0.5};
    context.dimension = 0;
    context.op        = true;
    return context;
}

} // namespace

LIT_TEST(service_load_place_clear) {
    prepareServiceDirs();
    LitematicaService service({serviceRoot(), {}, {}, {}});
    LIT_CHECK(service.initialize().ok());

    auto context = playerContext();

    auto loaded = service.execute(context, "load tiny");
    LIT_CHECK(loaded.ok);
    LIT_CHECK(!loaded.lines.empty());

    auto placed = service.execute(context, "place tiny 0 64 0");
    LIT_CHECK(placed.ok);

    service.updateViewer("Steve", {0.5, 64.0, 0.5}, 0);
    FakeSink sink;
    for (int i = 0; i < 10; ++i) service.tick(sink, static_cast<std::uint64_t>(i));
    LIT_CHECK_EQ(sink.blocks.size(), std::size_t(6));

    auto clear = service.execute(context, "clear");
    LIT_CHECK(clear.ok);
    for (int i = 0; i < 10; ++i) service.tick(sink, static_cast<std::uint64_t>(100 + i));
    LIT_CHECK_EQ(sink.blocks.size(), std::size_t(0));
    LIT_CHECK(sink.clearCalls > 0);
}

LIT_TEST(service_layer_filters) {
    prepareServiceDirs();
    LitematicaService service({serviceRoot(), {}, {}, {}});
    LIT_CHECK(service.initialize().ok());
    auto context = playerContext();

    LIT_CHECK(service.execute(context, "place tiny 0 64 0").ok);
    LIT_CHECK(service.execute(context, "layer 0").ok);

    service.updateViewer("Steve", {0.5, 64.0, 0.5}, 0);
    FakeSink sink;
    for (int i = 0; i < 10; ++i) service.tick(sink, static_cast<std::uint64_t>(i));
    LIT_CHECK_EQ(sink.blocks.size(), std::size_t(3));
    for (auto const& [pos, state] : sink.blocks) {
        LIT_CHECK_EQ(pos.y, 64);
        LIT_CHECK(!state.empty());
    }

    LIT_CHECK(service.execute(context, "layer all").ok);
    for (int i = 0; i < 10; ++i) service.tick(sink, static_cast<std::uint64_t>(100 + i));
    LIT_CHECK_EQ(sink.blocks.size(), std::size_t(6));
}

LIT_TEST(service_info_and_materials) {
    prepareServiceDirs();
    LitematicaService service({serviceRoot(), {}, {}, {}});
    LIT_CHECK(service.initialize().ok());
    auto context = playerContext();

    LIT_CHECK(service.execute(context, "place tiny").ok);
    auto info = service.execute(context, "info");
    LIT_CHECK(info.ok);
    LIT_CHECK(info.lines.size() >= 3);

    auto materials = service.execute(context, "materials 5");
    LIT_CHECK(materials.ok);
    LIT_CHECK(materials.lines.size() >= 2);
    LIT_CHECK(std::filesystem::exists(serviceRoot() / "exports" / "tiny-materials.csv"));

    auto status = service.viewers();
    LIT_CHECK_EQ(status.size(), std::size_t(1));
    LIT_CHECK(status[0].active);
    LIT_CHECK_EQ(status[0].schematic, std::string("tiny"));
}

LIT_TEST(service_unknown_command) {
    prepareServiceDirs();
    LitematicaService service({serviceRoot(), {}, {}, {}});
    LIT_CHECK(service.initialize().ok());
    auto result = service.execute(playerContext(), "definitely-not-a-command");
    LIT_CHECK(!result.ok);
    LIT_CHECK(!result.lines.empty());
}

LIT_TEST(service_place_coordinates) {
    prepareServiceDirs();
    LitematicaService service({serviceRoot(), {}, {}, {}});
    LIT_CHECK(service.initialize().ok());
    auto context = playerContext();

    LIT_CHECK(service.execute(context, "place tiny 10 64 10").ok);
    auto status = service.viewers();
    LIT_CHECK_EQ(status.size(), std::size_t(1));
    LIT_CHECK_EQ(status[0].origin, Vec3i(10, 64, 10));

    LIT_CHECK(service.execute(context, "place 20 64 20").ok);
    status = service.viewers();
    LIT_CHECK_EQ(status[0].origin, Vec3i(20, 64, 20));
}

LIT_TEST(service_save_region) {
    prepareServiceDirs();
    LitematicaService service({serviceRoot(), {}, {}, {}});
    LIT_CHECK(service.initialize().ok());

    service.setBlockSampler([](std::string_view, Vec3i pos, int) -> std::optional<BlockState> {
        if (pos.x >= 0 && pos.x <= 2 && pos.y == 64 && pos.z >= 0 && pos.z <= 2) {
            return BlockState{"minecraft:stone", ""};
        }
        return std::nullopt;
    });

    auto context = playerContext();
    auto saved   = service.execute(context, "save saved_region 0 64 0 2 64 2");
    LIT_CHECK(saved.ok);
    if (!saved.ok) return;
    auto path = serviceRoot() / "schematics" / "saved_region.litematic";
    LIT_CHECK(std::filesystem::exists(path));

    BlockMapper mapper;
    auto        loaded = loadLitematicFile(path, mapper);
    LIT_CHECK(loaded.ok());
    if (loaded.ok()) {
        LIT_CHECK_EQ(loaded.value().schematic->meta.name, std::string("saved_region"));
        LIT_CHECK_EQ(loaded.value().schematic->meta.size, Vec3i(3, 1, 3));
        LIT_CHECK_EQ(loaded.value().schematic->blocks.size(), std::size_t(9));
    }

    auto duplicate = service.execute(context, "save saved_region 0 64 0 0 64 0");
    LIT_CHECK(!duplicate.ok);
}

LIT_TEST(service_mark_placed_skips_render) {
    prepareServiceDirs();
    LitematicaService service({serviceRoot(), {}, {}, {}});
    LIT_CHECK(service.initialize().ok());
    auto context = playerContext();

    LIT_CHECK(service.execute(context, "place tiny 0 64 0").ok);
    service.updateViewer("Steve", {0.5, 64.0, 0.5}, 0);
    FakeSink sink;
    for (int i = 0; i < 10; ++i) service.tick(sink, static_cast<std::uint64_t>(i));
    LIT_CHECK_EQ(sink.blocks.size(), std::size_t(6));

    Vec3i const placedPos{0, 64, 0};
    service.markBlockPlaced("Steve", placedPos);
    sink.blocks.erase(placedPos);
    for (int i = 0; i < 10; ++i) service.tick(sink, static_cast<std::uint64_t>(100 + i));
    LIT_CHECK(sink.blocks.find(placedPos) == sink.blocks.end());
    LIT_CHECK_EQ(sink.blocks.size(), std::size_t(5));
}

LIT_TEST(service_slice_follows_feet) {
    prepareServiceDirs();
    LitematicaService service({serviceRoot(), {}, {}, {}});
    LIT_CHECK(service.initialize().ok());
    auto context = playerContext();

    LIT_CHECK(service.execute(context, "place tiny 0 64 0").ok);
    LIT_CHECK(service.execute(context, "layer slice").ok);
    service.updateViewer("Steve", {0.5, 64.0, 0.5}, 0);
    FakeSink sink;
    for (int i = 0; i < 10; ++i) service.tick(sink, static_cast<std::uint64_t>(i));
    LIT_CHECK_EQ(sink.blocks.size(), std::size_t(3));
    for (auto const& [pos, state] : sink.blocks) {
        (void)state;
        LIT_CHECK_EQ(pos.y, 64);
    }

    service.updateViewer("Steve", {0.5, 65.0, 0.5}, 0);
    for (int i = 0; i < 10; ++i) service.tick(sink, static_cast<std::uint64_t>(100 + i));
    LIT_CHECK_EQ(sink.blocks.size(), std::size_t(3));
    for (auto const& [pos, state] : sink.blocks) {
        (void)state;
        LIT_CHECK_EQ(pos.y, 65);
    }
}

LIT_TEST(service_save_keeps_orientation) {
    prepareServiceDirs();
    LitematicaService service({serviceRoot(), {}, {}, {}});
    LIT_CHECK(service.initialize().ok());

    service.setBlockSampler([](std::string_view, Vec3i pos, int) -> std::optional<BlockState> {
        if (pos.y != 64 || pos.z != 0) return std::nullopt;
        if (pos.x == 0) return BlockState{"minecraft:oak_stairs", "upside_down_bit=1,weirdo_direction=3"};
        if (pos.x == 1) return BlockState{"minecraft:oak_log", "pillar_axis=x"};
        return std::nullopt;
    });

    auto context = playerContext();
    auto saved   = service.execute(context, "save oriented 0 64 0 1 64 0");
    LIT_CHECK(saved.ok);
    if (!saved.ok) return;

    BlockMapper mapper;
    auto        loaded = loadLitematicFile(serviceRoot() / "schematics" / "oriented.litematic", mapper);
    LIT_CHECK(loaded.ok());
    if (!loaded.ok()) return;

    bool hasStairs = false;
    bool hasLog    = false;
    for (auto const& block : loaded.value().schematic->blocks) {
        auto const& state = loaded.value().schematic->palette[block.palette].bedrockState;
        if (state == "minecraft:oak_stairs[upside_down_bit=1,weirdo_direction=3]") hasStairs = true;
        if (state == "minecraft:oak_log[pillar_axis=x]") hasLog = true;
    }
    LIT_CHECK(hasStairs);
    LIT_CHECK(hasLog);
}

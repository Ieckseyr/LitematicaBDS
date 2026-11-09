#include "TestHarness.h"

#include <filesystem>
#include <fstream>

#include "litematicabds/Config.h"

using namespace litematicabds;

LIT_TEST(config_defaults_when_missing) {
    auto path = std::filesystem::path("tests") / "out" / "config-missing.json";
    std::filesystem::remove(path);
    auto config = loadConfigFile(path);
    LIT_CHECK(config.ok());
    LIT_CHECK_EQ(config.value().render.blocksPerTick, 256);
    LIT_CHECK_EQ(config.value().render.viewDistance, 64);
    LIT_CHECK_EQ(config.value().storage.schematicsDir, std::string("schematics"));
}

LIT_TEST(config_comments_and_trailing_commas) {
    auto path = std::filesystem::path("tests") / "out" / "config-comments.json";
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << R"({
        // 注释
        "render": {
            "blocksPerTick": 128,
            "viewDistance": 32, /* 块注释 */
            "surfaceOnly": false,
        },
        "projection": { "defaultRotation": 90, },
        "debug": true,
    })";
    out.close();

    std::vector<std::string> warnings;
    auto                     config = loadConfigFile(path, &warnings);
    LIT_CHECK(config.ok());
    LIT_CHECK_EQ(config.value().render.blocksPerTick, 128);
    LIT_CHECK_EQ(config.value().render.viewDistance, 32);
    LIT_CHECK(!config.value().render.surfaceOnly);
    LIT_CHECK_EQ(config.value().projection.defaultRotation, 90);
    LIT_CHECK(config.value().debug);
}

LIT_TEST(config_bad_types_fall_back) {
    auto path = std::filesystem::path("tests") / "out" / "config-bad.json";
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << R"({"render": {"blocksPerTick": "many", "viewDistance": 9999}, "projection": {"defaultRotation": 45}})";
    out.close();

    std::vector<std::string> warnings;
    auto                     config = loadConfigFile(path, &warnings);
    LIT_CHECK(config.ok());
    LIT_CHECK_EQ(config.value().render.blocksPerTick, 256);
    LIT_CHECK_EQ(config.value().render.viewDistance, 256); // clamp
    LIT_CHECK_EQ(config.value().projection.defaultRotation, 0);
    LIT_CHECK(!warnings.empty());
}

LIT_TEST(config_save_roundtrip) {
    auto path = std::filesystem::path("tests") / "out" / "config-save.json";
    std::filesystem::create_directories(path.parent_path());

    Config config;
    config.render.blocksPerTick = 512;
    config.render.viewDistance  = 96;
    config.sulfur.enabled       = false;
    config.sulfur.radius        = 12;
    config.sulfur.visible       = true;
    config.fill.quickFill       = true;
    config.save.maxVolume       = 123456;
    config.storage.exportsDir   = "out-exports";
    config.debug                = true;

    auto saved = saveConfigFile(path, config);
    LIT_CHECK(saved.ok());

    auto loaded = loadConfigFile(path);
    LIT_CHECK(loaded.ok());
    if (!loaded.ok()) return;
    LIT_CHECK_EQ(loaded.value().render.blocksPerTick, 512);
    LIT_CHECK_EQ(loaded.value().render.viewDistance, 96);
    LIT_CHECK(!loaded.value().sulfur.enabled);
    LIT_CHECK_EQ(loaded.value().sulfur.radius, 12);
    LIT_CHECK(loaded.value().sulfur.visible);
    LIT_CHECK(loaded.value().fill.quickFill);
    LIT_CHECK_EQ(loaded.value().save.maxVolume, 123456);
    LIT_CHECK_EQ(loaded.value().storage.exportsDir, std::string("out-exports"));
    LIT_CHECK(loaded.value().debug);
}

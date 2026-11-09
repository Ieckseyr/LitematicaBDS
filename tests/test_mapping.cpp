#include "TestHarness.h"

#include <string>

#include "litematicabds/Mapping.h"

using namespace litematicabds;

LIT_TEST(mapping_table_is_loaded) {
    BlockMapper mapper;
    LIT_CHECK(mapper.tableSize() > 800);
}

LIT_TEST(mapping_passthrough_unknown) {
    BlockMapper mapper;
    bool        known = true;
    auto        state = mapper.javaToBedrock("mymod:mystery_block", "a=b", &known);
    LIT_CHECK(!known);
    LIT_CHECK_EQ(state.name, std::string("mymod:mystery_block"));
    LIT_CHECK_EQ(state.properties, std::string(""));
}

LIT_TEST(mapping_log_axis) {
    BlockMapper mapper;
    bool        known = false;
    auto        state = mapper.javaToBedrock("minecraft:oak_log", "axis=y", &known);
    LIT_CHECK(known);
    LIT_CHECK_EQ(state.name, std::string("minecraft:oak_log"));
    LIT_CHECK_EQ(state.properties, std::string("pillar_axis=y"));
}

LIT_TEST(mapping_stairs) {
    BlockMapper mapper;
    auto        state = mapper.javaToBedrock(
        "minecraft:oak_stairs",
        "facing=north,half=top,shape=straight,waterlogged=false"
    );
    LIT_CHECK_EQ(state.name, std::string("minecraft:oak_stairs"));
    LIT_CHECK_EQ(state.properties, std::string("upside_down_bit=1,weirdo_direction=3"));
}

LIT_TEST(mapping_door) {
    BlockMapper mapper;
    auto        state = mapper.javaToBedrock(
        "minecraft:oak_door",
        "facing=east,half=upper,hinge=right,open=true,powered=false"
    );
    LIT_CHECK_EQ(state.name, std::string("minecraft:wooden_door"));
    LIT_CHECK_EQ(
        state.properties,
        std::string("direction=0,door_hinge_bit=1,open_bit=1,upper_block_bit=1")
    );
}

LIT_TEST(mapping_repeater) {
    BlockMapper mapper;
    auto        state = mapper.javaToBedrock(
        "minecraft:repeater",
        "delay=3,facing=west,locked=false,powered=true"
    );
    LIT_CHECK_EQ(state.name, std::string("minecraft:unpowered_repeater"));
    LIT_CHECK_EQ(
        state.properties,
        std::string("direction=1,locked_bit=0,powered_bit=1,repeater_delay=2")
    );
}

LIT_TEST(mapping_reverse_basic) {
    BlockMapper mapper;
    auto        java = mapper.bedrockToJava({"minecraft:oak_log", "pillar_axis=x"});
    LIT_CHECK_EQ(java.name, std::string("minecraft:oak_log"));
    LIT_CHECK_EQ(java.properties, std::string("axis=x"));
}

LIT_TEST(mapping_bedrock_name_aliases) {
    BlockMapper mapper;
    LIT_CHECK_EQ(mapper.javaToBedrock("minecraft:dirt_path", "").name, std::string("minecraft:grass_path"));
    LIT_CHECK_EQ(mapper.javaToBedrock("minecraft:grass_block", "").name, std::string("minecraft:grass_block"));
    LIT_CHECK_EQ(mapper.javaToBedrock("minecraft:terracotta", "").name, std::string("minecraft:hardened_clay"));
    LIT_CHECK_EQ(
        mapper.javaToBedrock("minecraft:small_dripleaf", "").name,
        std::string("minecraft:small_dripleaf_block")
    );
    LIT_CHECK_EQ(
        mapper.javaToBedrock("minecraft:end_stone_brick_slab", "").name,
        std::string("minecraft:end_stone_brick_slab")
    );
}

LIT_TEST(mapping_cauldron) {
    BlockMapper mapper;
    auto        state = mapper.javaToBedrock("minecraft:powder_snow_cauldron", "level=2");
    LIT_CHECK_EQ(state.name, std::string("minecraft:cauldron"));
    LIT_CHECK_EQ(state.properties, std::string("cauldron_liquid=powder_snow,fill_level=2"));

    auto lava = mapper.javaToBedrock("minecraft:lava_cauldron", "level=3");
    LIT_CHECK_EQ(lava.name, std::string("minecraft:cauldron"));
    LIT_CHECK_EQ(lava.properties, std::string("cauldron_liquid=lava,fill_level=3"));
}


LIT_TEST(mapping_reverse_stairs) {
    BlockMapper mapper;
    auto        java = mapper.bedrockToJava(
        {"minecraft:oak_stairs", "upside_down_bit=1,weirdo_direction=3"}
    );
    LIT_CHECK_EQ(java.name, std::string("minecraft:oak_stairs"));
    LIT_CHECK_EQ(java.properties, std::string("facing=north,half=top,shape=straight,waterlogged=false"));
}

LIT_TEST(mapping_reverse_door) {
    BlockMapper mapper;
    auto        java = mapper.bedrockToJava(
        {"minecraft:wooden_door", "direction=0,door_hinge_bit=1,open_bit=1,upper_block_bit=1"}
    );
    LIT_CHECK_EQ(java.name, std::string("minecraft:oak_door"));
    LIT_CHECK_EQ(
        java.properties,
        std::string("facing=east,half=upper,hinge=right,open=true,powered=false")
    );
}

LIT_TEST(mapping_reverse_slab_chest_piston) {
    BlockMapper mapper;
    auto        slab = mapper.bedrockToJava({"minecraft:oak_slab", "vertical_half=top"});
    LIT_CHECK_EQ(slab.name, std::string("minecraft:oak_slab"));
    LIT_CHECK_EQ(slab.properties, std::string("type=top,waterlogged=false"));

    auto chest = mapper.bedrockToJava({"minecraft:chest", "cardinal_direction=north"});
    LIT_CHECK_EQ(chest.name, std::string("minecraft:chest"));
    LIT_CHECK_EQ(chest.properties, std::string("facing=north,type=single,waterlogged=false"));

    auto piston = mapper.bedrockToJava({"minecraft:piston", "extended_bit=1,facing_direction=2"});
    LIT_CHECK_EQ(piston.name, std::string("minecraft:piston"));
    LIT_CHECK_EQ(piston.properties, std::string("extended=true,facing=north"));
}

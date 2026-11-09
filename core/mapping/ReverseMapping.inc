// Bedrock -> Java state reverse mapping (included inside BlockMapper.cpp's anonymous namespace).

std::string cleanStateKey(std::string_view key) {
    constexpr std::string_view prefix = "minecraft:";
    if (key.size() > prefix.size() && key.substr(0, prefix.size()) == prefix) {
        key.remove_prefix(prefix.size());
    }
    return std::string(key);
}

int scoreEntry(detail::MapEntry const& entry, Props const& be, std::string_view shortName) {
    auto const javaBase = stripMc(entry.javaName);
    int        score    = javaBase == shortName ? 2 : 0;
    static constexpr std::string_view kVariantKeys[] = {
        "wood_type",      "old_log_type",  "old_leaf_type",        "color",
        "sand_type",      "stone_type",    "dirt_type",            "flower_type",
        "double_plant_type", "monster_egg_stone_type", "prismarine_block_type", "coral_color",
    };
    for (auto key : kVariantKeys) {
        auto const value = get(be, key);
        if (!value) continue;
        if (javaBase == *value) {
            score += 20;
        } else if (javaBase.size() > value->size() && javaBase.substr(0, value->size()) == *value
                   && javaBase[value->size()] == '_') {
            score += 10;
        } else if (javaBase.size() > value->size() + 1
                   && javaBase.substr(javaBase.size() - value->size()) == *value
                   && javaBase[javaBase.size() - value->size() - 1] == '_') {
            score += 5;
        }
    }
    return score;
}

detail::MapEntry const* chooseEntry(
    std::vector<detail::MapEntry const*> const& candidates,
    Props const&                                be,
    std::string_view                            shortName
) {
    detail::MapEntry const* best      = candidates.front();
    int                     bestScore = -1;
    for (auto const* entry : candidates) {
        int const score = scoreEntry(*entry, be, shortName);
        if (score > bestScore) {
            best      = entry;
            bestScore = score;
        }
    }
    return best;
}

BlockState reverseMapBedrock(
    std::vector<detail::MapEntry const*> const& candidates,
    std::string_view                            shortName,
    Props const&                                be
) {
    auto const* entry    = chooseEntry(candidates, be, shortName);
    auto const& javaName = entry->javaName;
    auto const  base     = stripMc(javaName);
    Props       js       = be;

    // registry state reverse rename
    if (entry->stateMap != nullptr && *entry->stateMap != '\0') {
        std::string_view mapText(entry->stateMap);
        std::size_t      start = 0;
        while (start <= mapText.size()) {
            auto const comma = mapText.find(',', start);
            auto const end   = comma == std::string_view::npos ? mapText.size() : comma;
            auto const part  = trim(mapText.substr(start, end - start));
            auto const colon = part.find(':');
            if (colon != std::string_view::npos) {
                auto const javaKey = std::string(trim(part.substr(0, colon)));
                auto const beKey   = std::string(trim(part.substr(colon + 1)));
                if (auto const value = get(be, beKey)) js[javaKey] = std::string(*value);
            }
            if (comma == std::string_view::npos) break;
            start = comma + 1;
        }
    }

    auto add = [&](std::string key, std::string value) { js[std::move(key)] = std::move(value); };
    auto boolText = [](bool value) { return std::string(value ? "true" : "false"); };
    auto facing6 = [&](std::string_view key) -> std::string {
        static constexpr char const* kFacing[] = {"down", "up", "north", "south", "west", "east"};
        return kFacing[std::clamp(asInt(be, key, 0), 0, 5)];
    };
    auto facing4 = [&](std::string_view key, char const* const* table) -> std::string {
        return table[std::clamp(asInt(be, key, 0), 0, 3)];
    };

    static constexpr char const* kCardinal4[] = {"south", "west", "north", "east"};
    static constexpr char const* kStair4[]    = {"east", "west", "south", "north"};
    static constexpr char const* kDoor4[]     = {"east", "south", "west", "north"};
    static constexpr char const* kTrapdoor4[] = {"south", "west", "north", "east"};
    static constexpr char const* kBed4[]      = {"east", "north", "west", "south"};
    static constexpr char const* kButton6[]   = {"down", "up", "south", "north", "east", "west"};
    static constexpr char const* kRailShapes[] = {
        "north_south", "east_west", "ascending_east", "ascending_west", "ascending_north",
        "ascending_south", "south_east", "south_west", "north_west", "north_east",
    };

    if (endsWith(base, "_stairs")) {
        add("facing", facing4("weirdo_direction", kStair4));
        add("half", asBool(be, "upside_down_bit") ? "top" : "bottom");
        add("shape", std::string(get(be, "corner").value_or("straight")));
        add("waterlogged", "false");
        eraseAll(js, {"weirdo_direction", "upside_down_bit", "corner"});
    } else if (endsWith(base, "_slab") || endsWith(base, "_double_slab") || endsWith(shortName, "_double_slab")) {
        add(
            "type",
            endsWith(shortName, "_double_slab") ? "double"
                                                : std::string(get(be, "vertical_half").value_or("bottom"))
        );
        add("waterlogged", "false");
        eraseAll(js, {"vertical_half", "top_slot_bit"});
    } else if (endsWith(base, "_door")) {
        add("facing", facing4("direction", kDoor4));
        add("half", asBool(be, "upper_block_bit") ? "upper" : "lower");
        add("hinge", asBool(be, "door_hinge_bit") ? "right" : "left");
        add("open", boolText(asBool(be, "open_bit")));
        add("powered", "false");
        eraseAll(js, {"direction", "upper_block_bit", "door_hinge_bit", "open_bit"});
    } else if (endsWith(base, "_trapdoor")) {
        add("facing", facing4("direction", kTrapdoor4));
        add("half", asBool(be, "upside_down_bit") ? "top" : "bottom");
        add("open", boolText(asBool(be, "open_bit")));
        add("powered", "false");
        add("waterlogged", "false");
        eraseAll(js, {"direction", "upside_down_bit", "open_bit"});
    } else if (contains(base, "_bed")) {
        add("facing", facing4("direction", kBed4));
        add("part", asBool(be, "head_piece_bit") ? "head" : "foot");
        add("occupied", boolText(asBool(be, "occupied_bit")));
        eraseAll(js, {"direction", "head_piece_bit", "occupied_bit"});
    } else if (contains(base, "_button")) {
        int const dir = asInt(be, "facing_direction", 2);
        add("face", dir == 0 ? "ceiling" : (dir == 1 ? "floor" : "wall"));
        add("facing", kButton6[std::clamp(dir, 0, 5)]);
        add("powered", boolText(asBool(be, "button_pressed_bit")));
        eraseAll(js, {"facing_direction", "button_pressed_bit"});
    } else if (base == "lever") {
        auto const lever = std::string(get(be, "lever_direction").value_or("north"));
        std::string face   = "wall";
        std::string facing = lever;
        if (lever == "up_north_south") {
            face = "ceiling";
            facing = "north";
        } else if (lever == "up_east_west") {
            face = "ceiling";
            facing = "east";
        } else if (lever == "down_north_south") {
            face = "floor";
            facing = "north";
        } else if (lever == "down_east_west") {
            face = "floor";
            facing = "east";
        }
        add("face", face);
        add("facing", facing);
        add("powered", boolText(asBool(be, "open_bit")));
        eraseAll(js, {"lever_direction", "open_bit"});
    } else if (contains(base, "fence_gate")) {
        add("facing", facing4("direction", kCardinal4));
        add("open", boolText(asBool(be, "open_bit")));
        add("in_wall", boolText(asBool(be, "in_wall_bit")));
        add("powered", "false");
        eraseAll(js, {"direction", "open_bit", "in_wall_bit"});
    } else if (base == "rail" || endsWith(base, "_rail")) {
        int const idx = std::clamp(asInt(be, "rail_direction", 0), 0, 9);
        add("shape", kRailShapes[idx]);
        if (get(be, "rail_data_bit")) add("powered", boolText(asBool(be, "rail_data_bit")));
        add("waterlogged", "false");
        eraseAll(js, {"rail_direction", "rail_data_bit"});
    } else if (contains(base, "repeater")) {
        add("facing", facing4("direction", kCardinal4));
        add("delay", std::to_string(std::clamp(asInt(be, "repeater_delay", 0) + 1, 1, 4)));
        add("locked", boolText(asBool(be, "locked_bit")));
        add("powered", boolText(asBool(be, "powered_bit")));
        eraseAll(js, {"direction", "repeater_delay", "locked_bit", "powered_bit"});
    } else if (contains(base, "comparator")) {
        add("facing", facing4("direction", kCardinal4));
        add("mode", asBool(be, "output_subtract_bit") ? "subtract" : "compare");
        add("powered", boolText(asBool(be, "output_lit_bit")));
        eraseAll(js, {"direction", "output_subtract_bit", "output_lit_bit"});
    } else if (contains(base, "torch")) {
        auto const dir = std::string(get(be, "torch_facing_direction").value_or("top"));
        if (endsWith(base, "wall_torch")) add("facing", dir == "top" ? "north" : dir);
        if (contains(base, "redstone_torch")) add("lit", "true");
        eraseAll(js, {"torch_facing_direction", "lit"});
    } else if (contains(base, "pumpkin")) {
        add("facing", facing4("direction", kCardinal4));
        eraseAll(js, {"direction"});
    } else if (contains(base, "piston")) {
        add("facing", facing6("facing_direction"));
        add("extended", boolText(asBool(be, "extended_bit")));
        eraseAll(js, {"facing_direction", "extended_bit"});
    } else if (base == "dispenser" || base == "dropper") {
        add("facing", facing6("facing_direction"));
        add("triggered", boolText(asBool(be, "triggered_bit")));
        eraseAll(js, {"facing_direction", "triggered_bit"});
    } else if (base == "observer") {
        add("facing", facing6("facing_direction"));
        add("powered", boolText(asBool(be, "powered_bit")));
        eraseAll(js, {"facing_direction", "powered_bit"});
    } else if (base == "hopper") {
        add("facing", facing6("facing_direction"));
        add("enabled", boolText(!asBool(be, "toggle_bit")));
        eraseAll(js, {"facing_direction", "toggle_bit"});
    } else if (base == "furnace" || base == "smoker" || base == "blast_furnace") {
        add("facing", facing6("facing_direction"));
        if (get(be, "lit")) add("lit", boolText(asBool(be, "lit")));
        eraseAll(js, {"facing_direction", "lit"});
    } else if (base == "chest" || endsWith(base, "_chest") || base == "trapped_chest") {
        add("facing", std::string(get(be, "cardinal_direction").value_or("north")));
        add("type", "single");
        add("waterlogged", "false");
        eraseAll(js, {"cardinal_direction"});
    } else if (base == "barrel") {
        add("facing", facing6("facing_direction"));
        add("open", "false");
        eraseAll(js, {"facing_direction"});
    } else if (contains(base, "_log") || contains(base, "_wood") || contains(base, "_stem")
               || base == "basalt" || base == "bone_block" || base == "hay_block"
               || base == "polished_basalt") {
        add("axis", std::string(get(be, "pillar_axis").value_or("y")));
        eraseAll(js, {"pillar_axis"});
    } else if (contains(base, "leaves")) {
        add("persistent", boolText(asBool(be, "persistent_bit")));
        add("distance", "7");
        add("waterlogged", "false");
        eraseAll(js, {"persistent_bit", "update_bit"});
    } else if (base == "snow") {
        add("layers", std::to_string(std::clamp(asInt(be, "height", 0) + 1, 1, 8)));
        eraseAll(js, {"height"});
    } else if (base == "cake") {
        add("bites", std::to_string(asInt(be, "bite_counter", 0)));
        eraseAll(js, {"bite_counter"});
    } else if (base == "composter") {
        add("level", std::to_string(asInt(be, "composter_fill_level", 0)));
        eraseAll(js, {"composter_fill_level"});
    } else if (base == "water" || base == "lava" || base == "flowing_water" || base == "flowing_lava") {
        add("level", std::to_string(asInt(be, "liquid_depth", 0)));
        eraseAll(js, {"liquid_depth"});
    } else if (contains(base, "candle")) {
        add("candles", std::to_string(std::clamp(asInt(be, "candles", 1), 1, 4)));
        add("lit", boolText(asBool(be, "lit_bit")));
        add("waterlogged", "false");
        eraseAll(js, {"lit_bit"});
    } else if (base == "sea_pickle") {
        add("pickles", std::to_string(std::clamp(asInt(be, "cluster_count", 1), 1, 4)));
        add("waterlogged", "false");
        eraseAll(js, {"cluster_count"});
    } else if (base == "turtle_egg") {
        add("eggs", std::to_string(std::clamp(asInt(be, "egg_count", 1), 1, 4)));
        eraseAll(js, {"egg_count"});
    } else if (base == "sweet_berry_bush") {
        add("age", std::to_string(std::clamp(asInt(be, "growth", 0), 0, 3)));
        eraseAll(js, {"growth"});
    } else if (base == "redstone_wire") {
        add("power", std::to_string(std::clamp(asInt(be, "redstone_signal", 0), 0, 15)));
        for (auto side : {"north", "south", "east", "west"}) {
            if (!js.contains(side)) js[side] = "none";
        }
        eraseAll(js, {"redstone_signal"});
    } else if (base == "bamboo") {
        if (get(be, "bamboo_leaf_size")) add("leaves", std::string(*get(be, "bamboo_leaf_size")));
        eraseAll(js, {"bamboo_leaf_size"});
    } else if (base == "big_dripleaf") {
        if (get(be, "big_dripleaf_tilt")) add("tilt", std::string(*get(be, "big_dripleaf_tilt")));
        add("waterlogged", "false");
        eraseAll(js, {"big_dripleaf_tilt"});
    } else if (contains(base, "wall_sign") || base == "standing_sign") {
        if (get(be, "ground_sign_direction")) {
            add("rotation", std::to_string(asInt(be, "ground_sign_direction", 0)));
        } else if (get(be, "facing_direction")) {
            add("facing", facing6("facing_direction"));
        }
        add("waterlogged", "false");
        eraseAll(js, {"ground_sign_direction", "facing_direction"});
    }

    else if (base == "tall_grass" || base == "large_fern" || base == "sunflower" || base == "lilac"
             || base == "rose_bush" || base == "peony") {
        add("half", asBool(be, "upper_block_bit") ? "upper" : "lower");
        eraseAll(js, {"upper_block_bit"});
    } else if (base == "iron_bars") {
        for (auto side : {"north", "south", "east", "west"}) {
            auto bit = std::string(side) + "_bit";
            if (auto value = get(be, bit)) js[side] = boolText(asBool(be, bit));
            if (!js.contains(side)) js[side] = "false";
        }
        add("waterlogged", "false");
    } else if (endsWith(base, "_pressure_plate")) {
        add("powered", boolText(asBool(be, "powered_bit") || asInt(be, "redstone_signal", 0) > 0));
        eraseAll(js, {"powered_bit", "redstone_signal"});
    } else if (endsWith(base, "_fence") || (contains(base, "fence") && !contains(base, "fence_gate"))) {
        for (auto side : {"north", "south", "east", "west"}) {
            auto bit = std::string(side) + "_bit";
            if (auto value = get(be, bit)) js[side] = boolText(asBool(be, bit));
            if (!js.contains(side)) js[side] = "false";
        }
        add("waterlogged", "false");
    } else if (contains(base, "_wall") && !contains(base, "wall_sign") && !contains(base, "wall_torch")) {
        static constexpr std::pair<char const*, char const*> kSides[] = {
            {"north", "wall_connection_type_north"},
            {"south", "wall_connection_type_south"},
            {"east",  "wall_connection_type_east"},
            {"west",  "wall_connection_type_west"},
        };
        for (auto const& [side, key] : kSides) {
            auto value = std::string(get(be, key).value_or("none"));
            if (value == "short") value = "low";
            js[side] = value;
        }
        add("up", boolText(asBool(be, "wall_post_bit")));
        add("waterlogged", "false");
    } else if (endsWith(base, "_pane")) {
        for (auto side : {"north", "south", "east", "west"}) {
            auto bit = std::string(side) + "_bit";
            if (auto value = get(be, bit)) js[side] = boolText(asBool(be, bit));
            if (!js.contains(side)) js[side] = "false";
        }
        add("waterlogged", "false");
    }

    static constexpr std::string_view kBedrockOnly[] = {
        "facing_direction", "direction", "weirdo_direction", "upside_down_bit", "open_bit",
        "upper_block_bit", "door_hinge_bit", "button_pressed_bit", "toggle_bit", "powered_bit",
        "triggered_bit", "extended_bit", "output_subtract_bit", "output_lit_bit", "repeater_delay",
        "rail_direction", "rail_data_bit", "lever_direction", "torch_facing_direction",
        "redstone_signal", "growth", "bite_counter", "composter_fill_level", "height",
        "cluster_count", "egg_count", "lit_bit", "in_wall_bit", "vertical_half", "liquid_depth",
        "persistent_bit", "update_bit", "pillar_axis", "wall_post_bit", "cardinal_direction",
        "corner", "multi_face_direction", "top_slot_bit", "block_light_level",
        "wall_connection_type_north", "wall_connection_type_south", "wall_connection_type_east",
        "wall_connection_type_west", "north_bit", "south_bit", "east_bit", "west_bit", "wood_type", "old_log_type", "old_leaf_type", "color",
        "sand_type", "stone_type", "dirt_type", "flower_type", "double_plant_type",
        "monster_egg_stone_type", "prismarine_block_type", "coral_color",
    };
    for (auto key : kBedrockOnly) js.erase(std::string(key));

    return {std::string(javaName), canonical(js)};
}

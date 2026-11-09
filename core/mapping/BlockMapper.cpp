#include "litematicabds/Mapping.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "BlockMapData.inc"

namespace litematicabds {
namespace {

namespace detail = mapping::detail;

using Props = std::map<std::string, std::string, std::less<>>;

std::string_view stripMc(std::string_view name) {
    constexpr std::string_view prefix = "minecraft:";
    return name.size() > prefix.size() && name.substr(0, prefix.size()) == prefix ? name.substr(prefix.size()) : name;
}

bool endsWith(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

bool contains(std::string_view s, std::string_view part) { return s.find(part) != std::string_view::npos; }

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

Props parseProps(std::string_view text) {
    Props out;
    std::size_t start = 0;
    while (start <= text.size()) {
        auto const comma = text.find(',', start);
        auto const end   = comma == std::string_view::npos ? text.size() : comma;
        auto const part  = trim(text.substr(start, end - start));
        if (!part.empty()) {
            auto const eq = part.find('=');
            if (eq != std::string_view::npos && eq > 0) {
                out[std::string(trim(part.substr(0, eq)))] = std::string(trim(part.substr(eq + 1)));
            }
        }
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return out;
}

std::string canonical(Props const& props) {
    std::string out;
    for (auto const& [key, value] : props) {
        if (!out.empty()) out.push_back(',');
        out += key;
        out.push_back('=');
        out += value;
    }
    return out;
}

std::optional<std::string_view> get(Props const& props, std::string_view key) {
    auto const it = props.find(key);
    if (it == props.end()) return std::nullopt;
    return std::string_view(it->second);
}

bool valueIsTrue(std::string_view value) { return value == "true" || value == "1"; }

bool asBool(Props const& props, std::string_view key, bool fallback = false) {
    auto const value = get(props, key);
    if (!value) return fallback;
    if (valueIsTrue(*value)) return true;
    if (*value == "false" || *value == "0") return false;
    return fallback;
}

int asInt(Props const& props, std::string_view key, int fallback = 0) {
    auto const value = get(props, key);
    if (!value) return fallback;
    try {
        return std::stoi(std::string(*value));
    } catch (...) {
        return fallback;
    }
}

void eraseAll(Props& props, std::initializer_list<std::string_view> keys) {
    for (auto key : keys) props.erase(std::string(key));
}

// ── facing 映射表（与 LitematicaBE 的 BlockStateConverters 对齐）──────────────
int facingToBedrock(std::string_view facing) {
    // 0=down 1=up 2=north 3=south 4=west 5=east
    if (facing == "down") return 0;
    if (facing == "up") return 1;
    if (facing == "north") return 2;
    if (facing == "south") return 3;
    if (facing == "west") return 4;
    if (facing == "east") return 5;
    return -1;
}

int cardinalToBedrock(std::string_view facing) {
    // 0=south 1=west 2=north 3=east
    if (facing == "south") return 0;
    if (facing == "west") return 1;
    if (facing == "north") return 2;
    if (facing == "east") return 3;
    return -1;
}

int stairToBedrock(std::string_view facing) {
    // 0=east 1=west 2=south 3=north
    if (facing == "east") return 0;
    if (facing == "west") return 1;
    if (facing == "south") return 2;
    if (facing == "north") return 3;
    return -1;
}

int doorToBedrock(std::string_view facing) {
    // 0=east 1=south 2=west 3=north
    if (facing == "east") return 0;
    if (facing == "south") return 1;
    if (facing == "west") return 2;
    if (facing == "north") return 3;
    return -1;
}

int buttonToBedrock(std::string_view facing) {
    // 0=down 1=up 2=south 3=north 4=east 5=west
    if (facing == "down") return 0;
    if (facing == "up") return 1;
    if (facing == "south") return 2;
    if (facing == "north") return 3;
    if (facing == "east") return 4;
    if (facing == "west") return 5;
    return -1;
}

int bedToBedrock(std::string_view facing) {
    if (facing == "south") return 3;
    if (facing == "north") return 1;
    if (facing == "east") return 0;
    if (facing == "west") return 2;
    return -1;
}

int anvilToBedrock(std::string_view facing) {
    if (facing == "south") return 0;
    if (facing == "west") return 1;
    if (facing == "north") return 2;
    if (facing == "east") return 3;
    return -1;
}

std::optional<std::string> torchToBedrock(std::string_view facing) {
    if (facing == "east" || facing == "west" || facing == "south" || facing == "north") {
        return std::string(facing);
    }
    if (facing == "up") return std::string("top");
    return std::nullopt;
}

int railShapeIndex(std::string_view shape) {
    static constexpr std::string_view kShapes[] = {
        "north_south",
        "east_west",
        "ascending_east",
        "ascending_west",
        "ascending_north",
        "ascending_south",
        "south_east",
        "south_west",
        "north_west",
        "north_east",
    };
    for (std::size_t i = 0; i < std::size(kShapes); ++i) {
        if (shape == kShapes[i]) return static_cast<int>(i);
    }
    return -1;
}

bool isColoredBlock(std::string_view javaName, std::string_view suffix) { return endsWith(javaName, suffix); }

// 颜色提取：white_wool / light_blue_concrete -> color=white / light_blue
void extractColor(std::string_view javaName, Props& be) {
    static constexpr std::string_view kColors[] = {
        "white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray",
        "light_gray", "cyan", "purple", "blue", "brown", "green", "red", "black",
    };
    static constexpr std::string_view kTypes[] = {
        "_wool", "_carpet", "_terracotta", "_concrete", "_concrete_powder", "_stained_glass",
        "_stained_glass_pane", "_banner", "_wall_banner", "_bed", "_shulker_box", "_candle", "_candle_cake",
    };
    for (auto type : kTypes) {
        if (!isColoredBlock(javaName, type)) continue;
        auto const base   = stripMc(javaName);
        auto const prefix = base.substr(0, base.size() - type.size());
        for (auto color : kColors) {
            if (prefix == color) {
                be["color"] = std::string(color);
                return;
            }
        }
    }
}

// 1.21 之后 BDS 有几处方块改名；生成表保留原 LitematicaBE 的名字，这里做运行期别名。
// 名字均已在 26.60 服务端用 Block::tryGetFromRegistry 验证存在。
std::string_view bedrockNameAlias(std::string_view javaName, std::string_view bedrockName) {
    static constexpr std::pair<std::string_view, std::string_view> kAliases[] = {
        {"minecraft:grass_block", "grass_block"},
        {"minecraft:dirt_path", "grass_path"},
        {"minecraft:end_stone_brick_slab", "end_stone_brick_slab"},
        {"minecraft:end_stone_brick_wall", "end_stone_brick_wall"},
        {"minecraft:prismarine_brick_stairs", "prismarine_bricks_stairs"},
        {"minecraft:terracotta", "hardened_clay"},
        {"minecraft:small_dripleaf", "small_dripleaf_block"},
        {"minecraft:powder_snow_cauldron", "cauldron"},
        {"minecraft:lava_cauldron", "cauldron"},
    };
    for (auto const& [java, bedrock] : kAliases) {
        if (javaName == java) return bedrock;
    }
    return bedrockName;
}

void applySpecial(std::string_view javaName, Props const& js, Props& be) {
    extractColor(javaName, be);

    // 台阶：half -> upside_down_bit；shape 基岩版没有对应状态
    if (endsWith(javaName, "_stairs")) {
        be["upside_down_bit"] = get(js, "half").value_or("bottom") == "top" ? "1" : "0";
        eraseAll(be, {"half", "shape", "waterlogged"});
        return;
    }

    // 半砖：type=bottom/top -> vertical_half。double 由调用方改方块名（见 javaToBedrock）。
    if (endsWith(javaName, "_slab") || javaName == "minecraft:smooth_stone_slab") {
        auto const type = get(js, "type").value_or("bottom");
        if (type != "double") be["vertical_half"] = std::string(type);
        eraseAll(be, {"type", "waterlogged"});
        return;
    }

    // 炼药锅：Java 用 level，基岩版用 cauldron_liquid + fill_level
    if (javaName == "minecraft:powder_snow_cauldron" || javaName == "minecraft:water_cauldron"
        || javaName == "minecraft:lava_cauldron") {
        be["cauldron_liquid"] = javaName == "minecraft:powder_snow_cauldron"
                                  ? "powder_snow"
                                  : (javaName == "minecraft:lava_cauldron" ? "lava" : "water");
        be["fill_level"] = std::to_string(asInt(js, "level", 1));
        eraseAll(be, {"level"});
        return;
    }

    // 门
    if (endsWith(javaName, "_door")) {
        auto const facing = get(js, "facing").value_or("north");
        int const  dir    = doorToBedrock(facing);
        be["direction"]       = std::to_string(dir >= 0 ? dir : 0);
        be["open_bit"]        = asBool(js, "open") ? "1" : "0";
        be["upper_block_bit"] = get(js, "half").value_or("lower") == "upper" ? "1" : "0";
        be["door_hinge_bit"]  = get(js, "hinge").value_or("left") == "right" ? "1" : "0";
        eraseAll(be, {"facing", "open", "half", "hinge", "powered"});
        return;
    }

    // 活板门
    if (endsWith(javaName, "_trapdoor") || javaName == "minecraft:iron_trapdoor"
        || javaName == "minecraft:copper_trapdoor") {
        auto const facing = get(js, "facing").value_or("north");
        int const  dir    = cardinalToBedrock(facing);
        be["direction"]     = std::to_string(dir >= 0 ? dir : 0);
        be["open_bit"]      = asBool(js, "open") ? "1" : "0";
        be["upside_down_bit"] = get(js, "half").value_or("bottom") == "top" ? "1" : "0";
        eraseAll(be, {"facing", "open", "half", "powered", "waterlogged"});
        return;
    }

    // 床
    if (contains(javaName, "_bed") || javaName == "minecraft:bed") {
        auto const facing = get(js, "facing").value_or("north");
        int const  dir    = bedToBedrock(facing);
        be["direction"]       = std::to_string(dir >= 0 ? dir : 3);
        be["head_piece_bit"]  = get(js, "part").value_or("foot") == "head" ? "1" : "0";
        be["occupied_bit"]    = asBool(js, "occupied") ? "1" : "0";
        eraseAll(be, {"facing", "part", "occupied"});
        return;
    }

    // 拉杆
    if (javaName == "minecraft:lever") {
        auto const facing = get(js, "facing").value_or("north");
        auto const face   = get(js, "face").value_or("wall");
        std::string leverDir;
        if (face == "wall") {
            int const dir = cardinalToBedrock(facing);
            static constexpr char const* kDirs[] = {"south", "west", "north", "east"};
            leverDir = kDirs[dir >= 0 ? dir : 0];
        } else if (face == "ceiling") {
            leverDir = (facing == "north" || facing == "south") ? "up_north_south" : "up_east_west";
        } else {
            leverDir = (facing == "north" || facing == "south") ? "down_north_south" : "down_east_west";
        }
        be["lever_direction"] = leverDir;
        be["open_bit"]        = asBool(js, "powered") ? "1" : "0";
        eraseAll(be, {"facing", "powered"});
        return;
    }

    // 栅栏门
    if (contains(javaName, "fence_gate")) {
        auto const facing = get(js, "facing").value_or("north");
        int const  dir    = cardinalToBedrock(facing);
        be["direction"]  = std::to_string(dir >= 0 ? dir : 0);
        be["open_bit"]   = asBool(js, "open") ? "1" : "0";
        be["in_wall_bit"] = asBool(js, "in_wall") ? "1" : "0";
        eraseAll(be, {"facing", "open", "in_wall", "powered"});
        return;
    }

    // 铁轨
    if (javaName == "minecraft:rail") {
        auto const shape = get(js, "shape").value_or("north_south");
        int const  idx   = railShapeIndex(shape);
        be["rail_direction"] = std::to_string(idx >= 0 ? idx : 0);
        eraseAll(be, {"shape"});
        return;
    }
    if (contains(javaName, "_rail") && javaName != "minecraft:rail") {
        auto const shape = get(js, "shape").value_or("north_south");
        int const  idx   = railShapeIndex(shape);
        be["rail_direction"] = std::to_string(idx >= 0 ? idx : 0);
        if (get(js, "powered")) {
            be["rail_data_bit"] = asBool(js, "powered") ? "1" : "0";
            eraseAll(be, {"powered"});
        }
        eraseAll(be, {"shape"});
        return;
    }

    // 红石中继器
    if (contains(javaName, "repeater") || javaName == "minecraft:powered_repeater") {
        int const delay = std::clamp(asInt(js, "delay", 1) - 1, 0, 3);
        be["repeater_delay"] = std::to_string(delay);
        be["powered_bit"]    = contains(javaName, "powered")
                                 ? "1"
                                 : (asBool(js, "powered") ? "1" : "0");
        if (get(js, "locked")) {
            be["locked_bit"] = asBool(js, "locked") ? "1" : "0";
            eraseAll(be, {"locked"});
        }
        eraseAll(be, {"delay", "powered"});
        return;
    }

    // 红石比较器
    if (contains(javaName, "comparator")) {
        be["output_subtract_bit"] = get(js, "mode").value_or("compare") == "subtract" ? "1" : "0";
        be["output_lit_bit"]      = contains(javaName, "powered")
                                      ? "1"
                                      : (asBool(js, "powered") ? "1" : "0");
        eraseAll(be, {"mode", "powered"});
        return;
    }

    if (javaName == "minecraft:cake") {
        be["bite_counter"] = std::to_string(std::min(6, asInt(js, "bites", 0)));
        eraseAll(be, {"bites"});
        return;
    }
    if (javaName == "minecraft:composter") {
        be["composter_fill_level"] = std::to_string(std::min(8, asInt(js, "level", 0)));
        eraseAll(be, {"level"});
        return;
    }
    if (javaName == "minecraft:snow") {
        be["height"] = std::to_string(std::clamp(asInt(js, "layers", 1), 1, 8) - 1);
        eraseAll(be, {"layers"});
        return;
    }
    if (javaName == "minecraft:chorus_flower") {
        be["age"] = std::to_string(asInt(js, "age", 0));
        return;
    }
    if (contains(javaName, "candle")) {
        be["candles"] = std::to_string(std::clamp(asInt(js, "candles", 1), 1, 4));
        be["lit_bit"] = asBool(js, "lit") ? "1" : "0";
        eraseAll(be, {"lit", "waterlogged"});
        return;
    }
    if (javaName == "minecraft:sea_pickle") {
        be["cluster_count"] = std::to_string(std::clamp(asInt(js, "pickles", 1), 1, 4));
        eraseAll(be, {"pickles", "waterlogged"});
        return;
    }
    if (javaName == "minecraft:turtle_egg") {
        be["egg_count"] = std::to_string(std::clamp(asInt(js, "eggs", 1), 1, 4));
        eraseAll(be, {"eggs"});
        return;
    }
    if (javaName == "minecraft:sweet_berry_bush") {
        be["growth"] = std::to_string(std::min(3, asInt(js, "age", 0)));
        return;
    }

    static constexpr std::pair<std::string_view, std::string_view> kCrops[] = {
        {"minecraft:wheat", "growth"},
        {"minecraft:carrots", "growth"},
        {"minecraft:potatoes", "growth"},
        {"minecraft:beetroots", "growth"},
        {"minecraft:nether_wart", "growth"},
        {"minecraft:torchflower_crop", "growth"},
        {"minecraft:pitcher_crop", "growth"},
    };
    for (auto const& [crop, key] : kCrops) {
        if (javaName != crop) continue;
        if (get(js, "age")) {
            be[std::string(key)] = std::to_string(asInt(js, "age", 0));
            eraseAll(be, {"age"});
        }
        return;
    }

    if (javaName == "minecraft:bamboo") {
        if (get(js, "leaves")) be["bamboo_leaf_size"] = std::string(*get(js, "leaves"));
        eraseAll(be, {"leaves"});
        return;
    }
    if (javaName == "minecraft:big_dripleaf") {
        if (get(js, "tilt")) be["big_dripleaf_tilt"] = std::string(*get(js, "tilt"));
        eraseAll(be, {"tilt"});
        return;
    }
    if (javaName == "minecraft:pink_petals") {
        be["growth"] = std::to_string(std::clamp(asInt(js, "growth", 1), 1, 4));
        return;
    }
    if (javaName == "minecraft:redstone_wire") {
        be["redstone_signal"] = std::to_string(std::clamp(asInt(js, "power", 0), 0, 15));
        eraseAll(be, {"power"});
        return;
    }
    if (contains(javaName, "redstone_torch")) {
        eraseAll(be, {"lit"});
        return;
    }
    if (javaName == "minecraft:observer") {
        be["powered_bit"] = asBool(js, "powered") ? "1" : "0";
        eraseAll(be, {"powered"});
        return;
    }
    if (javaName == "minecraft:piston" || javaName == "minecraft:sticky_piston") {
        be["extended_bit"] = asBool(js, "extended") ? "1" : "0";
        eraseAll(be, {"extended"});
        return;
    }
    if (javaName == "minecraft:dispenser" || javaName == "minecraft:dropper") {
        be["triggered_bit"] = asBool(js, "triggered") ? "1" : "0";
        eraseAll(be, {"triggered"});
        return;
    }
    if (javaName == "minecraft:hopper") {
        be["toggle_bit"] = asBool(js, "enabled", true) ? "0" : "1";
        eraseAll(be, {"enabled"});
        return;
    }

    // 活塞/发射器/漏斗等由 _convertFacing 统一处理 facing；这里只兜底清理
}

void applyFacing(std::string_view javaName, Props const& js, Props& be) {
    auto const facing = get(js, "facing");
    if (!facing) return;

    // 这些方块已由 applySpecial 完整处理 facing，不能再落到通用 cardinal 分支
    // （否则会同时出现 direction 与 facing_direction，基岩版解析会失败）。
    if (endsWith(javaName, "_door") || endsWith(javaName, "_trapdoor") || contains(javaName, "_bed")
        || javaName == "minecraft:lever" || contains(javaName, "fence_gate")) {
        return;
    }

    if (contains(javaName, "pumpkin") && !contains(javaName, "_stem")) {
        int const dir = cardinalToBedrock(*facing);
        be["direction"] = std::to_string(dir >= 0 ? dir : 0);
        eraseAll(be, {"facing"});
        return;
    }

    if (contains(javaName, "piston") || javaName == "minecraft:dispenser" || javaName == "minecraft:dropper"
        || javaName == "minecraft:hopper" || javaName == "minecraft:observer" || javaName == "minecraft:crafter"
        || javaName == "minecraft:furnace" || javaName == "minecraft:smoker"
        || javaName == "minecraft:blast_furnace") {
        int const dir = facingToBedrock(*facing);
        be["facing_direction"] = std::to_string(dir >= 0 ? dir : 0);
        eraseAll(be, {"facing"});
        return;
    }

    if (endsWith(javaName, "_stairs")) {
        int const dir = stairToBedrock(*facing);
        be["weirdo_direction"] = std::to_string(dir >= 0 ? dir : 0);
        eraseAll(be, {"facing"});
        return;
    }

    if (contains(javaName, "_button")) {
        be["button_pressed_bit"] = asBool(js, "powered") || asBool(js, "pressed") ? "1" : "0";
        auto const face = get(js, "face").value_or("wall");
        int        dir  = -1;
        if (face == "ceiling") {
            dir = 0;
        } else if (face == "floor") {
            dir = 1;
        } else {
            dir = buttonToBedrock(*facing);
        }
        be["facing_direction"] = std::to_string(dir >= 0 ? dir : 2);
        eraseAll(be, {"facing", "face", "pressed"});
        return;
    }

    if (contains(javaName, "repeater") || contains(javaName, "comparator")) {
        int const dir = cardinalToBedrock(*facing);
        be["direction"] = std::to_string(dir >= 0 ? dir : 0);
        eraseAll(be, {"facing"});
        return;
    }

    if (contains(javaName, "anvil")) {
        int const dir = anvilToBedrock(*facing);
        be["direction"] = std::to_string(dir >= 0 ? dir : 0);
        eraseAll(be, {"facing"});
        return;
    }

    if (contains(javaName, "bell")) {
        int const dir = anvilToBedrock(*facing);
        be["direction"] = std::to_string(dir >= 0 ? dir : 0);
        eraseAll(be, {"facing"});
        return;
    }

    if (contains(javaName, "torch") && !contains(javaName, "redstone_torch")) {
        if (auto dir = torchToBedrock(*facing)) be["torch_facing_direction"] = *dir;
        eraseAll(be, {"facing"});
        return;
    }

    if (contains(javaName, "redstone_torch")) {
        if (auto dir = torchToBedrock(*facing)) be["torch_facing_direction"] = *dir;
        eraseAll(be, {"facing"});
        return;
    }

    if (contains(javaName, "end_rod")) {
        int const dir = facingToBedrock(*facing);
        be["facing_direction"] = std::to_string(dir >= 0 ? dir : 0);
        eraseAll(be, {"facing"});
        return;
    }

    if (*facing == "north" || *facing == "south" || *facing == "west" || *facing == "east") {
        int const dir = facingToBedrock(*facing);
        be["facing_direction"] = std::to_string(dir >= 0 ? dir : 3);
        eraseAll(be, {"facing"});
        return;
    }

    if (contains(javaName, "end_portal_frame")) {
        int const dir = cardinalToBedrock(*facing);
        be["direction"] = std::to_string(dir >= 0 ? dir : 0);
        eraseAll(be, {"facing"});
    }
}

detail::MapEntry const* findEntry(std::string_view javaName) {
    auto const* begin = std::begin(detail::kBlockMap);
    auto const* end   = std::end(detail::kBlockMap);
    auto const* it    = std::lower_bound(begin, end, javaName, [](detail::MapEntry const& entry, std::string_view key) {
        return std::string_view(entry.javaName) < key;
    });
    if (it != end && javaName == it->javaName) return it;
    return nullptr;
}

#include "ReverseMapping.inc"

} // namespace

struct BlockMapper::Impl {
    std::unordered_map<std::string_view, std::vector<detail::MapEntry const*>> byBedrock;

    Impl() {
        byBedrock.reserve(detail::kBlockMapCount);
        for (auto const& entry : detail::kBlockMap) {
            byBedrock[std::string_view(entry.bedrockName)].push_back(&entry);
        }
    }
};

BlockMapper::BlockMapper() : mImpl(std::make_unique<Impl>()) {}
BlockMapper::~BlockMapper() = default;

std::size_t BlockMapper::tableSize() const noexcept { return detail::kBlockMapCount; }

std::vector<std::string> BlockMapper::bedrockNames() const {
    std::vector<std::string> names;
    names.reserve(detail::kBlockMapCount);
    std::unordered_set<std::string> seen;
    seen.reserve(detail::kBlockMapCount);
    for (auto const& entry : detail::kBlockMap) {
        std::string name = std::string("minecraft:") + std::string(bedrockNameAlias(entry.javaName, entry.bedrockName));
        if (seen.insert(name).second) names.push_back(std::move(name));
    }
    return names;
}

std::optional<std::string> BlockMapper::bedrockNameOf(std::string_view javaName) const {
    auto const* entry = findEntry(javaName);
    if (entry == nullptr) return std::nullopt;
    return std::string("minecraft:") + std::string(bedrockNameAlias(entry->javaName, entry->bedrockName));
}

BlockState BlockMapper::javaToBedrock(std::string_view javaName, std::string_view javaProperties, bool* known) const {
    auto const* entry = findEntry(javaName);
    if (known != nullptr) *known = entry != nullptr;

    Props const js = parseProps(javaProperties);

    if (entry == nullptr) {
        return {withNamespace(javaName), ""};
    }

    Props be;
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
                if (auto const value = get(js, javaKey)) be[beKey] = std::string(*value);
            }
            if (comma == std::string_view::npos) break;
            start = comma + 1;
        }
    } else {
        be = js;
    }

    applySpecial(javaName, js, be);
    if ((entry->flags & 1) != 0) applyFacing(javaName, js, be);

    // 这些 Java 键在基岩版没有对应语义，且保留会导致 create_block_data 解析失败。
    eraseAll(be, {"waterlogged", "persistent", "distance", "snowy"});

    return {std::string("minecraft:") + std::string(bedrockNameAlias(javaName, entry->bedrockName)), canonical(be)};
}

BlockState BlockMapper::bedrockToJava(BlockState const& bedrock) const {
    auto const shortName = std::string(stripMc(bedrock.name));
    auto const found     = mImpl->byBedrock.find(shortName);
    if (found == mImpl->byBedrock.end() || found->second.empty()) {
        return bedrock;
    }

    Props be;
    for (auto const& [key, value] : parseProps(bedrock.properties)) {
        be[cleanStateKey(key)] = value;
    }
    return reverseMapBedrock(found->second, shortName, be);
}

} // namespace litematicabds

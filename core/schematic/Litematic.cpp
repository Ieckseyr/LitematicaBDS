#include "litematicabds/Schematic.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#include "litematicabds/Nbt.h"
#include "litematicabds/Version.h"
#include "../util/Gzip.h"

namespace litematicabds {
namespace {

using nbt::Compound;
using nbt::Tag;
using nbt::Type;

bool endsWith(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

std::string tagToString(Tag const& tag) {
    switch (tag.type) {
    case Type::String:
        return tag.asString();
    case Type::Byte:
    case Type::Short:
    case Type::Int:
    case Type::Long:
        return std::to_string(tag.asInt64());
    case Type::Float:
        return std::to_string(static_cast<double>(std::get<float>(tag.value)));
    case Type::Double:
        return std::to_string(std::get<double>(tag.value));
    default:
        return {};
    }
}

std::string propertiesToText(Tag const& properties) {
    if (!properties.isCompound()) return {};
    std::string out;
    auto const& compound = properties.asCompound();
    for (std::size_t i = 0; i < compound.keys.size(); ++i) {
        auto const value = tagToString(compound.values[i]);
        if (value.empty()) continue;
        if (!out.empty()) out.push_back(',');
        out += compound.keys[i];
        out.push_back('=');
        out += value;
    }
    return canonicalProperties(out);
}

std::optional<Vec3i> readVec3(Tag const& tag) {
    if (tag.isCompound()) {
        auto const& c = tag.asCompound();
        auto const* x = c.find("x");
        auto const* y = c.find("y");
        auto const* z = c.find("z");
        if (x == nullptr || y == nullptr || z == nullptr) return std::nullopt;
        return Vec3i{
            static_cast<int32_t>(x->asInt64()),
            static_cast<int32_t>(y->asInt64()),
            static_cast<int32_t>(z->asInt64()),
        };
    }
    if (tag.type == Type::IntArray) {
        auto const& values = std::get<Tag::IntArray>(tag.value);
        if (values.size() < 3) return std::nullopt;
        return Vec3i{values[0], values[1], values[2]};
    }
    return std::nullopt;
}

struct RawRegion {
    std::string  name;
    Vec3i        position;
    Vec3i        size;
    Tag const*   palette{nullptr};
    Tag const*   blockStates{nullptr};
};

struct PaletteKey {
    std::string javaName;
    std::string javaProperties;

    bool operator==(PaletteKey const& other) const noexcept {
        return javaName == other.javaName && javaProperties == other.javaProperties;
    }
};

struct PaletteKeyHash {
    std::size_t operator()(PaletteKey const& key) const noexcept {
        return std::hash<std::string>{}(key.javaName) * 1315423911u ^ std::hash<std::string>{}(key.javaProperties);
    }
};

} // namespace

std::uint64_t Schematic::chunkKey(int cx, int cy, int cz) noexcept {
    auto const pack = [](int v) -> std::uint64_t {
        return static_cast<std::uint64_t>(static_cast<std::uint32_t>(v) & 0x1FFFFFu);
    };
    return (pack(cx) << 42) | (pack(cy) << 21) | pack(cz);
}

std::uint64_t Schematic::chunkKeyFor(Vec3i local) noexcept {
    auto const floorDiv = [](int value, int divisor) {
        int q = value / divisor;
        int r = value % divisor;
        if (r < 0) --q;
        return q;
    };
    return chunkKey(floorDiv(local.x, kChunkSize), floorDiv(local.y, kChunkSize), floorDiv(local.z, kChunkSize));
}

void Schematic::buildIndices() {
    chunks.clear();
    chunks.reserve(blocks.size() / 8 + 16);
    for (std::uint32_t i = 0; i < blocks.size(); ++i) {
        chunks[chunkKeyFor(blocks[i].pos)].push_back(i);
    }
}

std::uint64_t Schematic::visibleCount() const noexcept {
    std::uint64_t count = 0;
    for (auto visible : surface) count += visible != 0 ? 1 : 0;
    return count;
}

std::vector<std::pair<std::string, std::uint64_t>> Schematic::materialCounts() const {
    std::unordered_map<std::string, std::uint64_t> counts;
    counts.reserve(palette.size());
    for (auto const& block : blocks) {
        if (block.palette >= palette.size()) continue;
        counts[palette[block.palette].bedrockState.substr(0, palette[block.palette].bedrockState.find('['))] += 1;
    }
    std::vector<std::pair<std::string, std::uint64_t>> out(counts.begin(), counts.end());
    std::sort(out.begin(), out.end(), [](auto const& a, auto const& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });
    return out;
}

Result<LoadResult> parseLitematic(
    std::span<std::uint8_t const> nbtBytes,
    std::string                   sourceName,
    BlockMapper const&            mapper,
    LoadOptions const&            options
) {
    auto parsed = nbt::parse(nbtBytes);
    if (!parsed.ok()) return parsed.error();
    if (!parsed.value().isCompound()) return Error("litematic 根标签不是 Compound");

    auto const& root = parsed.value().asCompound();

    auto result   = std::make_shared<Schematic>();
    LoadResult load;
    load.schematic = result;

    // ── Metadata ──────────────────────────────────────────────────────────
    auto const* metadata = root.find("Metadata");
    if (metadata != nullptr && metadata->isCompound()) {
        auto const& meta = metadata->asCompound();
        if (auto const* name = meta.find("Name")) {
            if (name->isString()) result->meta.name = name->asString();
        }
        if (auto const* author = meta.find("Author")) {
            if (author->isString()) result->meta.author = author->asString();
        }
        if (auto const* description = meta.find("Description")) {
            if (description->isString()) result->meta.description = description->asString();
        }
        if (auto const* created = meta.find("TimeCreated")) result->meta.timeCreated = created->asInt64();
        if (auto const* enclosing = meta.find("EnclosingSize")) {
            if (auto size = readVec3(*enclosing)) result->meta.size = *size;
        }
    }
    if (result->meta.name.empty() || result->meta.name == "Unnamed") {
        auto const slash = sourceName.find_last_of("/\\");
        auto const base  = slash == std::string::npos ? sourceName : sourceName.substr(slash + 1);
        auto const dot   = base.find_last_of('.');
        result->meta.name = dot == std::string::npos ? base : base.substr(0, dot);
        if (result->meta.name.empty()) result->meta.name = "Unnamed";
    }
    result->meta.sourcePath = sourceName;

    auto const* regions = root.find("Regions");
    if (regions == nullptr || !regions->isCompound()) return Error("litematic 缺少 Regions");

    struct RegionPlan {
        RawRegion raw;
        std::uint32_t paletteOffset{0};
        std::uint32_t paletteCount{0};
        std::uint64_t volume{0};
    };

    std::vector<RegionPlan>                       plans;
    std::unordered_map<PaletteKey, std::uint32_t, PaletteKeyHash> paletteIndex;

    auto const& regionCompound = regions->asCompound();
    std::uint64_t totalVolume  = 0;
    for (std::size_t r = 0; r < regionCompound.keys.size(); ++r) {
        auto const& regionTag = regionCompound.values[r];
        if (!regionTag.isCompound()) {
            load.warnings.push_back("忽略非 Compound 区域: " + regionCompound.keys[r]);
            continue;
        }
        auto const& region = regionTag.asCompound();
        RawRegion   raw;
        raw.name = regionCompound.keys[r];

        if (auto const* position = region.find("Position")) {
            if (auto pos = readVec3(*position)) raw.position = *pos;
        }
        if (auto const* size = region.find("Size")) {
            if (auto parsedSize = readVec3(*size)) raw.size = *parsedSize;
        } else if (auto const* sizeX = region.find("SizeX")) {
            auto const* sizeY = region.find("SizeY");
            auto const* sizeZ = region.find("SizeZ");
            raw.size = Vec3i{
                static_cast<int32_t>(sizeX->asInt64()),
                static_cast<int32_t>(sizeY != nullptr ? sizeY->asInt64() : 0),
                static_cast<int32_t>(sizeZ != nullptr ? sizeZ->asInt64() : 0),
            };
        }
        raw.size = Vec3i{std::abs(raw.size.x), std::abs(raw.size.y), std::abs(raw.size.z)};
        if (raw.size.x <= 0 || raw.size.y <= 0 || raw.size.z <= 0) {
            load.warnings.push_back("跳过空区域: " + raw.name);
            continue;
        }

        raw.palette     = region.find("BlockStatePalette");
        raw.blockStates = region.find("BlockStates");
        if (raw.palette == nullptr || !raw.palette->isList()) {
            load.warnings.push_back("区域缺少 BlockStatePalette: " + raw.name);
            continue;
        }
        if (raw.blockStates != nullptr && raw.blockStates->type != Type::LongArray) {
            load.warnings.push_back("区域 BlockStates 不是 LongArray: " + raw.name);
            raw.blockStates = nullptr;
        }

        auto const volume = static_cast<std::uint64_t>(raw.size.x) * static_cast<std::uint64_t>(raw.size.y)
                          * static_cast<std::uint64_t>(raw.size.z);
        if (volume == 0 || volume > options.maxVolume) {
            return Error("区域 " + raw.name + " 体积非法或超过上限");
        }
        totalVolume += volume;
        if (totalVolume > options.maxVolume) return Error("原理图总体积超过上限");
        plans.push_back({raw, 0, 0, volume});
    }

    if (plans.empty()) return Error("litematic 没有可用区域");

    // 先确定全局包围盒（区域 Position 可能为负）。
    Vec3i minPos{std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::max()};
    Vec3i maxPos{std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::min()};
    for (auto const& plan : plans) {
        minPos.x = std::min(minPos.x, plan.raw.position.x);
        minPos.y = std::min(minPos.y, plan.raw.position.y);
        minPos.z = std::min(minPos.z, plan.raw.position.z);
        maxPos.x = std::max(maxPos.x, plan.raw.position.x + plan.raw.size.x);
        maxPos.y = std::max(maxPos.y, plan.raw.position.y + plan.raw.size.y);
        maxPos.z = std::max(maxPos.z, plan.raw.position.z + plan.raw.size.z);
    }
    Vec3i const size{maxPos.x - minPos.x, maxPos.y - minPos.y, maxPos.z - minPos.z};
    if (size.x <= 0 || size.y <= 0 || size.z <= 0) return Error("litematic 包围盒非法");

    auto const boundingVolume = static_cast<std::uint64_t>(size.x) * static_cast<std::uint64_t>(size.y)
                              * static_cast<std::uint64_t>(size.z);
    if (boundingVolume > options.maxVolume) return Error("原理图包围盒超过上限");

    result->meta.size    = size;
    result->meta.volume  = boundingVolume;
    result->meta.regionCount = static_cast<std::int32_t>(plans.size());

    // 计算每个区域在全局 palette 中的起点。
    std::uint32_t paletteCursor = 0;
    for (auto& plan : plans) {
        auto const& list = plan.raw.palette->asList();
        plan.paletteOffset = paletteCursor;
        plan.paletteCount  = static_cast<std::uint32_t>(list.items.size());
        paletteCursor += plan.paletteCount;
    }

    // 全局 palette（按 Java 名 + 属性去重）。
    result->palette.reserve(std::min<std::size_t>(paletteCursor, 4096));
    for (auto const& plan : plans) {
        auto const& list = plan.raw.palette->asList();
        for (auto const& item : list.items) {
            if (!item.isCompound()) return Error("palette 项不是 Compound");
            auto const&  entryCompound = item.asCompound();
            auto const*  nameTag       = entryCompound.find("Name");
            if (nameTag == nullptr || !nameTag->isString()) return Error("palette 项缺少 Name");
            std::string  javaName = nameTag->asString();
            std::string  javaProps;
            if (auto const* properties = entryCompound.find("Properties")) {
                javaProps = propertiesToText(*properties);
            }

            PaletteKey const key{javaName, javaProps};
            auto const       found = paletteIndex.find(key);
            if (found != paletteIndex.end()) continue;

            PaletteEntry entry;
            entry.javaName       = javaName;
            entry.javaProperties = javaProps;
            entry.air            = javaName == "minecraft:air" || javaName == "minecraft:cave_air"
                        || javaName == "minecraft:void_air" || endsWith(javaName, "_air");

            bool known = false;
            auto bedrock = mapper.javaToBedrock(javaName, javaProps, &known);
            if (!known && load.warnings.size() < 50) {
                load.warnings.push_back("未收录的 Java 方块: " + javaName);
            }
            entry.bedrockState = bedrock.toString();
            paletteIndex.emplace(key, static_cast<std::uint32_t>(result->palette.size()));
            result->palette.push_back(std::move(entry));
        }
    }

    // 解码所有区域。
    result->blocks.reserve(std::min<std::uint64_t>(totalVolume, options.maxBlocks));
    for (auto const& plan : plans) {
        auto const& raw     = plan.raw;
        auto const& list    = raw.palette->asList();
        auto const  paletteCount = list.items.size();
        if (paletteCount == 0) continue;

        std::uint32_t bits = 2;
        while ((1ull << bits) < paletteCount) ++bits;
        bits = std::max<std::uint32_t>(2, bits);
        std::uint64_t const mask = (1ull << bits) - 1ull;

        std::vector<std::uint64_t> empty;
        Tag::LongArray const*     longs = nullptr;
        if (raw.blockStates != nullptr) {
            longs = &raw.blockStates->asLongArray();
        }
        std::uint64_t const totalCells = plan.volume;
        if (longs != nullptr) {
            auto const neededWords = (totalCells * bits + 63) / 64;
            if (longs->size() < neededWords) {
                load.warnings.push_back("区域 BlockStates 长度不足，尾部按空气处理: " + raw.name);
            }
        }

        std::uint32_t ci = 0;
        for (int32_t y = 0; y < raw.size.y; ++y) {
            for (int32_t z = 0; z < raw.size.z; ++z) {
                for (int32_t x = 0; x < raw.size.x; ++x, ++ci) {
                    std::uint32_t regionPalette = 0;
                    if (longs != nullptr && !longs->empty()) {
                        std::uint64_t const bitIndex = static_cast<std::uint64_t>(ci) * bits;
                        std::uint64_t const word     = bitIndex >> 6;
                        std::uint32_t const offset   = static_cast<std::uint32_t>(bitIndex & 63);
                        std::uint64_t       value    = 0;
                        if (word < longs->size()) value = (*longs)[word] >> offset;
                        if (offset + bits > 64 && word + 1 < longs->size()) {
                            value |= (*longs)[word + 1] << (64 - offset);
                        }
                        regionPalette = static_cast<std::uint32_t>(value & mask);
                    }
                    if (regionPalette >= paletteCount) regionPalette = 0;

                    auto const& paletteItem = list.items[regionPalette];
                    if (!paletteItem.isCompound()) continue;
                    auto const& entryCompound = paletteItem.asCompound();
                    auto const* nameTag       = entryCompound.find("Name");
                    if (nameTag == nullptr || !nameTag->isString()) continue;
                    std::string javaName = nameTag->asString();
                    if (javaName == "minecraft:air" || javaName == "minecraft:cave_air"
                        || javaName == "minecraft:void_air" || endsWith(javaName, "_air")) {
                        continue;
                    }
                    std::string javaProps;
                    if (auto const* properties = entryCompound.find("Properties")) {
                        javaProps = propertiesToText(*properties);
                    }
                    auto const found = paletteIndex.find(PaletteKey{javaName, javaProps});
                    if (found == paletteIndex.end()) continue;

                    result->blocks.push_back(SchematicBlock{
                        Vec3i{raw.position.x - minPos.x + x, raw.position.y - minPos.y + y, raw.position.z - minPos.z + z},
                        found->second,
                    });
                }
            }
        }
        result->regions.push_back(RegionInfo{raw.name, raw.position - minPos, raw.size});
    }

    if (result->blocks.size() > options.maxBlocks) return Error("非空气方块数超过上限");

    result->meta.totalBlocks = result->blocks.size();

    // 面剔除：占用位图 -> 每个方块是否至少一面暴露。
    result->surface.assign(result->blocks.size(), 1);
    if (options.computeSurface) {
        std::vector<std::uint64_t> occupancy((boundingVolume + 63) / 64, 0);
        auto const indexOf = [&](Vec3i p) -> std::uint64_t {
            return static_cast<std::uint64_t>(p.x)
                 + static_cast<std::uint64_t>(size.x)
                       * (static_cast<std::uint64_t>(p.y) + static_cast<std::uint64_t>(size.y) * p.z);
        };
        for (auto const& block : result->blocks) {
            auto const idx = indexOf(block.pos);
            occupancy[idx >> 6] |= 1ull << (idx & 63);
        }
        auto occupied = [&](int x, int y, int z) {
            if (x < 0 || y < 0 || z < 0 || x >= size.x || y >= size.y || z >= size.z) return false;
            auto const idx = indexOf({x, y, z});
            return (occupancy[idx >> 6] & (1ull << (idx & 63))) != 0;
        };
        for (std::size_t i = 0; i < result->blocks.size(); ++i) {
            auto const p = result->blocks[i].pos;
            bool const exposed = !occupied(p.x + 1, p.y, p.z) || !occupied(p.x - 1, p.y, p.z)
                              || !occupied(p.x, p.y + 1, p.z) || !occupied(p.x, p.y - 1, p.z)
                              || !occupied(p.x, p.y, p.z + 1) || !occupied(p.x, p.y, p.z - 1);
            result->surface[i] = exposed ? 1 : 0;
        }
    }

    result->buildIndices();
    return load;
}

Result<LoadResult> loadLitematicFile(
    std::filesystem::path const& path,
    BlockMapper const&           mapper,
    LoadOptions const&           options
) {
    std::error_code ec;
    auto const      fileSize = std::filesystem::file_size(path, ec);
    if (ec) return Error("无法读取原理图: " + path.string() + " (" + ec.message() + ")");
    if (fileSize == 0) return Error("原理图为空: " + path.string());
    if (fileSize > 512ull * 1024ull * 1024ull) return Error("原理图文件过大（>512MB）");

    std::ifstream in(path, std::ios::binary);
    if (!in) return Error("无法打开原理图: " + path.string());
    std::vector<std::uint8_t> raw(static_cast<std::size_t>(fileSize));
    in.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
    if (!in) return Error("读取原理图失败: " + path.string());

    std::span<std::uint8_t const> nbtBytes(raw);
    std::vector<std::uint8_t>     decompressed;
    if (util::looksGzip(raw) || util::looksZlib(raw)) {
        auto result = util::decompress(raw);
        if (!result.ok()) return result.error();
        decompressed = std::move(result).value();
        nbtBytes     = decompressed;
    }

    return parseLitematic(nbtBytes, path.string(), mapper, options);
}

Result<void> writeLitematicFile(
    std::filesystem::path const& path,
    SaveRequest const&           request,
    BlockMapper const&           mapper,
    std::vector<std::string>*    warnings
) {
    if (request.size.x <= 0 || request.size.y <= 0 || request.size.z <= 0) return Error("保存尺寸非法");
    auto const volume = static_cast<std::uint64_t>(request.size.x) * static_cast<std::uint64_t>(request.size.y)
                      * static_cast<std::uint64_t>(request.size.z);
    if (volume > 64'000'000ull) return Error("保存体积过大（>64M）");

    struct JavaEntry {
        std::string name;
        std::string properties;
        bool        operator==(JavaEntry const& other) const noexcept {
            return name == other.name && properties == other.properties;
        }
    };
    struct JavaEntryHash {
        std::size_t operator()(JavaEntry const& entry) const noexcept {
            return std::hash<std::string>{}(entry.name) * 1315423911u ^ std::hash<std::string>{}(entry.properties);
        }
    };

    std::vector<JavaEntry>                          palette;
    std::unordered_map<JavaEntry, std::uint32_t, JavaEntryHash> index;
    palette.push_back({"minecraft:air", ""});
    index.emplace(JavaEntry{"minecraft:air", ""}, 0);

    std::vector<std::uint32_t> cells(static_cast<std::size_t>(volume), 0);
    std::uint64_t              nonAir = 0;
    std::size_t                skipped = 0;

    for (auto const& block : request.blocks) {
        if (block.pos.x < 0 || block.pos.y < 0 || block.pos.z < 0 || block.pos.x >= request.size.x
            || block.pos.y >= request.size.y || block.pos.z >= request.size.z) {
            ++skipped;
            continue;
        }
        if (block.state.isAir() || block.state.empty()) continue;

        auto java = mapper.bedrockToJava(block.state);
        if (java.name.empty()) {
            java.name = "minecraft:stone";
            if (warnings != nullptr && warnings->size() < 50) {
                warnings->push_back("无法反向映射，降级为石头: " + block.state.toString());
            }
        }
        java.properties = canonicalProperties(java.properties);
        JavaEntry const key{java.name, java.properties};
        auto const      found = index.find(key);
        std::uint32_t   paletteId = 0;
        if (found != index.end()) {
            paletteId = found->second;
        } else {
            paletteId = static_cast<std::uint32_t>(palette.size());
            palette.push_back(key);
            index.emplace(key, paletteId);
        }

        // litematic 的索引顺序是 y 外层、z 中层、x 内层：
        // index = x + sizeX * (z + sizeZ * y)
        auto const ordered = static_cast<std::size_t>(block.pos.x)
                           + static_cast<std::size_t>(request.size.x)
                                 * (static_cast<std::size_t>(block.pos.z)
                                    + static_cast<std::size_t>(request.size.z) * block.pos.y);
        cells[ordered] = paletteId;
        ++nonAir;
    }

    if (skipped > 0 && warnings != nullptr && warnings->size() < 50) {
        warnings->push_back("有 " + std::to_string(skipped) + " 个方块超出保存范围，已忽略");
    }

    std::uint32_t bits = 2;
    while ((1ull << bits) < palette.size()) ++bits;
    bits = std::max<std::uint32_t>(2, bits);
    std::uint64_t const mask  = (1ull << bits) - 1ull;
    std::size_t const   words = static_cast<std::size_t>((volume * bits + 63) / 64);
    std::vector<std::uint64_t> longs(words, 0);

    for (std::size_t i = 0; i < cells.size(); ++i) {
        std::uint64_t const bitIndex = static_cast<std::uint64_t>(i) * bits;
        std::size_t const   word     = static_cast<std::size_t>(bitIndex >> 6);
        std::uint32_t const offset   = static_cast<std::uint32_t>(bitIndex & 63);
        std::uint64_t const value    = cells[i] & mask;
        longs[word] |= value << offset;
        if (offset + bits > 64 && word + 1 < longs.size()) {
            longs[word + 1] |= value >> (64 - offset);
        }
    }

    Compound metadata;
    metadata.set("Name", request.name.empty() ? std::string("Unnamed") : request.name);
    metadata.set("Author", request.author);
    metadata.set("Description", request.description);
    metadata.set("RegionCount", static_cast<std::int32_t>(1));
    metadata.set("TotalBlocks", static_cast<std::int64_t>(nonAir));
    metadata.set("TotalVolume", static_cast<std::int64_t>(volume));
    Compound enclosing;
    enclosing.set("x", request.size.x);
    enclosing.set("y", request.size.y);
    enclosing.set("z", request.size.z);
    metadata.set("EnclosingSize", Tag::makeCompound(enclosing));
    auto const now = static_cast<std::int64_t>(std::time(nullptr));
    metadata.set("TimeCreated", now);
    metadata.set("TimeModified", now);

    Compound region;
    Compound position;
    position.set("x", 0);
    position.set("y", 0);
    position.set("z", 0);
    region.set("Position", Tag::makeCompound(position));
    Compound regionSize;
    regionSize.set("x", request.size.x);
    regionSize.set("y", request.size.y);
    regionSize.set("z", request.size.z);
    region.set("Size", Tag::makeCompound(regionSize));

    std::vector<Tag> paletteTags;
    paletteTags.reserve(palette.size());
    for (auto const& entry : palette) {
        Compound item;
        item.set("Name", entry.name);
        if (!entry.properties.empty()) {
            Compound properties;
            for (auto const& [key, value] : splitProperties(entry.properties)) properties.set(key, value);
            item.set("Properties", Tag::makeCompound(properties));
        }
        paletteTags.push_back(Tag::makeCompound(item));
    }
    region.set("BlockStatePalette", Tag::makeList(Type::Compound, std::move(paletteTags)));
    region.set("BlockStates", Tag::makeLongArray(std::move(longs)));
    region.set("TileEntities", Tag::makeList(Type::End));
    region.set("Entities", Tag::makeList(Type::End));

    Compound regions;
    regions.set("main", Tag::makeCompound(region));

    Compound root;
    root.set("Version", 6);
    root.set("MinecraftDataVersion", request.dataVersion != 0 ? request.dataVersion : 3955);
    root.set("Metadata", Tag::makeCompound(metadata));
    root.set("Regions", Tag::makeCompound(regions));

    auto nbtBytes = nbt::serialize("", Tag::makeCompound(root));
    auto compressed = util::gzipCompress(nbtBytes, 6);
    if (!compressed.ok()) return compressed.error();

    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return Error("无法写入文件: " + path.string());
    auto const& bytes = compressed.value();
    out.write(reinterpret_cast<char const*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) return Error("写入文件失败: " + path.string());
    return {};
}

} // namespace litematicabds

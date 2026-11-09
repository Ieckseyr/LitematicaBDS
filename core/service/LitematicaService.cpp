#include "litematicabds/Service.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "litematicabds/Schematic.h"
#include "litematicabds/Version.h"

namespace litematicabds {
namespace {


constexpr std::size_t kNoIndex = std::numeric_limits<std::size_t>::max();

enum class LayerMode {
    All,
    Fixed,
    Auto,
    Slice, // 只显示玩家脚部所在的单层
};

struct Candidate {
    Vec3i         pos;
    double        distance2{0.0};
    std::uint32_t palette{0};
};

struct Session {
    std::string player;
    Vec3d       position;
    int         dimension{0};
    bool        hasViewer{false};

    std::shared_ptr<Schematic> schematic;
    std::string                schematicName;
    Vec3i                      origin{};
    int                        projectionDimension{0};
    Rotation                   rotation{Rotation::None};
    Mirror                     mirror{Mirror::None};
    LayerMode                  layerMode{LayerMode::All};
    int                        fixedLayer{0};
    int                        viewDistanceOverride{0};

    std::optional<Vec3i> pos1;
    std::optional<Vec3i> pos2;

    bool active{false};
    bool needsPlan{false};
    bool draining{false};

    Vec3d lastPlanPosition{};
    int   lastPlanDimension{std::numeric_limits<int>::min()};
    int   lastPlanLayer{std::numeric_limits<int>::min()};

    std::vector<BlockChange> pending;
    std::size_t              pendingCursor{0};

    std::vector<Vec3i> pendingClears;
    std::size_t        pendingClearCursor{0};

    std::vector<Vec3i> forcedClears;
    std::size_t        forcedClearCursor{0};

    std::unordered_set<Vec3i, Vec3iHash> applied;
    std::unordered_set<Vec3i, Vec3iHash> desired;
    std::unordered_set<Vec3i, Vec3iHash> placed;

    std::uint64_t setsSent{0};
    std::uint64_t setFailed{0};
    std::uint64_t clearsSent{0};
};

struct StoreEntry {
    std::filesystem::path      path;
    std::shared_ptr<Schematic> schematic;
};

std::string lowerCopy(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

std::string formatVec(Vec3i v) {
    return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
}

std::vector<std::string> splitArgs(std::string_view line) {
    std::vector<std::string> args;
    std::string              current;
    char                     quote = '\0';
    for (char c : line) {
        if (quote != '\0') {
            if (c == quote) {
                quote = '\0';
            } else {
                current.push_back(c);
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!current.empty()) {
                args.push_back(std::move(current));
                current.clear();
            }
            continue;
        }
        current.push_back(c);
    }
    if (!current.empty()) args.push_back(std::move(current));
    return args;
}

bool parseInt(std::string_view text, int& out) {
    if (text.empty()) return false;
    int value = 0;
    auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return false;
    out = value;
    return true;
}

std::string sanitizeFileName(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.') {
            out.push_back(c);
        } else {
            out.push_back('_');
        }
    }
    return out.empty() ? "schematic" : out;
}

double distanceSquared(Vec3d a, Vec3d b) {
    double const dx = a.x - b.x;
    double const dy = a.y - b.y;
    double const dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

} // namespace

struct LitematicaService::Impl {
    Options    options;
    Config     config;
    BlockMapper mapper;
    LogFn      logger;
    StateValidator validator;
    BlockSampler   sampler;

    std::filesystem::path                     schematicsDir;
    std::filesystem::path                     exportsDir;
    std::filesystem::path                     configPath;
    std::unordered_map<std::string, StoreEntry> store;
    std::unordered_map<std::string, Session>   sessions;

    void log(LogLevel level, std::string const& message) const {
        if (logger) logger(level, message);
    }

    void rebuildPaths() {
        if (!options.schematicsDir.empty()) {
            schematicsDir = options.schematicsDir;
        } else if (config.storage.schematicsDir.empty()) {
            schematicsDir = options.dataDir / "schematics";
        } else {
            schematicsDir = std::filesystem::path(config.storage.schematicsDir);
        }
        if (schematicsDir.is_relative()) schematicsDir = options.dataDir / schematicsDir;

        if (!options.exportsDir.empty()) {
            exportsDir = options.exportsDir;
        } else if (config.storage.exportsDir.empty()) {
            exportsDir = options.dataDir / "exports";
        } else {
            exportsDir = std::filesystem::path(config.storage.exportsDir);
        }
        if (exportsDir.is_relative()) exportsDir = options.dataDir / exportsDir;

        configPath = options.configPath.empty() ? options.dataDir / "config.json" : options.configPath;
    }

    void scanStore() {
        store.clear();
        std::error_code ec;
        std::filesystem::create_directories(schematicsDir, ec);
        if (!std::filesystem::exists(schematicsDir, ec)) {
            log(LogLevel::Warn, "原理图目录不存在: " + schematicsDir.string());
            return;
        }
        for (auto it = std::filesystem::recursive_directory_iterator(
                 schematicsDir,
                 std::filesystem::directory_options::skip_permission_denied,
                 ec
             );
             it != std::filesystem::recursive_directory_iterator();
             it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            auto const ext = lowerCopy(it->path().extension().string());
            if (ext != ".litematic") continue;
            std::string key = it->path().stem().string();
            if (key.empty()) continue;
            if (store.contains(key)) {
                auto parent = it->path().parent_path().filename().string();
                key += " (" + parent + ")";
            }
            store.emplace(std::move(key), StoreEntry{it->path(), nullptr});
        }
        log(LogLevel::Info, "扫描到 " + std::to_string(store.size()) + " 个原理图");
    }

    StoreEntry* findEntry(std::string_view name) {
        auto it = store.find(std::string(name));
        if (it != store.end()) return &it->second;

        std::string const needle = lowerCopy(name);
        StoreEntry*       prefix  = nullptr;
        std::size_t       matches = 0;
        for (auto& [key, entry] : store) {
            auto const lowerKey = lowerCopy(key);
            if (lowerKey == needle) return &entry;
            if (lowerKey.starts_with(needle)) {
                prefix = &entry;
                ++matches;
            }
        }
        return matches == 1 ? prefix : nullptr;
    }

    void validatePalette(Schematic& schematic) {
        if (!validator) return;
        std::size_t failed = 0;
        for (auto& entry : schematic.palette) {
            if (entry.air) continue;
            if (auto normalized = validator(entry.bedrockState)) {
                entry.bedrockState = *normalized;
                entry.renderable   = true;
                continue;
            }
            auto const bracket = entry.bedrockState.find('[');
            auto const base    = bracket == std::string::npos ? entry.bedrockState : entry.bedrockState.substr(0, bracket);
            if (auto normalized = validator(base)) {
                entry.bedrockState = *normalized;
                entry.renderable   = true;
            } else {
                entry.renderable = false;
                ++failed;
            }
        }
        if (failed > 0) {
            log(LogLevel::Warn, "有 " + std::to_string(failed) + " 种方块状态无法被 BDS 解析，渲染时跳过");
        }
    }

    std::shared_ptr<Schematic> loadNamed(std::string_view name, std::string& resolvedName, std::string& error) {
        auto* entry = findEntry(name);
        if (entry == nullptr) {
            error = "找不到原理图: " + std::string(name);
            return nullptr;
        }
        if (entry->schematic) {
            resolvedName = entry->path.stem().string();
            return entry->schematic;
        }

        LoadOptions loadOptions;
        loadOptions.maxBlocks = 4'000'000;
        auto result = loadLitematicFile(entry->path, mapper, loadOptions);
        if (!result.ok()) {
            error = result.error().message;
            return nullptr;
        }
        if (!result.value().warnings.empty()) {
            for (auto const& warning : result.value().warnings) log(LogLevel::Warn, warning);
        }
        validatePalette(*result.value().schematic);
        entry->schematic = result.value().schematic;
        resolvedName     = entry->path.stem().string();
        return entry->schematic;
    }

    Session& sessionFor(std::string const& player) {
        auto [it, inserted] = sessions.try_emplace(player);
        if (inserted) {
            it->second.player    = player;
            it->second.position  = {};
            it->second.dimension = 0;
        }
        return it->second;
    }

    void resetProjection(Session& session) {
        for (auto const& pos : session.applied) session.forcedClears.push_back(pos);
        session.applied.clear();
        session.pending.clear();
        session.pendingCursor = 0;
        session.pendingClears.clear();
        session.pendingClearCursor = 0;
        session.desired.clear();
        session.placed.clear();
        session.needsPlan = false;
    }

    void plan(Session& session) {
        session.needsPlan = false;
        session.desired.clear();
        session.pending.clear();
        session.pendingCursor = 0;
        session.pendingClears.clear();
        session.pendingClearCursor = 0;

        if (!session.active || !session.schematic || !session.hasViewer
            || session.dimension != session.projectionDimension) {
            for (auto const& pos : session.applied) session.pendingClears.push_back(pos);
            session.lastPlanPosition  = session.position;
            session.lastPlanDimension = session.dimension;
            return;
        }

        auto const& schematic = *session.schematic;
        Vec3i const size      = schematic.meta.size;
        Vec3i const rotated   = rotatedSize(size, session.rotation);
        (void)rotated;

        double const view  = session.viewDistanceOverride > 0 ? session.viewDistanceOverride : config.render.viewDistance;
        double const view2 = view * view;
        Vec3d const  eye{session.position.x, session.position.y, session.position.z};
        int const    playerLocalY = static_cast<int>(std::floor(session.position.y)) - session.origin.y;

        std::vector<Candidate> candidates;
        candidates.reserve(1024);

        for (auto const& [key, indices] : schematic.chunks) {
            int const cx = static_cast<int>((key >> 42) & 0x1FFFFFu);
            int const cy = static_cast<int>((key >> 21) & 0x1FFFFFu);
            int const cz = static_cast<int>(key & 0x1FFFFFu);

            Vec3i const chunkMin{cx * Schematic::kChunkSize, cy * Schematic::kChunkSize, cz * Schematic::kChunkSize};
            Vec3i const chunkMax{
                std::min(chunkMin.x + Schematic::kChunkSize - 1, size.x - 1),
                std::min(chunkMin.y + Schematic::kChunkSize - 1, size.y - 1),
                std::min(chunkMin.z + Schematic::kChunkSize - 1, size.z - 1),
            };
            if (chunkMin.x >= size.x || chunkMin.y >= size.y || chunkMin.z >= size.z) continue;

            Vec3i worldMin{std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), std::numeric_limits<int>::max()};
            Vec3i worldMax{std::numeric_limits<int>::min(), std::numeric_limits<int>::min(), std::numeric_limits<int>::min()};
            for (int i = 0; i < 8; ++i) {
                Vec3i const corner{
                    (i & 1) != 0 ? chunkMax.x : chunkMin.x,
                    (i & 2) != 0 ? chunkMax.y : chunkMin.y,
                    (i & 4) != 0 ? chunkMax.z : chunkMin.z,
                };
                auto const world = session.origin + transformLocal(corner, size, session.rotation, session.mirror);
                worldMin.x = std::min(worldMin.x, world.x);
                worldMin.y = std::min(worldMin.y, world.y);
                worldMin.z = std::min(worldMin.z, world.z);
                worldMax.x = std::max(worldMax.x, world.x);
                worldMax.y = std::max(worldMax.y, world.y);
                worldMax.z = std::max(worldMax.z, world.z);
            }

            double const dx = eye.x < worldMin.x ? worldMin.x - eye.x : (eye.x > worldMax.x ? eye.x - worldMax.x : 0.0);
            double const dy = eye.y < worldMin.y ? worldMin.y - eye.y : (eye.y > worldMax.y ? eye.y - worldMax.y : 0.0);
            double const dz = eye.z < worldMin.z ? worldMin.z - eye.z : (eye.z > worldMax.z ? eye.z - worldMax.z : 0.0);
            if (dx * dx + dy * dy + dz * dz > view2) continue;

            for (auto index : indices) {
                auto const& block = schematic.blocks[index];
                if (session.layerMode == LayerMode::Fixed && block.pos.y != session.fixedLayer) continue;
                if (session.layerMode == LayerMode::Slice && block.pos.y != playerLocalY) continue;
                if (session.layerMode == LayerMode::Auto
                    && std::abs(block.pos.y - playerLocalY) > config.render.autoLayerRadius) {
                    continue;
                }
                if (config.render.surfaceOnly && index < schematic.surface.size() && schematic.surface[index] == 0) {
                    continue;
                }
                auto const& entry = schematic.palette[block.palette];
                if (!entry.renderable || entry.bedrockState.empty()) continue;

                auto const world = session.origin + transformLocal(block.pos, size, session.rotation, session.mirror);
                if (session.placed.contains(world)) continue;
                double const px  = world.x + 0.5 - eye.x;
                double const py  = world.y + 0.5 - eye.y;
                double const pz  = world.z + 0.5 - eye.z;
                double const d2  = px * px + py * py + pz * pz;
                if (d2 > view2) continue;
                candidates.push_back(Candidate{world, d2, block.palette});
            }
        }

        std::size_t const maxBlocks = static_cast<std::size_t>(
            std::max(1, config.render.maxBlocksPerViewer)
        );
        auto const byDistance = [](Candidate const& a, Candidate const& b) { return a.distance2 < b.distance2; };
        if (candidates.size() > maxBlocks) {
            std::nth_element(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(maxBlocks), candidates.end(), byDistance);
            candidates.resize(maxBlocks);
        }
        std::sort(candidates.begin(), candidates.end(), byDistance);

        session.desired.reserve(candidates.size() * 2 + 16);
        session.pending.reserve(candidates.size());
        for (auto const& candidate : candidates) {
            session.desired.insert(candidate.pos);
            if (!session.applied.contains(candidate.pos)) {
                session.pending.push_back(BlockChange{candidate.pos, schematic.palette[candidate.palette].bedrockState});
            }
        }
        for (auto const& pos : session.applied) {
            if (!session.desired.contains(pos)) session.pendingClears.push_back(pos);
        }

        session.lastPlanPosition  = session.position;
        session.lastPlanDimension = session.dimension;
        session.lastPlanLayer     = playerLocalY;
    }

    void tick(IGhostSink& sink) {
        for (auto it = sessions.begin(); it != sessions.end();) {
            Session& session = it->second;
            if (session.needsPlan && session.hasViewer) plan(session);

            int clearBudget = config.render.clearPerTick;
            while (clearBudget > 0 && session.forcedClearCursor < session.forcedClears.size()) {
                auto const pos = session.forcedClears[session.forcedClearCursor++];
                sink.clearBlock(session.player, pos);
                session.applied.erase(pos);
                --clearBudget;
                ++session.clearsSent;
            }
            while (clearBudget > 0 && session.pendingClearCursor < session.pendingClears.size()) {
                auto const pos = session.pendingClears[session.pendingClearCursor++];
                sink.clearBlock(session.player, pos);
                session.applied.erase(pos);
                --clearBudget;
                ++session.clearsSent;
            }
            if (session.forcedClearCursor >= session.forcedClears.size()) {
                session.forcedClears.clear();
                session.forcedClearCursor = 0;
            }
            if (session.pendingClearCursor >= session.pendingClears.size()) {
                session.pendingClears.clear();
                session.pendingClearCursor = 0;
            }

            int setBudget = config.render.blocksPerTick;
            while (setBudget > 0 && session.pendingCursor < session.pending.size()) {
                auto const& change = session.pending[session.pendingCursor++];
                bool const  ok     = sink.setBlock(session.player, change.pos, change.state);
                session.applied.insert(change.pos);
                if (ok) {
                    ++session.setsSent;
                } else {
                    ++session.setFailed;
                }
                --setBudget;
            }
            if (session.pendingCursor >= session.pending.size()) {
                session.pending.clear();
                session.pendingCursor = 0;
            }

            if (session.draining && session.applied.empty() && session.forcedClears.empty()
                && session.pendingClears.empty()) {
                session.draining = false;
            }

            bool const idle = !session.hasViewer && !session.draining && session.pending.empty()
                           && session.forcedClears.empty() && session.pendingClears.empty();
            if (idle) {
                it = sessions.erase(it);
            } else {
                ++it;
            }
        }
    }

    Result<std::filesystem::path> saveSchematic(SaveRequest const& request, std::vector<std::string>* warnings) {
        if (request.name.empty()) return Error("原理图名字不能为空");
        auto const      fileName = sanitizeFileName(request.name) + ".litematic";
        auto const      path     = schematicsDir / fileName;
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) {
            return Error("已存在同名原理图: " + path.filename().string() + "（换一个名字）");
        }
        auto written = writeLitematicFile(path, request, mapper, warnings);
        if (!written.ok()) return written.error();
        scanStore();
        return path;
    }

    CommandResult command(CommandContext const& context, std::string_view commandLine) {
        CommandResult result;
        auto          args = splitArgs(commandLine);
        if (args.empty()) {
            result.lines.push_back("§e用法: /litematica <help|list|load|place|clear|layer|rot|mirror|info|materials|reload|status>");
            return result;
        }

        std::string const action = lowerCopy(args[0]);
        auto              needPlayer = [&]() -> bool {
            if (context.player.empty()) {
                result.ok = false;
                result.lines.push_back("§c该命令需要玩家身份");
                return false;
            }
            return true;
        };

        if (action == "help") {
            result.lines = {
                "§a§lLitematicaBDS §7— 纯服务端投影",
                "§e/litematica list §7列出原理图",
                "§e/litematica load <名字> §7加载原理图",
                "§e/litematica place [名字] [x y z] §7放置投影（默认脚下）",
                "§e/litematica clear §7清除当前投影",
                "§e/litematica layer <all|auto|slice|层号> §7逐层显示（slice=脚部单层）",
                "§e/litematica rot <0|90|180|270> §7旋转投影",
                "§e/litematica mirror <none|x|z|xz> §7镜像投影",
                "§e/litematica info §7查看当前投影",
                "§e/litematica materials [数量] §7导出材料清单 CSV",
                "§e/litematica pos1 / pos2 §7设置选区角点（脚下）",
                "§e/litematica save <名字> [x1 y1 z1 x2 y2 z2] §7保存选区为 .litematic",
                "§e/litematica reload §7重载配置与原理图",
                "§e/litematica status §7运行状态",
                "§7提示：手持木棍右键打开菜单；木棍左键快速放置当前投影。",
            };
            return result;
        }

        if (action == "list") {
            if (store.empty()) {
                result.lines.push_back("§7原理图目录为空: " + schematicsDir.string());
                return result;
            }
            std::vector<std::string> names;
            names.reserve(store.size());
            for (auto const& [name, entry] : store) names.push_back(name);
            std::sort(names.begin(), names.end());
            result.lines.push_back("§a原理图 §7(" + std::to_string(names.size()) + "):");
            std::size_t shown = 0;
            for (auto const& name : names) {
                if (shown++ >= 40) {
                    result.lines.push_back("§7… 其余 " + std::to_string(names.size() - 40) + " 个省略");
                    break;
                }
                auto const& entry = store.at(name);
                if (entry.schematic) {
                    auto const& meta = entry.schematic->meta;
                    result.lines.push_back(
                        "§f" + name + " §7— " + std::to_string(meta.size.x) + "×" + std::to_string(meta.size.y)
                        + "×" + std::to_string(meta.size.z) + " / " + std::to_string(meta.totalBlocks) + " 方块"
                    );
                } else {
                    result.lines.push_back("§f" + name + " §8(未加载)");
                }
            }
            return result;
        }

        if (action == "load") {
            if (args.size() < 2) {
                result.ok = false;
                result.lines.push_back("§c用法: /litematica load <名字>");
                return result;
            }
            std::string error;
            std::string resolved;
            auto        schematic = loadNamed(args[1], resolved, error);
            if (!schematic) {
                result.ok = false;
                result.lines.push_back("§c" + error);
                return result;
            }
            if (!context.player.empty()) {
                auto& session      = sessionFor(context.player);
                session.schematic  = schematic;
                session.schematicName = resolved;
            }
            auto const& meta = schematic->meta;
            result.lines.push_back(
                "§a已加载 §f" + resolved + " §7(" + std::to_string(meta.size.x) + "×" + std::to_string(meta.size.y)
                + "×" + std::to_string(meta.size.z) + ", " + std::to_string(meta.totalBlocks) + " 方块, "
                + std::to_string(meta.regionCount) + " 区域)"
            );
            if (!meta.author.empty()) result.lines.push_back("§7作者: §f" + meta.author);
            return result;
        }

        if (action == "place") {
            if (!needPlayer()) return result;
            auto& session = sessionFor(context.player);

            std::string name;
            Vec3i       origin{
                static_cast<int32_t>(std::floor(context.position.x)),
                static_cast<int32_t>(std::floor(context.position.y)),
                static_cast<int32_t>(std::floor(context.position.z)),
            };

            std::size_t cursor = 1;
            std::size_t nameIndex = kNoIndex;
            // 支持两种写法：
            //   place <x> <y> <z> [名字]
            //   place [名字] [x y z]
            if (args.size() >= cursor + 3) {
                int x = 0;
                int y = 0;
                int z = 0;
                if (parseInt(args[cursor], x) && parseInt(args[cursor + 1], y) && parseInt(args[cursor + 2], z)) {
                    origin = {x, y, z};
                    cursor += 3;
                }
            }
            if (cursor < args.size()) {
                nameIndex = cursor++;
            }
            if (nameIndex != kNoIndex && args.size() >= nameIndex + 4) {
                int x = 0;
                int y = 0;
                int z = 0;
                if (parseInt(args[nameIndex + 1], x) && parseInt(args[nameIndex + 2], y)
                    && parseInt(args[nameIndex + 3], z)) {
                    origin = {x, y, z};
                }
            }
            if (nameIndex != kNoIndex) name = args[nameIndex];
            if (name.empty()) name = session.schematicName;
            if (name.empty()) {
                result.ok = false;
                result.lines.push_back("§c用法: /litematica place [名字] [x y z]（先 load 或给名字）");
                return result;
            }

            std::string error;
            std::string resolved;
            auto        schematic = loadNamed(name, resolved, error);
            if (!schematic) {
                result.ok = false;
                result.lines.push_back("§c" + error);
                return result;
            }

            resetProjection(session);
            session.schematic           = schematic;
            session.schematicName       = resolved;
            session.origin              = origin;
            session.projectionDimension = context.dimension;
            session.active              = true;
            session.draining            = false;
            session.needsPlan           = true;
            session.rotation            = static_cast<Rotation>(config.projection.defaultRotation);
            session.mirror              = config.projection.defaultMirrorX
                                            ? (config.projection.defaultMirrorZ ? Mirror::XZ : Mirror::X)
                                            : (config.projection.defaultMirrorZ ? Mirror::Z : Mirror::None);

            auto const& meta = schematic->meta;
            result.lines.push_back(
                "§a投影已放置: §f" + resolved + " §7@ " + formatVec(origin) + " 维度 " + std::to_string(context.dimension)
            );
            result.lines.push_back(
                "§7尺寸 " + std::to_string(meta.size.x) + "×" + std::to_string(meta.size.y) + "×"
                + std::to_string(meta.size.z) + "，总方块 " + std::to_string(meta.totalBlocks)
                + "，表面 " + std::to_string(schematic->visibleCount())
            );
            return result;
        }

        if (action == "clear") {
            if (!needPlayer()) return result;
            auto it = sessions.find(context.player);
            if (it == sessions.end() || !it->second.active) {
                result.lines.push_back("§7当前没有投影");
                return result;
            }
            auto& session = it->second;
            session.active = false;
            session.needsPlan = false;
            for (auto const& pos : session.applied) session.forcedClears.push_back(pos);
            session.applied.clear();
            session.pending.clear();
            session.pendingCursor = 0;
            session.pendingClears.clear();
            session.pendingClearCursor = 0;
            session.desired.clear();
            session.placed.clear();
            session.draining = true;
            result.lines.push_back("§a投影已清除");
            return result;
        }

        if (action == "layer") {
            if (!needPlayer()) return result;
            if (args.size() < 2) {
                result.ok = false;
                result.lines.push_back("§c用法: /litematica layer <all|auto|层号>");
                return result;
            }
            auto&     session = sessionFor(context.player);
            auto const value   = lowerCopy(args[1]);
            if (value == "all" || value == "全部") {
                session.layerMode = LayerMode::All;
            } else if (value == "auto" || value == "自动") {
                session.layerMode = LayerMode::Auto;
            } else if (value == "slice" || value == "分片" || value == "single") {
                session.layerMode = LayerMode::Slice;
            } else if (value == "next" || value == "prev" || value == "up" || value == "down") {
                if (session.layerMode != LayerMode::Fixed) {
                    session.layerMode  = LayerMode::Fixed;
                    session.fixedLayer = 0;
                } else {
                    session.fixedLayer += (value == "next" || value == "up") ? 1 : -1;
                }
            } else {
                int layer = 0;
                if (!parseInt(value, layer)) {
                    result.ok = false;
                    result.lines.push_back("§c层号必须是整数，或 all / auto / next / prev");
                    return result;
                }
                session.layerMode  = LayerMode::Fixed;
                session.fixedLayer = layer;
            }
            session.needsPlan = session.active;
            result.lines.push_back("§a显示层: §f" + std::string(value));
            return result;
        }

        if (action == "rot" || action == "rotate") {
            if (!needPlayer()) return result;
            if (args.size() < 2) {
                result.ok = false;
                result.lines.push_back("§c用法: /litematica rot <0|90|180|270>");
                return result;
            }
            auto& session = sessionFor(context.player);
            Rotation rotation{};
            auto const value = lowerCopy(args[1]);
            if (value == "next" || value == "prev") {
                int degrees = static_cast<int>(session.rotation);
                degrees     = (degrees + (value == "next" ? 90 : 270)) % 360;
                rotation    = static_cast<Rotation>(degrees);
            } else if (!parseRotation(value, rotation)) {
                result.ok = false;
                result.lines.push_back("§c旋转角度必须是 0/90/180/270/next/prev");
                return result;
            }
            if (session.active) resetProjection(session);
            session.rotation  = rotation;
            session.needsPlan = session.active;
            result.lines.push_back("§a旋转: §f" + std::string(rotationName(rotation)));
            return result;
        }

        if (action == "mirror") {
            if (!needPlayer()) return result;
            if (args.size() < 2) {
                result.ok = false;
                result.lines.push_back("§c用法: /litematica mirror <none|x|z|xz>");
                return result;
            }
            auto& session = sessionFor(context.player);
            Mirror mirror{};
            auto const value = lowerCopy(args[1]);
            if (value == "next") {
                switch (session.mirror) {
                case Mirror::None: mirror = Mirror::X; break;
                case Mirror::X:    mirror = Mirror::Z; break;
                case Mirror::Z:    mirror = Mirror::XZ; break;
                case Mirror::XZ:   mirror = Mirror::None; break;
                }
            } else if (!parseMirror(value, mirror)) {
                result.ok = false;
                result.lines.push_back("§c镜像必须是 none/x/z/xz/next");
                return result;
            }
            if (session.active) resetProjection(session);
            session.mirror    = mirror;
            session.needsPlan = session.active;
            result.lines.push_back("§a镜像: §f" + std::string(mirrorName(mirror)));
            return result;
        }

        if (action == "info") {
            if (!needPlayer()) return result;
            auto it = sessions.find(context.player);
            if (it == sessions.end() || !it->second.active) {
                result.lines.push_back("§7当前没有投影");
                return result;
            }
            auto const& session = it->second;
            auto const& meta    = session.schematic->meta;
            result.lines.push_back("§a当前投影: §f" + session.schematicName);
            result.lines.push_back("§7原点 " + formatVec(session.origin) + "，维度 " + std::to_string(session.projectionDimension));
            result.lines.push_back(
                "§7尺寸 " + std::to_string(meta.size.x) + "×" + std::to_string(meta.size.y) + "×"
                + std::to_string(meta.size.z) + "，总方块 " + std::to_string(meta.totalBlocks)
            );
            result.lines.push_back(
                "§7已显示 " + std::to_string(session.applied.size()) + "，待发送 " + std::to_string(session.pending.size())
                + "，待还原 " + std::to_string(session.pendingClears.size() + session.forcedClears.size())
            );
            result.lines.push_back(
                "§7旋转 " + std::string(rotationName(session.rotation)) + "，镜像 "
                + std::string(mirrorName(session.mirror)) + "，层 "
                + (session.layerMode == LayerMode::All
                       ? "all"
                       : (session.layerMode == LayerMode::Auto
                              ? "auto"
                              : (session.layerMode == LayerMode::Slice ? "slice" : std::to_string(session.fixedLayer))))
            );
            result.lines.push_back(
                "§7累计发包 " + std::to_string(session.setsSent) + "，失败 " + std::to_string(session.setFailed)
            );
            return result;
        }

        if (action == "materials" || action == "mat") {
            if (!needPlayer()) return result;
            auto it = sessions.find(context.player);
            if (it == sessions.end() || !it->second.schematic) {
                result.ok = false;
                result.lines.push_back("§c先 load 或 place 一个原理图");
                return result;
            }
            auto const& session = it->second;
            auto const  counts  = session.schematic->materialCounts();
            if (counts.empty()) {
                result.lines.push_back("§7没有可统计的方块");
                return result;
            }

            std::error_code ec;
            std::filesystem::create_directories(exportsDir, ec);
            auto const csvPath = exportsDir
                               / (sanitizeFileName(session.schematicName) + "-materials.csv");
            std::ofstream out(csvPath, std::ios::binary | std::ios::trunc);
            if (out) {
                out << "block,count\n";
                for (auto const& [name, count] : counts) out << name << ',' << count << '\n';
            }

            std::size_t limit = 20;
            if (args.size() >= 2) {
                int parsed = 0;
                if (parseInt(args[1], parsed) && parsed > 0) limit = std::min<std::size_t>(static_cast<std::size_t>(parsed), 100);
            }
            result.lines.push_back("§a材料清单 §7(" + std::to_string(counts.size()) + " 种，CSV: " + csvPath.string() + ")");
            std::size_t shown = 0;
            for (auto const& [name, count] : counts) {
                if (shown++ >= limit) break;
                result.lines.push_back("§f" + name + " §7× " + std::to_string(count));
            }
            return result;
        }

        if (action == "pos1" || action == "pos2" || action == "sel1" || action == "sel2") {
            if (!needPlayer()) return result;
            auto&      session = sessionFor(context.player);
            Vec3i const point{
                static_cast<int32_t>(std::floor(context.position.x)),
                static_cast<int32_t>(std::floor(context.position.y)),
                static_cast<int32_t>(std::floor(context.position.z)),
            };
            if (action == "pos1" || action == "sel1") {
                session.pos1 = point;
            } else {
                session.pos2 = point;
            }
            result.lines.push_back("§a选区角点 " + action + ": §f" + formatVec(point));
            if (session.pos1 && session.pos2) {
                auto const min = Vec3i{
                    std::min(session.pos1->x, session.pos2->x),
                    std::min(session.pos1->y, session.pos2->y),
                    std::min(session.pos1->z, session.pos2->z),
                };
                auto const max = Vec3i{
                    std::max(session.pos1->x, session.pos2->x),
                    std::max(session.pos1->y, session.pos2->y),
                    std::max(session.pos1->z, session.pos2->z),
                };
                auto const size = max - min + Vec3i{1, 1, 1};
                result.lines.push_back(
                    "§7选区: " + formatVec(min) + " → " + formatVec(max) + "，体积 "
                    + std::to_string(static_cast<std::uint64_t>(size.x) * size.y * size.z)
                );
            }
            return result;
        }

        if (action == "save") {
            if (!needPlayer()) return result;
            if (args.size() < 2) {
                result.ok = false;
                result.lines.push_back("§c用法: /litematica save <名字> [x1 y1 z1 x2 y2 z2]");
                return result;
            }

            auto&       session = sessionFor(context.player);
            std::string name    = args[1];

            Vec3i min{};
            Vec3i max{};
            bool  haveBounds = false;
            if (args.size() >= 8) {
                int x1 = 0, y1 = 0, z1 = 0, x2 = 0, y2 = 0, z2 = 0;
                if (parseInt(args[2], x1) && parseInt(args[3], y1) && parseInt(args[4], z1)
                    && parseInt(args[5], x2) && parseInt(args[6], y2) && parseInt(args[7], z2)) {
                    min = {std::min(x1, x2), std::min(y1, y2), std::min(z1, z2)};
                    max = {std::max(x1, x2), std::max(y1, y2), std::max(z1, z2)};
                    haveBounds = true;
                }
            }
            if (!haveBounds && session.pos1 && session.pos2) {
                min = {std::min(session.pos1->x, session.pos2->x), std::min(session.pos1->y, session.pos2->y),
                       std::min(session.pos1->z, session.pos2->z)};
                max = {std::max(session.pos1->x, session.pos2->x), std::max(session.pos1->y, session.pos2->y),
                       std::max(session.pos1->z, session.pos2->z)};
                haveBounds = true;
            }
            if (!haveBounds && session.active && session.schematic) {
                auto const rotated = rotatedSize(session.schematic->meta.size, session.rotation);
                min = session.origin;
                max = session.origin + rotated - Vec3i{1, 1, 1};
                haveBounds = true;
            }
            if (!haveBounds) {
                result.ok = false;
                result.lines.push_back("§c没有选区：先 /litematica pos1 与 pos2，或给 6 个坐标");
                return result;
            }

            auto const size = max - min + Vec3i{1, 1, 1};
            auto const volume = static_cast<std::uint64_t>(size.x) * static_cast<std::uint64_t>(size.y)
                              * static_cast<std::uint64_t>(size.z);
            if (volume > static_cast<std::uint64_t>(config.save.maxVolume)) {
                result.ok = false;
                result.lines.push_back(
                    "§c选区体积 " + std::to_string(volume) + " 超过上限 "
                    + std::to_string(config.save.maxVolume) + "（save.maxVolume）"
                );
                return result;
            }
            if (!sampler) {
                result.ok = false;
                result.lines.push_back("§c当前平台没有提供方块采样器，无法保存");
                return result;
            }

            int const dimension = context.dimension;
            SaveRequest request;
            request.name        = name;
            request.author      = context.player;
            request.description = "Created by LitematicaBDS";
            request.size        = size;
            request.blocks.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(volume, 262144)));

            for (int32_t y = min.y; y <= max.y; ++y) {
                for (int32_t z = min.z; z <= max.z; ++z) {
                    for (int32_t x = min.x; x <= max.x; ++x) {
                        auto sampled = sampler(context.player, Vec3i{x, y, z}, dimension);
                        if (!sampled || sampled->isAir() || sampled->name.empty()) continue;
                        request.blocks.push_back(BuildBlock{
                            Vec3i{x - min.x, y - min.y, z - min.z},
                            std::move(*sampled),
                        });
                    }
                }
            }

            std::vector<std::string> warnings;
            auto saved = saveSchematic(request, &warnings);
            if (!saved.ok()) {
                result.ok = false;
                result.lines.push_back("§c保存失败: " + saved.error().message);
                return result;
            }
            result.lines.push_back("§a已保存 §f" + saved.value().filename().string());
            result.lines.push_back(
                "§7区域 " + formatVec(min) + " → " + formatVec(max) + "，尺寸 " + std::to_string(size.x) + "×"
                + std::to_string(size.y) + "×" + std::to_string(size.z) + "，非空气 " + std::to_string(request.blocks.size())
            );
            result.lines.push_back("§7文件: " + saved.value().string());
            for (auto const& warning : warnings) result.lines.push_back("§e" + warning);
            return result;
        }

        if (action == "reload") {
            if (!context.op) {
                result.ok = false;
                result.lines.push_back("§c需要 OP 权限");
                return result;
            }
            auto warnings = std::vector<std::string>{};
            auto loaded   = loadConfigFile(configPath, &warnings);
            if (!loaded.ok()) {
                result.ok = false;
                result.lines.push_back("§c" + loaded.error().message);
                return result;
            }
            config = loaded.value();
            rebuildPaths();
            scanStore();
            result.lines.push_back("§a配置与原理图已重载（" + std::to_string(store.size()) + " 个原理图）");
            for (auto const& warning : warnings) result.lines.push_back("§e" + warning);
            return result;
        }

        if (action == "status") {
            result.lines.push_back("§aLitematicaBDS " + std::string(LITEMATICABDS_VERSION));
            result.lines.push_back("§7原理图: " + std::to_string(store.size()) + "，在线投影会话: " + std::to_string(sessions.size()));
            result.lines.push_back(
                "§7渲染: 每 tick " + std::to_string(config.render.blocksPerTick) + " 格，视距 "
                + std::to_string(config.render.viewDistance) + "，上限 " + std::to_string(config.render.maxBlocksPerViewer)
            );
            result.lines.push_back("§7映射表: " + std::to_string(mapper.tableSize()) + " 项");
            return result;
        }

        result.ok = false;
        result.lines.push_back("§c未知命令: " + action + "（/litematica help）");
        return result;
    }
};

LitematicaService::LitematicaService(Options options) : mImpl(std::make_unique<Impl>()) {
    mImpl->options = std::move(options);
}

LitematicaService::~LitematicaService() = default;

void LitematicaService::setLogger(LogFn logger) { mImpl->logger = std::move(logger); }

void LitematicaService::setStateValidator(StateValidator validator) { mImpl->validator = std::move(validator); }

void LitematicaService::setBlockSampler(BlockSampler sampler) { mImpl->sampler = std::move(sampler); }

std::filesystem::path const& LitematicaService::schematicsDir() const { return mImpl->schematicsDir; }

Result<std::filesystem::path> LitematicaService::saveSchematic(
    SaveRequest const&        request,
    std::vector<std::string>* warnings
) {
    return mImpl->saveSchematic(request, warnings);
}

Result<void> LitematicaService::initialize() {
    std::error_code ec;
    std::filesystem::create_directories(mImpl->options.dataDir, ec);
    if (ec) return Error("无法创建数据目录: " + ec.message());

    mImpl->rebuildPaths();

    std::vector<std::string> warnings;
    auto                     loaded = loadConfigFile(mImpl->configPath, &warnings);
    if (!loaded.ok()) return loaded.error();
    mImpl->config = loaded.value();
    for (auto const& warning : warnings) mImpl->log(LogLevel::Warn, warning);

    mImpl->rebuildPaths();
    std::filesystem::create_directories(mImpl->schematicsDir, ec);
    std::filesystem::create_directories(mImpl->exportsDir, ec);
    mImpl->scanStore();

    mImpl->log(
        LogLevel::Info,
        "LitematicaBDS " + std::string(LITEMATICABDS_VERSION) + " 初始化完成，映射表 " + std::to_string(mImpl->mapper.tableSize()) + " 项"
    );
    return {};
}

void LitematicaService::reload() {
    std::vector<std::string> warnings;
    auto                     loaded = loadConfigFile(mImpl->configPath, &warnings);
    if (loaded.ok()) {
        mImpl->config = loaded.value();
    } else {
        mImpl->log(LogLevel::Error, loaded.error().message);
    }
    for (auto const& warning : warnings) mImpl->log(LogLevel::Warn, warning);
    mImpl->rebuildPaths();
    mImpl->scanStore();
}

Result<void> LitematicaService::updateConfig(Config const& config) {
    auto saved = saveConfigFile(mImpl->configPath, config);
    if (!saved.ok()) return saved;

    auto const oldSchematics = mImpl->schematicsDir;
    auto const oldExports    = mImpl->exportsDir;

    mImpl->config = config;
    mImpl->rebuildPaths();
    if (oldSchematics != mImpl->schematicsDir || oldExports != mImpl->exportsDir) {
        std::error_code ec;
        std::filesystem::create_directories(mImpl->schematicsDir, ec);
        std::filesystem::create_directories(mImpl->exportsDir, ec);
        mImpl->scanStore();
    }

    for (auto& [player, session] : mImpl->sessions) {
        (void)player;
        session.needsPlan = true;
    }
    mImpl->log(LogLevel::Info, "配置已更新并写回 " + mImpl->configPath.string());
    return {};
}

CommandResult LitematicaService::execute(CommandContext const& context, std::string_view commandLine) {
    try {
        return mImpl->command(context, commandLine);
    } catch (std::exception const& e) {
        CommandResult result;
        result.ok = false;
        result.lines.push_back(std::string("§c命令执行失败: ") + e.what());
        return result;
    }
}

void LitematicaService::updateViewer(std::string_view player, Vec3d position, int dimension) {
    if (player.empty()) return;
    auto& session     = mImpl->sessionFor(std::string(player));
    session.position  = position;
    session.dimension = dimension;
    session.hasViewer = true;

    if (!session.active) return;

    bool needPlan = session.needsPlan;
    if (session.projectionDimension != dimension) needPlan = true;
    if (session.lastPlanDimension != dimension) needPlan = true;
    if (distanceSquared(position, session.lastPlanPosition)
        > static_cast<double>(mImpl->config.render.planMoveThreshold) * mImpl->config.render.planMoveThreshold) {
        needPlan = true;
    }
    if (session.layerMode == LayerMode::Auto || session.layerMode == LayerMode::Slice) {
        int const localY = static_cast<int>(std::floor(position.y)) - session.origin.y;
        if (localY != session.lastPlanLayer) needPlan = true;
    }
    if (needPlan) session.needsPlan = true;
}

void LitematicaService::removeViewer(std::string_view player) {
    auto it = mImpl->sessions.find(std::string(player));
    if (it == mImpl->sessions.end()) return;
    auto& session     = it->second;
    session.hasViewer = false;
    if (session.active || !session.applied.empty()) {
        session.active = false;
        session.needsPlan = false;
        for (auto const& pos : session.applied) session.forcedClears.push_back(pos);
        session.applied.clear();
        session.pending.clear();
        session.pendingCursor = 0;
        session.pendingClears.clear();
        session.pendingClearCursor = 0;
        session.desired.clear();
        session.draining = true;
    } else {
        mImpl->sessions.erase(it);
    }
}

void LitematicaService::markBlockPlaced(std::string_view player, Vec3i pos) {
    auto it = mImpl->sessions.find(std::string(player));
    if (it == mImpl->sessions.end()) return;
    auto& session = it->second;
    session.placed.insert(pos);
    session.applied.erase(pos);
    session.desired.erase(pos);
    session.pending.clear();
    session.pendingCursor = 0;
    session.needsPlan       = session.active;
    session.lastPlanLayer   = std::numeric_limits<int>::min();
}

std::optional<Bounds> LitematicaService::selection(std::string_view player) const {
    auto it = mImpl->sessions.find(std::string(player));
    if (it == mImpl->sessions.end()) return std::nullopt;
    auto const& session = it->second;
    if (!session.pos1 || !session.pos2) return std::nullopt;
    Bounds bounds;
    bounds.min = {std::min(session.pos1->x, session.pos2->x), std::min(session.pos1->y, session.pos2->y),
                  std::min(session.pos1->z, session.pos2->z)};
    bounds.max = {std::max(session.pos1->x, session.pos2->x), std::max(session.pos1->y, session.pos2->y),
                  std::max(session.pos1->z, session.pos2->z)};
    return bounds;
}

void LitematicaService::tick(IGhostSink& sink, std::uint64_t /*nowMs*/) { mImpl->tick(sink); }

void LitematicaService::shutdown(IGhostSink& sink) {
    for (auto& [name, session] : mImpl->sessions) {
        for (auto const& pos : session.applied) sink.clearBlock(name, pos);
        session.applied.clear();
        session.pending.clear();
        session.forcedClears.clear();
        session.pendingClears.clear();
    }
    mImpl->sessions.clear();
}

Config const& LitematicaService::config() const { return mImpl->config; }

BlockMapper const& LitematicaService::mapper() const { return mImpl->mapper; }

std::vector<ViewerStatus> LitematicaService::viewers() const {
    std::vector<ViewerStatus> out;
    out.reserve(mImpl->sessions.size());
    for (auto const& [name, session] : mImpl->sessions) {
        ViewerStatus status;
        status.player        = name;
        status.schematic     = session.schematicName;
        status.active        = session.active;
        status.origin        = session.origin;
        status.dimension     = session.projectionDimension;
        status.totalBlocks   = session.schematic ? session.schematic->meta.totalBlocks : 0;
        status.appliedBlocks = session.applied.size();
        status.pendingBlocks = session.pending.size();
        if (session.schematic) status.size = session.schematic->meta.size;
        status.rotation = rotationName(session.rotation);
        status.mirror   = mirrorName(session.mirror);
        status.layer    = session.layerMode == LayerMode::All
                            ? "all"
                            : (session.layerMode == LayerMode::Auto
                                   ? "auto"
                                   : (session.layerMode == LayerMode::Slice ? "slice" : std::to_string(session.fixedLayer)));
        out.push_back(std::move(status));
    }
    return out;
}

std::vector<std::string> LitematicaService::schematicNames() const {
    std::vector<std::string> names;
    names.reserve(mImpl->store.size());
    for (auto const& [name, entry] : mImpl->store) names.push_back(name);
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace litematicabds

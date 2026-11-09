#include "litematicabds/Config.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>

#include "util/Json.h"

namespace litematicabds {
namespace {

using util::Json;

void warnType(std::vector<std::string>* warnings, std::string const& path, char const* expected) {
    if (warnings != nullptr && warnings->size() < 50) {
        warnings->push_back("配置项 " + path + " 类型不是 " + expected + "，使用默认值");
    }
}

int readInt(Json const& object, char const* key, int fallback, std::string const& path, std::vector<std::string>* warnings) {
    auto const* value = object.find(key);
    if (value == nullptr) return fallback;
    if (!value->isNumber()) {
        warnType(warnings, path + "." + key, "数字");
        return fallback;
    }
    return static_cast<int>(value->numberValue);
}

bool readBool(Json const& object, char const* key, bool fallback, std::string const& path, std::vector<std::string>* warnings) {
    auto const* value = object.find(key);
    if (value == nullptr) return fallback;
    if (!value->isBool()) {
        warnType(warnings, path + "." + key, "布尔");
        return fallback;
    }
    return value->boolValue;
}

std::string readString(
    Json const&                object,
    char const*                key,
    std::string                fallback,
    std::string const&         path,
    std::vector<std::string>*  warnings
) {
    auto const* value = object.find(key);
    if (value == nullptr) return fallback;
    if (!value->isString()) {
        warnType(warnings, path + "." + key, "字符串");
        return fallback;
    }
    return value->stringValue;
}

Json const& section(Json const& root, char const* key, std::vector<std::string>* warnings) {
    auto const* value = root.find(key);
    if (value != nullptr && value->isObject()) return *value;
    if (value != nullptr) warnType(warnings, key, "对象");
    static Json const empty;
    return empty;
}

} // namespace

Result<Config> loadConfigFile(std::filesystem::path const& path, std::vector<std::string>* warnings) {
    Config config;

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return config;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) return Error("无法打开配置文件: " + path.string());
    std::ostringstream buffer;
    buffer << in.rdbuf();
    std::string const text = buffer.str();
    if (text.size() > 4u * 1024u * 1024u) return Error("配置文件过大");

    auto parsed = util::parseJson(text);
    if (!parsed.ok()) return parsed.error();
    if (!parsed.value().isObject()) return Error("配置根节点必须是对象");

    auto const& root = parsed.value();

    auto const& render = section(root, "render", warnings);
    config.render.blocksPerTick = std::clamp(readInt(render, "blocksPerTick", config.render.blocksPerTick, "render", warnings), 1, 4096);
    config.render.clearPerTick  = std::clamp(readInt(render, "clearPerTick", config.render.clearPerTick, "render", warnings), 1, 4096);
    config.render.viewDistance  = std::clamp(readInt(render, "viewDistance", config.render.viewDistance, "render", warnings), 8, 256);
    config.render.autoLayerRadius =
        std::clamp(readInt(render, "autoLayerRadius", config.render.autoLayerRadius, "render", warnings), 0, 16);
    config.render.surfaceOnly = readBool(render, "surfaceOnly", config.render.surfaceOnly, "render", warnings);
    config.render.maxBlocksPerViewer =
        std::clamp(readInt(render, "maxBlocksPerViewer", config.render.maxBlocksPerViewer, "render", warnings), 64, 200000);
    config.render.planMoveThreshold =
        std::clamp(readInt(render, "planMoveThreshold", config.render.planMoveThreshold, "render", warnings), 1, 32);
    config.render.showStatusHud = readBool(render, "showStatusHud", config.render.showStatusHud, "render", warnings);

    auto const& projection = section(root, "projection", warnings);
    config.projection.showBounds = readBool(projection, "showBounds", config.projection.showBounds, "projection", warnings);
    config.projection.boundsColor =
        readString(projection, "boundsColor", config.projection.boundsColor, "projection", warnings);
    config.projection.defaultRotation =
        std::clamp(readInt(projection, "defaultRotation", config.projection.defaultRotation, "projection", warnings), 0, 270);
    config.projection.defaultMirrorX =
        readBool(projection, "defaultMirrorX", config.projection.defaultMirrorX, "projection", warnings);
    config.projection.defaultMirrorZ =
        readBool(projection, "defaultMirrorZ", config.projection.defaultMirrorZ, "projection", warnings);

    // 旋转只接受 0/90/180/270，其它值回退 0。
    if (config.projection.defaultRotation % 90 != 0) {
        if (warnings != nullptr) warnings->push_back("projection.defaultRotation 不是 90 的倍数，按 0 处理");
        config.projection.defaultRotation = 0;
    }

    auto const& sulfur = section(root, "sulfur", warnings);
    config.sulfur.enabled = readBool(sulfur, "enabled", config.sulfur.enabled, "sulfur", warnings);
    config.sulfur.radius  = std::clamp(readInt(sulfur, "radius", config.sulfur.radius, "sulfur", warnings), 1, 32);
    config.sulfur.maxDisplays =
        std::clamp(readInt(sulfur, "maxDisplays", config.sulfur.maxDisplays, "sulfur", warnings), 1, 512);
    config.sulfur.visible   = readBool(sulfur, "visible", config.sulfur.visible, "sulfur", warnings);
    config.sulfur.faceBlock = readBool(sulfur, "faceBlock", config.sulfur.faceBlock, "sulfur", warnings);

    auto const& fill = section(root, "fill", warnings);
    config.fill.enabled   = readBool(fill, "enabled", config.fill.enabled, "fill", warnings);
    config.fill.requireOp   = readBool(fill, "requireOp", config.fill.requireOp, "fill", warnings);
    config.fill.consumeItem = readBool(fill, "consumeItem", config.fill.consumeItem, "fill", warnings);
    config.fill.quickFill   = readBool(fill, "quickFill", config.fill.quickFill, "fill", warnings);

    auto const& save = section(root, "save", warnings);
    config.save.maxVolume = std::clamp(readInt(save, "maxVolume", config.save.maxVolume, "save", warnings), 1, 8'000'000);

    auto const& storage = section(root, "storage", warnings);
    config.storage.schematicsDir =
        readString(storage, "schematicsDir", config.storage.schematicsDir, "storage", warnings);
    config.storage.exportsDir = readString(storage, "exportsDir", config.storage.exportsDir, "storage", warnings);

    config.debug = readBool(root, "debug", config.debug, "", warnings);

    return config;
}

namespace {

std::string jsonQuote(std::string_view value) {
    std::string out;
    out.push_back('"');
    for (unsigned char c : value) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out.push_back(static_cast<char>(c));
            break;
        }
    }
    out.push_back('"');
    return out;
}

char const* boolText(bool value) { return value ? "true" : "false"; }

} // namespace

Result<void> saveConfigFile(std::filesystem::path const& path, Config const& config) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return Error("无法写入配置文件: " + path.string());

    out << "{\n"
        << "  \"render\": {\n"
        << "    \"blocksPerTick\": " << config.render.blocksPerTick << ",\n"
        << "    \"clearPerTick\": " << config.render.clearPerTick << ",\n"
        << "    \"viewDistance\": " << config.render.viewDistance << ",\n"
        << "    \"autoLayerRadius\": " << config.render.autoLayerRadius << ",\n"
        << "    \"surfaceOnly\": " << boolText(config.render.surfaceOnly) << ",\n"
        << "    \"maxBlocksPerViewer\": " << config.render.maxBlocksPerViewer << ",\n"
        << "    \"planMoveThreshold\": " << config.render.planMoveThreshold << ",\n"
        << "    \"showStatusHud\": " << boolText(config.render.showStatusHud) << "\n"
        << "  },\n"
        << "  \"projection\": {\n"
        << "    \"showBounds\": " << boolText(config.projection.showBounds) << ",\n"
        << "    \"boundsColor\": " << jsonQuote(config.projection.boundsColor) << ",\n"
        << "    \"defaultRotation\": " << config.projection.defaultRotation << ",\n"
        << "    \"defaultMirrorX\": " << boolText(config.projection.defaultMirrorX) << ",\n"
        << "    \"defaultMirrorZ\": " << boolText(config.projection.defaultMirrorZ) << "\n"
        << "  },\n"
        << "  \"sulfur\": {\n"
        << "    \"enabled\": " << boolText(config.sulfur.enabled) << ",\n"
        << "    \"radius\": " << config.sulfur.radius << ",\n"
        << "    \"maxDisplays\": " << config.sulfur.maxDisplays << ",\n"
        << "    \"visible\": " << boolText(config.sulfur.visible) << ",\n"
        << "    \"faceBlock\": " << boolText(config.sulfur.faceBlock) << "\n"
        << "  },\n"
        << "  \"fill\": {\n"
        << "    \"enabled\": " << boolText(config.fill.enabled) << ",\n"
        << "    \"requireOp\": " << boolText(config.fill.requireOp) << ",\n"
        << "    \"consumeItem\": " << boolText(config.fill.consumeItem) << ",\n"
        << "    \"quickFill\": " << boolText(config.fill.quickFill) << "\n"
        << "  },\n"
        << "  \"save\": {\n"
        << "    \"maxVolume\": " << config.save.maxVolume << "\n"
        << "  },\n"
        << "  \"storage\": {\n"
        << "    \"schematicsDir\": " << jsonQuote(config.storage.schematicsDir) << ",\n"
        << "    \"exportsDir\": " << jsonQuote(config.storage.exportsDir) << "\n"
        << "  },\n"
        << "  \"debug\": " << boolText(config.debug) << "\n"
        << "}\n";

    if (!out) return Error("配置文件写入失败: " + path.string());
    return {};
}

} // namespace litematicabds

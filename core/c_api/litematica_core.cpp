#include "litematica_core.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "litematicabds/Service.h"
#include "litematicabds/Version.h"

using namespace litematicabds;

namespace {

struct CollectSink : IGhostSink {
    std::vector<litbds_change>& out;

    explicit CollectSink(std::vector<litbds_change>& sink) : out(sink) {}

    static void fill(litbds_change& change, int kind, std::string_view player, Vec3i pos, std::string_view state) {
        change = litbds_change{};
        change.kind = kind;
        change.x    = pos.x;
        change.y    = pos.y;
        change.z    = pos.z;
        if (!state.empty()) {
            std::size_t const count = std::min(state.size(), sizeof(change.state) - 1);
            std::memcpy(change.state, state.data(), count);
            change.state[count] = '\0';
        }
        if (!player.empty()) {
            std::size_t const count = std::min(player.size(), sizeof(change.player) - 1);
            std::memcpy(change.player, player.data(), count);
            change.player[count] = '\0';
        }
    }

    bool setBlock(std::string_view player, Vec3i pos, std::string_view state) override {
        litbds_change change{};
        fill(change, 0, player, pos, state);
        out.push_back(change);
        return true;
    }

    bool clearBlock(std::string_view player, Vec3i pos) override {
        litbds_change change{};
        fill(change, 1, player, pos, {});
        out.push_back(change);
        return true;
    }
};

void copyOut(char* out, std::size_t capacity, std::string const& text) {
    if (out == nullptr || capacity == 0) return;
    std::size_t const count = std::min(text.size(), capacity - 1);
    std::memcpy(out, text.data(), count);
    out[count] = '\0';
}

} // namespace

struct litbds_core {
    std::unique_ptr<LitematicaService> service;
    std::vector<litbds_change>         outbox;
    std::size_t                        outCursor{0};
    std::string                        lastError;
    litbds_block_sampler_fn            sampler{nullptr};
    void*                              samplerUser{nullptr};
};

extern "C" {

uint32_t litbds_api_version(void) { return LITBDS_CORE_ABI_VERSION; }

const char* litbds_version(void) { return LITEMATICABDS_VERSION; }

litbds_core* litbds_create(const char* data_dir, const char* schematics_dir, const char* exports_dir) {
    if (data_dir == nullptr) return nullptr;
    try {
        auto core = std::make_unique<litbds_core>();
        LitematicaService::Options options;
        options.dataDir = std::filesystem::path(data_dir);
        if (schematics_dir != nullptr && *schematics_dir != '\0') options.schematicsDir = schematics_dir;
        if (exports_dir != nullptr && *exports_dir != '\0') options.exportsDir = exports_dir;
        core->service = std::make_unique<LitematicaService>(std::move(options));
        return core.release();
    } catch (...) {
        return nullptr;
    }
}

void litbds_destroy(litbds_core* core) { delete core; }

int32_t litbds_initialize(litbds_core* core) {
    if (core == nullptr || !core->service) return -1;
    auto result = core->service->initialize();
    if (!result.ok()) {
        core->lastError = result.error().message;
        return -1;
    }
    return 0;
}

void litbds_reload(litbds_core* core) {
    if (core != nullptr && core->service) core->service->reload();
}

int32_t litbds_execute(
    litbds_core* core,
    const char*  player,
    double       x,
    double       y,
    double       z,
    int32_t      dimension,
    int32_t      op,
    const char*  command,
    char*        out,
    size_t       out_capacity
) {
    if (core == nullptr || !core->service) return -1;
    CommandContext context;
    context.player    = player != nullptr ? player : "";
    context.position  = {x, y, z};
    context.dimension = dimension;
    context.op        = op != 0;

    auto result = core->service->execute(context, command != nullptr ? command : "");
    std::string text;
    for (auto const& line : result.lines) {
        if (!text.empty()) text.push_back('\n');
        text += line;
    }
    copyOut(out, out_capacity, text);
    return result.ok ? 1 : 0;
}

void litbds_set_sampler(litbds_core* core, litbds_block_sampler_fn sampler, void* user) {
    if (core == nullptr || !core->service) return;
    core->sampler     = sampler;
    core->samplerUser = user;
    if (sampler == nullptr) {
        core->service->setBlockSampler({});
        return;
    }
    core->service->setBlockSampler([core](std::string_view player, Vec3i pos, int dimension) -> std::optional<BlockState> {
        char buffer[192] = {};
        if (core->sampler == nullptr) return std::nullopt;
        std::string name(player);
        auto const  ok = core->sampler(
            name.c_str(),
            pos.x,
            pos.y,
            pos.z,
            dimension,
            buffer,
            sizeof(buffer),
            core->samplerUser
        );
        if (ok == 0) return std::nullopt;
        buffer[sizeof(buffer) - 1] = 0;
        if (auto parsed = parseBlockState(buffer)) return parsed;
        return std::nullopt;
    });
}

int32_t litbds_update_viewer(litbds_core* core, const char* player, double x, double y, double z, int32_t dimension) {
    if (core == nullptr || !core->service || player == nullptr) return -1;
    core->service->updateViewer(player, {x, y, z}, dimension);
    return 0;
}

int32_t litbds_remove_viewer(litbds_core* core, const char* player) {
    if (core == nullptr || !core->service || player == nullptr) return -1;
    core->service->removeViewer(player);
    return 0;
}

int32_t litbds_tick(litbds_core* core, uint64_t now_ms) {
    if (core == nullptr || !core->service) return -1;
    core->outbox.clear();
    core->outCursor = 0;
    CollectSink sink(core->outbox);
    core->service->tick(sink, now_ms);
    return static_cast<int32_t>(core->outbox.size());
}

int32_t litbds_poll(litbds_core* core, litbds_change* out, size_t capacity) {
    if (core == nullptr || out == nullptr || capacity == 0) return 0;
    std::size_t count = 0;
    while (core->outCursor < core->outbox.size() && count < capacity) {
        out[count++] = core->outbox[core->outCursor++];
    }
    if (core->outCursor >= core->outbox.size()) {
        core->outbox.clear();
        core->outCursor = 0;
    }
    return static_cast<int32_t>(count);
}

void litbds_shutdown(litbds_core* core) {
    if (core == nullptr || !core->service) return;
    core->outbox.clear();
    core->outCursor = 0;
    CollectSink sink(core->outbox);
    core->service->shutdown(sink);
}

size_t litbds_schematic_count(litbds_core* core) {
    if (core == nullptr || !core->service) return 0;
    return core->service->schematicNames().size();
}

int32_t litbds_schematic_name_at(litbds_core* core, size_t index, char* out, size_t capacity) {
    if (core == nullptr || !core->service) return -1;
    auto names = core->service->schematicNames();
    if (index >= names.size()) return -1;
    copyOut(out, capacity, names[index]);
    return 0;
}

const char* litbds_last_error(litbds_core* core) {
    return core != nullptr ? core->lastError.c_str() : "core is null";
}

} // extern "C"

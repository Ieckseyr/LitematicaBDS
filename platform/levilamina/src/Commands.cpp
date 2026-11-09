#include "Plugin.h"

#include <string>
#include <string_view>

#include <ll/api/command/CommandHandle.h>
#include <ll/api/command/CommandRegistrar.h>

#include <mc/server/commands/CommandOrigin.h>
#include <mc/server/commands/CommandOutput.h>
#include <mc/server/commands/CommandPermissionLevel.h>
#include <mc/world/actor/player/Player.h>

namespace litematicabds::platform {

// 命令参数结构体必须放在命名空间作用域（pfr 反射 + 链接要求）。
struct NameParam {
    std::string name{""};
};

struct PlaceParam {
    std::string name{""};
};

struct PlaceAtParam {
    int         x{0};
    int         y{0};
    int         z{0};
    std::string name{""};
};

struct ValueParam {
    std::string value{""};
};

struct LimitParam {
    std::string limit{""};
};

struct SaveParam {
    std::string name{""};
    std::string x1{""};
    std::string y1{""};
    std::string z1{""};
    std::string x2{""};
    std::string y2{""};
    std::string z2{""};
};

namespace {

Player* playerOf(CommandOrigin const& origin) {
    auto* entity = origin.getEntity();
    return entity != nullptr && entity->isPlayer() ? static_cast<Player*>(entity) : nullptr;
}

CommandContext makeContext(CommandOrigin const& origin) {
    CommandContext context;
    if (auto* player = playerOf(origin); player != nullptr) {
        context.player    = player->getRealName();
        auto const  pos   = player->getFeetPos();
        context.position  = {pos.x, pos.y, pos.z};
        context.dimension = static_cast<int>(player->getDimensionId());
        context.op        = static_cast<int>(player->getCommandPermissionLevel()) >= 1;
    } else {
        context.op = true; // 控制台
    }
    return context;
}

std::string quoteIfNeeded(std::string const& value) {
    if (value.find_first_of(" \t\"'") == std::string::npos) return value;
    std::string out = "\"";
    for (char c : value) {
        if (c == '"') out.push_back('\\');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

void run(Plugin& plugin, CommandOrigin const& origin, CommandOutput& output, std::string const& line) {
    auto* service = plugin.service();
    if (service == nullptr) {
        output.error("§cLitematicaBDS 尚未初始化");
        return;
    }
    auto result = service->execute(makeContext(origin), line);
    for (auto const& message : result.lines) output.success(message);
}

} // namespace

void Plugin::registerCommands() {
    auto& cmd = ll::command::CommandRegistrar::getInstance(false).getOrCreateCommand(
        "litematica",
        "§a纯服务端投影（HologramLib 前置）",
        CommandPermissionLevel::Any
    );
    cmd.alias("lit");

    cmd.overload().execute([](CommandOrigin const& origin, CommandOutput& output) {
        run(Plugin::getInstance(), origin, output, "help");
    });

    cmd.overload().text("help").execute([](CommandOrigin const& origin, CommandOutput& output) {
        run(Plugin::getInstance(), origin, output, "help");
    });

    cmd.overload().text("list").execute([](CommandOrigin const& origin, CommandOutput& output) {
        run(Plugin::getInstance(), origin, output, "list");
    });

    cmd.overload<NameParam>()
        .text("load")
        .required("name")
        .execute([](CommandOrigin const& origin, CommandOutput& output, NameParam const& param) {
            run(Plugin::getInstance(), origin, output, "load " + quoteIfNeeded(param.name));
        });

    cmd.overload<PlaceParam>()
        .text("place")
        .optional("name")
        .execute([](CommandOrigin const& origin, CommandOutput& output, PlaceParam const& param) {
            run(
                Plugin::getInstance(),
                origin,
                output,
                param.name.empty() ? "place" : "place " + quoteIfNeeded(param.name)
            );
        });

    cmd.overload<PlaceAtParam>()
        .text("placeat")
        .required("x")
        .required("y")
        .required("z")
        .optional("name")
        .execute([](CommandOrigin const& origin, CommandOutput& output, PlaceAtParam const& param) {
            std::string line = "place ";
            if (!param.name.empty()) line += quoteIfNeeded(param.name) + " ";
            line += std::to_string(param.x) + " " + std::to_string(param.y) + " " + std::to_string(param.z);
            run(Plugin::getInstance(), origin, output, line);
        });

    cmd.overload().text("clear").execute([](CommandOrigin const& origin, CommandOutput& output) {
        run(Plugin::getInstance(), origin, output, "clear");
    });

    cmd.overload<ValueParam>()
        .text("layer")
        .required("value")
        .execute([](CommandOrigin const& origin, CommandOutput& output, ValueParam const& param) {
            run(Plugin::getInstance(), origin, output, "layer " + param.value);
        });

    cmd.overload<ValueParam>()
        .text("rot")
        .required("value")
        .execute([](CommandOrigin const& origin, CommandOutput& output, ValueParam const& param) {
            run(Plugin::getInstance(), origin, output, "rot " + param.value);
        });

    cmd.overload<ValueParam>()
        .text("mirror")
        .required("value")
        .execute([](CommandOrigin const& origin, CommandOutput& output, ValueParam const& param) {
            run(Plugin::getInstance(), origin, output, "mirror " + param.value);
        });

    cmd.overload().text("info").execute([](CommandOrigin const& origin, CommandOutput& output) {
        run(Plugin::getInstance(), origin, output, "info");
    });

    cmd.overload<LimitParam>()
        .text("materials")
        .optional("limit")
        .execute([](CommandOrigin const& origin, CommandOutput& output, LimitParam const& param) {
            run(
                Plugin::getInstance(),
                origin,
                output,
                param.limit.empty() ? "materials" : "materials " + param.limit
            );
        });

    cmd.overload().text("reload").execute([](CommandOrigin const& origin, CommandOutput& output) {
        run(Plugin::getInstance(), origin, output, "reload");
    });

    cmd.overload().text("pos1").execute([](CommandOrigin const& origin, CommandOutput& output) {
        run(Plugin::getInstance(), origin, output, "pos1");
    });

    cmd.overload().text("pos2").execute([](CommandOrigin const& origin, CommandOutput& output) {
        run(Plugin::getInstance(), origin, output, "pos2");
    });

    cmd.overload<SaveParam>()
        .text("save")
        .optional("name")
        .optional("x1")
        .optional("y1")
        .optional("z1")
        .optional("x2")
        .optional("y2")
        .optional("z2")
        .execute([](CommandOrigin const& origin, CommandOutput& output, SaveParam const& param) {
            std::string line = "save";
            for (auto const* value : {&param.name, &param.x1, &param.y1, &param.z1, &param.x2, &param.y2, &param.z2}) {
                if (!value->empty()) line += " " + quoteIfNeeded(*value);
            }
            run(Plugin::getInstance(), origin, output, line);
        });

    cmd.overload().text("status").execute([](CommandOrigin const& origin, CommandOutput& output) {
        run(Plugin::getInstance(), origin, output, "status");
    });

}

} // namespace litematicabds::platform

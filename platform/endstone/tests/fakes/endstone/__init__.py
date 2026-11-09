"""测试用的 endstone 假实现：只覆盖 LitematicaBDS 套壳用到的 API。"""

from __future__ import annotations

from pathlib import Path


class _DimensionType:
    def __init__(self, value: int) -> None:
        self.value = value


class _BlockData:
    block_states = {"stone_type": "stone"}


class _Block:
    type = "minecraft:stone"
    data = _BlockData()


class _Dimension:
    def __init__(self, value: int) -> None:
        self.type = _DimensionType(value)

    def get_block_at(self, x: int, y: int, z: int):
        return _Block()


class _Location:
    def __init__(self, x: float = 0.0, y: float = 64.0, z: float = 0.0) -> None:
        self.x = x
        self.y = y
        self.z = z


class Player:
    def __init__(self, name: str = "Steve", dimension: int = 0, op: bool = True) -> None:
        self.name = name
        self.location = _Location()
        self.dimension = _Dimension(dimension)
        self.is_op = op
        self.messages: list[str] = []

    def send_message(self, message: str) -> None:
        self.messages.append(message)

    def send_form(self, form) -> None:
        self.messages.append("FORM")


class ConsoleSender:
    name = "Console"

    def __init__(self) -> None:
        self.messages: list[str] = []

    def send_message(self, message: str) -> None:
        self.messages.append(message)

    def send_form(self, form) -> None:
        self.messages.append("FORM")

    def send_error_message(self, message: str) -> None:
        self.messages.append(message)


class _Task:
    def __init__(self, fn) -> None:
        self.fn = fn
        self.cancelled = False

    def cancel(self) -> None:
        self.cancelled = True


class _Scheduler:
    def __init__(self) -> None:
        self.tasks: list[_Task] = []

    def run_task(self, plugin, task, delay: int = 0, period: int = 0) -> _Task:
        wrapped = _Task(task)
        self.tasks.append(wrapped)
        return wrapped


class _PluginManager:
    def __init__(self, plugins: dict) -> None:
        self._plugins = plugins

    def get_plugin(self, name: str):
        return self._plugins[name]


class _Server:
    def __init__(self, plugins: dict, players: list[Player]) -> None:
        self.plugin_manager = _PluginManager(plugins)
        self.scheduler = _Scheduler()
        self.online_players = players

    def get_player(self, name: str):
        for player in self.online_players:
            if player.name == name:
                return player
        return None


class _Logger:
    def __init__(self) -> None:
        self.lines: list[str] = []

    def info(self, message: str) -> None:
        self.lines.append(f"INFO {message}")

    def warning(self, message: str) -> None:
        self.lines.append(f"WARN {message}")

    def error(self, message: str) -> None:
        self.lines.append(f"ERROR {message}")


class Plugin:
    commands = None
    permissions = None

    def __init__(self) -> None:
        self._server: _Server | None = None
        self._data_folder = Path(".")
        self._logger = _Logger()

    @property
    def server(self) -> _Server:
        assert self._server is not None
        return self._server

    @property
    def data_folder(self) -> Path:
        return self._data_folder

    @property
    def logger(self) -> _Logger:
        return self._logger

    def register_events(self, listener) -> None:
        pass

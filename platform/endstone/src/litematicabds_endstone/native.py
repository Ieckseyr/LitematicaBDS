"""litematica_core.dll 的 ctypes 绑定（与 core/c_api/litematica_core.h 逐字段对齐）。"""

from __future__ import annotations

import ctypes
from pathlib import Path

STATE_LEN = 192
PLAYER_LEN = 64


SamplerFn = ctypes.CFUNCTYPE(
    ctypes.c_int32,
    ctypes.c_char_p,
    ctypes.c_int32,
    ctypes.c_int32,
    ctypes.c_int32,
    ctypes.c_int32,
    ctypes.POINTER(ctypes.c_char),
    ctypes.c_size_t,
    ctypes.c_void_p,
)


class Change(ctypes.Structure):
    _fields_ = [
        ("kind", ctypes.c_int32),
        ("x", ctypes.c_int32),
        ("y", ctypes.c_int32),
        ("z", ctypes.c_int32),
        ("state", ctypes.c_char * STATE_LEN),
        ("player", ctypes.c_char * PLAYER_LEN),
    ]


class CoreError(RuntimeError):
    pass


class NativeCore:
    """对 litematica_core.dll 的薄封装。所有调用必须在服务器主线程。"""

    def __init__(self, dll_path: str | Path | None = None) -> None:
        path = Path(dll_path) if dll_path is not None else Path(__file__).parent / "_native" / "litematica_core.dll"
        if not path.exists():
            raise CoreError(f"缺少核心库: {path}（先运行 xmake build litematica_core）")
        self._lib = ctypes.CDLL(str(path))
        self._handle: int | None = None
        self._sampler = None
        self._bind()

    def _bind(self) -> None:
        lib = self._lib
        lib.litbds_api_version.restype = ctypes.c_uint32
        lib.litbds_version.restype = ctypes.c_char_p
        lib.litbds_create.restype = ctypes.c_void_p
        lib.litbds_create.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p]
        lib.litbds_destroy.argtypes = [ctypes.c_void_p]
        lib.litbds_initialize.argtypes = [ctypes.c_void_p]
        lib.litbds_reload.argtypes = [ctypes.c_void_p]
        lib.litbds_execute.argtypes = [
            ctypes.c_void_p,
            ctypes.c_char_p,
            ctypes.c_double,
            ctypes.c_double,
            ctypes.c_double,
            ctypes.c_int32,
            ctypes.c_int32,
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_size_t,
        ]
        lib.litbds_set_sampler.argtypes = [ctypes.c_void_p, SamplerFn, ctypes.c_void_p]
        lib.litbds_update_viewer.argtypes = [
            ctypes.c_void_p,
            ctypes.c_char_p,
            ctypes.c_double,
            ctypes.c_double,
            ctypes.c_double,
            ctypes.c_int32,
        ]
        lib.litbds_remove_viewer.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        lib.litbds_tick.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
        lib.litbds_tick.restype = ctypes.c_int32
        lib.litbds_poll.argtypes = [ctypes.c_void_p, ctypes.POINTER(Change), ctypes.c_size_t]
        lib.litbds_poll.restype = ctypes.c_int32
        lib.litbds_shutdown.argtypes = [ctypes.c_void_p]
        lib.litbds_schematic_count.argtypes = [ctypes.c_void_p]
        lib.litbds_schematic_count.restype = ctypes.c_size_t
        lib.litbds_schematic_name_at.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_size_t]
        lib.litbds_last_error.restype = ctypes.c_char_p
        lib.litbds_last_error.argtypes = [ctypes.c_void_p]

    @property
    def api_version(self) -> int:
        return int(self._lib.litbds_api_version())

    @property
    def version(self) -> str:
        return self._lib.litbds_version().decode("utf-8", "replace")

    def create(self, data_dir: str | Path, schematics_dir: str | Path | None = None,
               exports_dir: str | Path | None = None) -> None:
        if self._handle:
            raise CoreError("core 已经创建")
        handle = self._lib.litbds_create(
            str(data_dir).encode("utf-8"),
            str(schematics_dir).encode("utf-8") if schematics_dir else None,
            str(exports_dir).encode("utf-8") if exports_dir else None,
        )
        if not handle:
            raise CoreError("litbds_create 失败")
        self._handle = int(handle)

    def initialize(self) -> None:
        handle = self._require()
        if self._lib.litbds_initialize(handle) != 0:
            raise CoreError(self.last_error)

    def reload(self) -> None:
        self._lib.litbds_reload(self._require())

    def execute(
        self,
        player: str,
        x: float,
        y: float,
        z: float,
        dimension: int,
        op: bool,
        command: str,
    ) -> tuple[bool, str]:
        handle = self._require()
        out = ctypes.create_string_buffer(16384)
        result = self._lib.litbds_execute(
            handle,
            player.encode("utf-8"),
            float(x),
            float(y),
            float(z),
            int(dimension),
            1 if op else 0,
            command.encode("utf-8"),
            out,
            len(out),
        )
        return result == 1, out.value.decode("utf-8", "replace")

    def set_sampler(self, callback) -> None:
        """注册世界方块采样器（save 命令用）。callback 为 None 时注销。"""
        handle = self._require()
        if callback is None:
            self._sampler = None
            self._lib.litbds_set_sampler(handle, None, None)
            return
        self._sampler = SamplerFn(callback)
        self._lib.litbds_set_sampler(handle, self._sampler, None)

    def update_viewer(self, player: str, x: float, y: float, z: float, dimension: int) -> None:
        self._lib.litbds_update_viewer(
            self._require(), player.encode("utf-8"), float(x), float(y), float(z), int(dimension)
        )

    def remove_viewer(self, player: str) -> None:
        self._lib.litbds_remove_viewer(self._require(), player.encode("utf-8"))

    def tick(self, now_ms: int) -> None:
        self._lib.litbds_tick(self._require(), int(now_ms))

    def poll(self, capacity: int = 4096) -> list[Change]:
        handle = self._require()
        batch = (Change * capacity)()
        count = int(self._lib.litbds_poll(handle, batch, capacity))
        return [batch[i] for i in range(max(0, count))]

    def shutdown(self) -> None:
        if self._handle:
            self._lib.litbds_shutdown(self._handle)

    def schematic_names(self) -> list[str]:
        handle = self._require()
        names: list[str] = []
        for index in range(int(self._lib.litbds_schematic_count(handle))):
            buffer = ctypes.create_string_buffer(256)
            if self._lib.litbds_schematic_name_at(handle, index, buffer, len(buffer)) == 0:
                names.append(buffer.value.decode("utf-8", "replace"))
        return names

    @property
    def last_error(self) -> str:
        if not self._handle:
            return "core 尚未创建"
        return self._lib.litbds_last_error(self._handle).decode("utf-8", "replace")

    def close(self) -> None:
        if self._handle:
            self._lib.litbds_destroy(self._handle)
            self._handle = None

    def _require(self) -> int:
        if not self._handle:
            raise CoreError("core 尚未创建")
        return self._handle

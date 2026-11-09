"""LitematicaBDS 的 endstone 套壳。

C++ 核心（litematica_core.dll）负责原理图解析、投影调度与状态映射；
Python 只做三件事：喂玩家位置、把 BlockChange 交给 HologramLib、转接命令。
"""

from .plugin import LitematicaBDSPlugin

__all__ = ["LitematicaBDSPlugin"]
__version__ = "0.1.0"

def event_handler(func):
    func._is_event_handler = True
    func._priority = 0
    func._ignore_cancelled = False
    return func


class PlayerQuitEvent:
    def __init__(self, player) -> None:
        self.player = player


class PlayerInteractEvent:
    class Action:
        LEFT_CLICK_BLOCK = 0
        RIGHT_CLICK_BLOCK = 1
        LEFT_CLICK_AIR = 2
        RIGHT_CLICK_AIR = 3

    LEFT_CLICK_BLOCK = Action.LEFT_CLICK_BLOCK
    RIGHT_CLICK_BLOCK = Action.RIGHT_CLICK_BLOCK
    LEFT_CLICK_AIR = Action.LEFT_CLICK_AIR
    RIGHT_CLICK_AIR = Action.RIGHT_CLICK_AIR

    def __init__(self, player, action=3, item=None) -> None:
        self.player = player
        self.action = action
        self.item = item
        self.has_item = item is not None
        self.cancelled = False

    def cancel(self) -> None:
        self.cancelled = True

class ActionForm:
    def __init__(self, title: str = "", content: str = "", **kwargs) -> None:
        self.title = title
        self.content = content
        self.buttons = []

    def add_button(self, text: str, icon=None, on_click=None):
        self.buttons.append(text)
        return self

    def add_label(self, text: str):
        return self

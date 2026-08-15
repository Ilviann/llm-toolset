"""Tkinter widget construction and passive deployment-window updates."""

from __future__ import annotations

import queue

from .discovery import default_engine_root
from .models import INSTALL_IN_PROJECT


OUTPUT_TAB_TITLES = ("Build log output", "MCP settings preview")


class DeploymentView:
    """Build the deployment window without owning validation or background work."""

    def __init__(self) -> None:
        import tkinter as tk
        from tkinter import ttk

        self.tk = tk
        self.ttk = ttk
        self.root = tk.Tk()
        self.root.title("Unreal MCP — Windows Deployment")
        self.root.geometry("820x860")
        self.root.minsize(680, 760)
        self.events: queue.Queue[tuple[str, object]] = queue.Queue()
        self.project_value = tk.StringVar()
        self.engine_value = tk.StringVar(value=default_engine_root())
        self.include_gas_value = tk.BooleanVar(value=False)
        self.include_commonui_value = tk.BooleanVar(value=False)
        self.include_enhanced_input_value = tk.BooleanVar(value=False)
        self.include_pdb_value = tk.BooleanVar(value=False)
        self.install_method_value = tk.StringVar(value=INSTALL_IN_PROJECT)
        self.writable_value = tk.BooleanVar(value=False)
        self.lifecycle_value = tk.BooleanVar(value=False)
        self.status_value = tk.StringVar(value="Select the folder containing your .uproject file.")
        self.busy = False
        self._build()
        self.root.protocol("WM_DELETE_WINDOW", self._close)
        self.root.after(100, self._poll_events)

    def _build(self) -> None:
        from tkinter import scrolledtext

        frame = self.ttk.Frame(self.root, padding=14)
        frame.pack(fill="both", expand=True)
        frame.columnconfigure(1, weight=1)
        frame.rowconfigure(12, weight=1)
        self.ttk.Label(frame, text="Unreal project folder").grid(row=0, column=0, sticky="w")
        self.project_entry = self.ttk.Entry(frame, textvariable=self.project_value)
        self.project_entry.grid(row=0, column=1, sticky="ew", padx=8)
        self.project_button = self.ttk.Button(frame, text="Browse…", command=self._browse_project)
        self.project_button.grid(row=0, column=2)
        self.ttk.Label(frame, text="Unreal Engine folder").grid(row=1, column=0, sticky="w", pady=(10, 0))
        self.engine_entry = self.ttk.Entry(frame, textvariable=self.engine_value)
        self.engine_entry.grid(row=1, column=1, sticky="ew", padx=8, pady=(10, 0))
        self.engine_button = self.ttk.Button(frame, text="Browse…", command=self._browse_engine)
        self.engine_button.grid(row=1, column=2, pady=(10, 0))
        self.ttk.Label(frame, text="Close Unreal Editor before installing. The build uses the selected Engine and Visual Studio.", wraplength=760).grid(row=2, column=0, columnspan=3, sticky="w", pady=(12, 0))
        self.include_gas_checkbox = self.ttk.Checkbutton(frame, text="Build and install Unreal MCP GAS companion plugin", variable=self.include_gas_value)
        self.include_gas_checkbox.grid(row=3, column=0, columnspan=3, sticky="w", pady=(10, 0))
        self.include_commonui_checkbox = self.ttk.Checkbutton(frame, text="Build and install Unreal MCP CommonUI companion plugin", variable=self.include_commonui_value)
        self.include_commonui_checkbox.grid(row=4, column=0, columnspan=3, sticky="w", pady=(8, 0))
        self.include_enhanced_input_checkbox = self.ttk.Checkbutton(frame, text="Build and install Unreal MCP Enhanced Input companion plugin", variable=self.include_enhanced_input_value)
        self.include_enhanced_input_checkbox.grid(row=5, column=0, columnspan=3, sticky="w", pady=(8, 0))
        self.include_pdb_checkbox = self.ttk.Checkbutton(frame, text="Include matching PDB crash symbols (larger installation)", variable=self.include_pdb_value)
        self.include_pdb_checkbox.grid(row=6, column=0, columnspan=3, sticky="w", pady=(8, 0))
        methods = self.ttk.LabelFrame(frame, text="Install method", padding=8)
        methods.grid(row=7, column=0, columnspan=3, sticky="ew", pady=(10, 0))
        from .models import INSTALL_IN_ENGINE_DISABLED, INSTALL_IN_ENGINE_ENABLED
        self.install_method_buttons = tuple(
            self.ttk.Radiobutton(methods, text=label, variable=self.install_method_value, value=value)
            for label, value in (
                ("Install into project (and enable)", INSTALL_IN_PROJECT),
                ("Install into engine (and set enabled by default)", INSTALL_IN_ENGINE_ENABLED),
                ("Install into engine (without enabling by default)", INSTALL_IN_ENGINE_DISABLED),
            )
        )
        for row, button in enumerate(self.install_method_buttons):
            button.grid(row=row, column=0, sticky="w", pady=(0 if row == 0 else 4, 0))
        self.writable_checkbox = self.ttk.Checkbutton(frame, text="Enable writable MCP tools in the generated MCP entries", variable=self.writable_value)
        self.writable_checkbox.grid(row=8, column=0, columnspan=3, sticky="w", pady=(8, 0))
        self.lifecycle_checkbox = self.ttk.Checkbutton(frame, text="Enable editor lifecycle control using the selected Engine", variable=self.lifecycle_value)
        self.lifecycle_checkbox.grid(row=9, column=0, columnspan=3, sticky="w", pady=(8, 0))
        self.install_button = self.ttk.Button(frame, text="Build and install selected plugins", command=self._install)
        self.install_button.grid(row=10, column=0, columnspan=3, sticky="ew", pady=12)
        self.ttk.Label(frame, textvariable=self.status_value, wraplength=760).grid(row=11, column=0, columnspan=3, sticky="w")
        previews = self.ttk.Notebook(frame)
        previews.grid(row=12, column=0, columnspan=3, sticky="nsew", pady=(8, 0))
        build_log = self.ttk.Frame(previews, padding=10)
        build_log.columnconfigure(0, weight=1)
        build_log.rowconfigure(0, weight=1)
        previews.add(build_log, text=OUTPUT_TAB_TITLES[0])
        self.log_text = scrolledtext.ScrolledText(build_log, height=18, state="disabled", wrap="word")
        self.log_text.grid(row=0, column=0, sticky="nsew")
        settings = self.ttk.Frame(previews, padding=10)
        settings.columnconfigure(0, weight=1)
        settings.rowconfigure(1, weight=1)
        previews.add(settings, text=OUTPUT_TAB_TITLES[1])
        header = self.ttk.Frame(settings)
        header.grid(row=0, column=0, sticky="ew", pady=(0, 8))
        header.columnconfigure(0, weight=1)
        self.ttk.Label(header, text="Sample entries for LM Studio and ChatGPT Codex").grid(row=0, column=0, sticky="w")
        self.copy_button = self.ttk.Button(header, text="Copy preview", command=self._copy_configuration, state="disabled")
        self.copy_button.grid(row=0, column=1, sticky="e")
        self.configuration_text = scrolledtext.ScrolledText(settings, height=18, state="disabled", wrap="none")
        self.configuration_text.grid(row=1, column=0, sticky="nsew")

    def _set_configuration(self, configuration: str) -> None:
        self.configuration_text.configure(state="normal")
        self.configuration_text.delete("1.0", "end")
        self.configuration_text.insert("1.0", configuration)
        self.configuration_text.configure(state="disabled")

    def _clear_configuration(self) -> None:
        self.configuration_text.configure(state="normal")
        self.configuration_text.delete("1.0", "end")
        self.configuration_text.configure(state="disabled")

    def _set_busy(self, busy: bool) -> None:
        self.busy = busy
        state = "disabled" if busy else "normal"
        for widget in (self.project_entry, self.project_button, self.engine_entry, self.engine_button, self.include_gas_checkbox, self.include_commonui_checkbox, self.include_enhanced_input_checkbox, self.include_pdb_checkbox, self.writable_checkbox, self.lifecycle_checkbox, self.install_button, *self.install_method_buttons):
            widget.configure(state=state)

    def _append_log(self, message: str) -> None:
        self.log_text.configure(state="normal")
        self.log_text.insert("end", message + "\n")
        self.log_text.see("end")
        self.log_text.configure(state="disabled")

    def _copy_configuration(self) -> None:
        self._copy_value(self.configuration_text.get("1.0", "end-1c"))

    def _copy_value(self, value: str) -> None:
        if not value:
            self.status_value.set("Install the selected plugins before copying configuration.")
            return
        self.root.clipboard_clear()
        self.root.clipboard_append(value)
        self.root.update()
        self.status_value.set("Configuration value copied to the clipboard.")

    def run(self) -> None:
        self.root.mainloop()

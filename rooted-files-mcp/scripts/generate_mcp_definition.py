#!/usr/bin/env python3
"""Generate Rooted Files MCP launch settings with a small tkinter GUI."""

from __future__ import annotations

import json
import sys
from pathlib import Path


SERVER_NAME = "rooted-files"
HOST_MODES = ("standard", "markdown")
SERVER_PATH = Path(__file__).resolve().parents[1] / "server.py"


def _existing_file(path: str | Path, label: str) -> Path:
    try:
        resolved = Path(path).expanduser().resolve(strict=True)
    except (OSError, RuntimeError, ValueError):
        raise ValueError(f"{label} is inaccessible") from None
    if not resolved.is_file():
        raise ValueError(f"{label} is not a file")
    return resolved


def _existing_folder(path: str | Path) -> Path:
    try:
        resolved = Path(path).expanduser().resolve(strict=True)
    except (OSError, RuntimeError, ValueError):
        raise ValueError("Root folder is inaccessible") from None
    if not resolved.is_dir():
        raise ValueError("Root path is not a folder")
    return resolved


def build_server_definition(
    root: str | Path,
    mode: str,
    *,
    python_executable: str | Path | None = None,
    server_path: str | Path = SERVER_PATH,
) -> dict[str, object]:
    """Return one validated, JSON-compatible stdio server definition."""

    if mode not in HOST_MODES:
        raise ValueError("Mode must be standard or markdown")

    resolved_root = _existing_folder(root)
    resolved_python = _existing_file(
        python_executable if python_executable is not None else sys.executable,
        "Python executable",
    )
    resolved_server = _existing_file(server_path, "Server script")
    return {
        "command": str(resolved_python),
        "args": [
            str(resolved_server),
            str(resolved_root),
            "--mode",
            mode,
        ],
    }


def format_mcp_json(server_definition: dict[str, object]) -> str:
    """Format a complete LM Studio-compatible mcpServers object."""

    return json.dumps(
        {"mcpServers": {SERVER_NAME: server_definition}},
        ensure_ascii=False,
        indent=2,
    )


def main() -> None:
    try:
        import tkinter as tk
        from tkinter import filedialog, messagebox, ttk
    except ImportError:
        raise SystemExit(
            "tkinter is required to run the MCP definition generator "
            f"(Python: {sys.executable or 'unknown'})"
        ) from None

    class MCPDefinitionApp:
        def __init__(self, window: tk.Tk) -> None:
            self.window = window
            self.window.title("Rooted Files MCP Definition Generator")
            self.window.minsize(760, 650)

            self.root_path = tk.StringVar()
            self.mode = tk.StringVar(value="standard")
            self.server_name = tk.StringVar(value=SERVER_NAME)
            self.python_path = tk.StringVar()
            self.argument_values = [tk.StringVar() for _ in range(4)]
            self.status = tk.StringVar(
                value="Choose the folder that the MCP server may access."
            )

            container = ttk.Frame(window, padding=16)
            container.grid(row=0, column=0, sticky="nsew")
            window.rowconfigure(0, weight=1)
            window.columnconfigure(0, weight=1)
            container.columnconfigure(1, weight=1)
            container.rowconfigure(5, weight=1)

            ttk.Label(container, text="Folder to serve").grid(
                row=0, column=0, sticky="w", padx=(0, 10), pady=(0, 10)
            )
            ttk.Entry(
                container, textvariable=self.root_path, state="readonly"
            ).grid(row=0, column=1, sticky="ew", pady=(0, 10))
            ttk.Button(container, text="Browse…", command=self.choose_root).grid(
                row=0, column=2, padx=(10, 0), pady=(0, 10)
            )

            ttk.Label(container, text="Mode").grid(
                row=1, column=0, sticky="w", padx=(0, 10), pady=(0, 12)
            )
            mode_frame = ttk.Frame(container)
            mode_frame.grid(row=1, column=1, columnspan=2, sticky="w", pady=(0, 12))
            for index, mode in enumerate(HOST_MODES):
                ttk.Radiobutton(
                    mode_frame,
                    text=mode.capitalize(),
                    value=mode,
                    variable=self.mode,
                    command=self.refresh,
                ).grid(row=0, column=index, padx=(0, 18))

            ttk.Button(
                container, text="Generate definition", command=self.refresh
            ).grid(row=2, column=0, columnspan=3, sticky="w", pady=(0, 16))

            json_header = ttk.Frame(container)
            json_header.grid(row=3, column=0, columnspan=3, sticky="ew")
            json_header.columnconfigure(0, weight=1)
            ttk.Label(
                json_header,
                text="LM Studio mcp.json (complete object)",
            ).grid(row=0, column=0, sticky="w")
            ttk.Button(
                json_header,
                text="Copy JSON",
                command=lambda: self.copy_text(self.json_output.get("1.0", "end-1c")),
            ).grid(row=0, column=1, sticky="e")

            json_frame = ttk.Frame(container)
            json_frame.grid(
                row=5, column=0, columnspan=3, sticky="nsew", pady=(6, 18)
            )
            json_frame.rowconfigure(0, weight=1)
            json_frame.columnconfigure(0, weight=1)
            self.json_output = tk.Text(
                json_frame,
                height=13,
                width=82,
                wrap="none",
                font=("TkFixedFont", 11),
                state="disabled",
            )
            json_scroll_y = ttk.Scrollbar(
                json_frame, orient="vertical", command=self.json_output.yview
            )
            json_scroll_x = ttk.Scrollbar(
                json_frame, orient="horizontal", command=self.json_output.xview
            )
            self.json_output.configure(
                yscrollcommand=json_scroll_y.set,
                xscrollcommand=json_scroll_x.set,
            )
            self.json_output.grid(row=0, column=0, sticky="nsew")
            json_scroll_y.grid(row=0, column=1, sticky="ns")
            json_scroll_x.grid(row=1, column=0, sticky="ew")

            codex = ttk.LabelFrame(
                container, text="ChatGPT Codex app — STDIO server", padding=10
            )
            codex.grid(row=6, column=0, columnspan=3, sticky="ew")
            codex.columnconfigure(1, weight=1)

            self._copyable_row(
                codex,
                row=0,
                label="Name",
                value=self.server_name,
            )
            self._copyable_row(
                codex,
                row=1,
                label="Command / Python",
                value=self.python_path,
            )
            for index, value in enumerate(self.argument_values, start=1):
                self._copyable_row(
                    codex,
                    row=index + 1,
                    label=f"Argument {index}",
                    value=value,
                )

            ttk.Label(
                container,
                textvariable=self.status,
            ).grid(row=7, column=0, columnspan=3, sticky="w", pady=(12, 0))

        def _copyable_row(
            self,
            parent: ttk.LabelFrame,
            *,
            row: int,
            label: str,
            value: tk.StringVar,
        ) -> None:
            ttk.Label(parent, text=label).grid(
                row=row, column=0, sticky="w", padx=(0, 10), pady=3
            )
            ttk.Entry(parent, textvariable=value, state="readonly").grid(
                row=row, column=1, sticky="ew", pady=3
            )
            ttk.Button(
                parent,
                text="Copy",
                command=lambda item=value: self.copy_text(item.get()),
            ).grid(row=row, column=2, padx=(10, 0), pady=3)

        def choose_root(self) -> None:
            selected = filedialog.askdirectory(
                parent=self.window,
                title="Choose the folder to serve",
                mustexist=True,
            )
            if selected:
                self.root_path.set(selected)
                self.refresh()

        def refresh(self) -> None:
            if not self.root_path.get():
                self.status.set("Choose a folder before generating the definition.")
                return
            try:
                definition = build_server_definition(
                    self.root_path.get(), self.mode.get()
                )
            except ValueError as exc:
                messagebox.showerror(
                    "Cannot generate definition", str(exc), parent=self.window
                )
                self.status.set(str(exc))
                return

            self.python_path.set(str(definition["command"]))
            arguments = definition["args"]
            assert isinstance(arguments, list)
            for variable, argument in zip(self.argument_values, arguments):
                variable.set(str(argument))
            self._set_json(format_mcp_json(definition))
            self.status.set(
                "Definition ready. Copy the complete JSON or the individual "
                "Codex STDIO fields."
            )

        def _set_json(self, value: str) -> None:
            self.json_output.configure(state="normal")
            self.json_output.delete("1.0", "end")
            self.json_output.insert("1.0", value)
            self.json_output.configure(state="disabled")

        def copy_text(self, value: str) -> None:
            if not value:
                self.status.set("Generate the definition before copying this field.")
                return
            self.window.clipboard_clear()
            self.window.clipboard_append(value)
            self.window.update()
            self.status.set("Copied to the clipboard.")

    window = tk.Tk()
    MCPDefinitionApp(window)
    window.mainloop()


if __name__ == "__main__":
    main()

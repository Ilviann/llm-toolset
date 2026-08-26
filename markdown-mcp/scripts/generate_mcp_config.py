#!/usr/bin/env python3
"""Generate Markdown MCP host configuration with a small tkinter GUI."""

from __future__ import annotations

import json
import sys
import tkinter as tk
from collections.abc import Mapping
from pathlib import Path
from tkinter import filedialog, messagebox, ttk


SERVER_NAME = "markdown"
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
    *,
    writable: bool = False,
    python_executable: str | Path | None = None,
    server_path: str | Path = SERVER_PATH,
) -> dict[str, object]:
    """Return one validated, JSON-compatible stdio server definition."""

    if not isinstance(writable, bool):
        raise ValueError("Writable must be true or false")

    resolved_root = _existing_folder(root)
    resolved_python = _existing_file(
        python_executable if python_executable is not None else sys.executable,
        "Python executable",
    )
    resolved_server = _existing_file(server_path, "Server script")
    arguments = [str(resolved_server), str(resolved_root)]
    if writable:
        arguments.append("--writable")
    return {
        "command": str(resolved_python),
        "args": arguments,
    }


def format_mcp_json(server_definition: Mapping[str, object]) -> str:
    """Format a complete LM Studio-compatible mcpServers object."""

    return json.dumps(
        {"mcpServers": {SERVER_NAME: dict(server_definition)}},
        ensure_ascii=False,
        indent=2,
    )


def format_codex_toml(server_definition: Mapping[str, object]) -> str:
    """Format a Codex config.toml MCP server entry."""

    command = server_definition.get("command")
    arguments = server_definition.get("args")
    if not isinstance(command, str) or not isinstance(arguments, list) or not all(
        isinstance(argument, str) for argument in arguments
    ):
        raise ValueError(
            "MCP server definition must contain a string command and string args"
        )
    encoded_arguments = ",\n".join(
        f"  {json.dumps(argument, ensure_ascii=False)}" for argument in arguments
    )
    return (
        f"[mcp_servers.{SERVER_NAME}]\n"
        f"command = {json.dumps(command, ensure_ascii=False)}\n"
        "args = [\n"
        f"{encoded_arguments}\n"
        "]"
    )


def format_mcp_settings_preview(server_definition: Mapping[str, object]) -> str:
    """Format LM Studio and Codex configuration snippets."""

    return (
        "mcp.json (LM Studio)\n"
        "====================\n\n"
        f"{format_mcp_json(server_definition)}\n\n"
        "config.toml (Codex)\n"
        "===================\n\n"
        f"{format_codex_toml(server_definition)}"
    )


def main() -> None:
    class MCPConfigApp:
        def __init__(self, window: tk.Tk) -> None:
            self.window = window
            self.window.title("Markdown MCP Configuration Generator")
            self.window.minsize(760, 620)

            self.root_path = tk.StringVar()
            self.writable = tk.BooleanVar(value=False)
            self.status = tk.StringVar(
                value="Choose the folder containing the Markdown files to serve."
            )

            container = ttk.Frame(window, padding=16)
            container.grid(row=0, column=0, sticky="nsew")
            window.rowconfigure(0, weight=1)
            window.columnconfigure(0, weight=1)
            container.columnconfigure(1, weight=1)
            container.rowconfigure(4, weight=1)

            ttk.Label(container, text="Markdown root folder").grid(
                row=0, column=0, sticky="w", padx=(0, 10), pady=(0, 10)
            )
            ttk.Entry(
                container, textvariable=self.root_path, state="readonly"
            ).grid(row=0, column=1, sticky="ew", pady=(0, 10))
            ttk.Button(container, text="Browse…", command=self.choose_root).grid(
                row=0, column=2, padx=(10, 0), pady=(0, 10)
            )

            ttk.Checkbutton(
                container,
                text="Enable Markdown editing tools (--writable)",
                variable=self.writable,
                command=self.refresh,
            ).grid(row=1, column=0, columnspan=3, sticky="w", pady=(0, 12))

            ttk.Button(
                container, text="Generate configuration", command=self.refresh
            ).grid(row=2, column=0, columnspan=3, sticky="w", pady=(0, 16))

            settings_header = ttk.Frame(container)
            settings_header.grid(row=3, column=0, columnspan=3, sticky="ew")
            settings_header.columnconfigure(0, weight=1)
            ttk.Label(settings_header, text="MCP configuration preview").grid(
                row=0, column=0, sticky="w"
            )
            ttk.Button(
                settings_header,
                text="Copy configuration",
                command=lambda: self.copy_text(
                    self.settings_output.get("1.0", "end-1c")
                ),
            ).grid(row=0, column=1, sticky="e")

            settings_frame = ttk.Frame(container)
            settings_frame.grid(
                row=4, column=0, columnspan=3, sticky="nsew", pady=(6, 18)
            )
            settings_frame.rowconfigure(0, weight=1)
            settings_frame.columnconfigure(0, weight=1)
            self.settings_output = tk.Text(
                settings_frame,
                height=23,
                width=82,
                wrap="none",
                font=("TkFixedFont", 11),
                state="disabled",
            )
            settings_scroll_y = ttk.Scrollbar(
                settings_frame, orient="vertical", command=self.settings_output.yview
            )
            settings_scroll_x = ttk.Scrollbar(
                settings_frame, orient="horizontal", command=self.settings_output.xview
            )
            self.settings_output.configure(
                yscrollcommand=settings_scroll_y.set,
                xscrollcommand=settings_scroll_x.set,
            )
            self.settings_output.grid(row=0, column=0, sticky="nsew")
            settings_scroll_y.grid(row=0, column=1, sticky="ns")
            settings_scroll_x.grid(row=1, column=0, sticky="ew")

            ttk.Label(container, textvariable=self.status).grid(
                row=5, column=0, columnspan=3, sticky="w"
            )

        def choose_root(self) -> None:
            selected = filedialog.askdirectory(
                parent=self.window,
                title="Choose the Markdown root folder",
                mustexist=True,
            )
            if selected:
                self.root_path.set(selected)
                self.refresh()

        def refresh(self) -> None:
            if not self.root_path.get():
                self._set_settings("")
                self.status.set("Choose a folder before generating configuration.")
                return
            try:
                definition = build_server_definition(
                    self.root_path.get(), writable=self.writable.get()
                )
            except ValueError as exc:
                self._set_settings("")
                messagebox.showerror(
                    "Cannot generate configuration", str(exc), parent=self.window
                )
                self.status.set(str(exc))
                return

            self._set_settings(format_mcp_settings_preview(definition))
            access = "writable" if self.writable.get() else "read-only"
            self.status.set(
                f"{access.capitalize()} configuration ready for LM Studio or Codex."
            )

        def _set_settings(self, value: str) -> None:
            self.settings_output.configure(state="normal")
            self.settings_output.delete("1.0", "end")
            self.settings_output.insert("1.0", value)
            self.settings_output.configure(state="disabled")

        def copy_text(self, value: str) -> None:
            if not value:
                self.status.set("Generate configuration before copying it.")
                return
            self.window.clipboard_clear()
            self.window.clipboard_append(value)
            self.window.update()
            self.status.set("Copied configuration to the clipboard.")

    window = tk.Tk()
    MCPConfigApp(window)
    window.mainloop()


if __name__ == "__main__":
    main()

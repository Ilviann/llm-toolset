#!/usr/bin/env python3
"""Build and install a symbol-free UnrealMCP binary plugin on Windows."""

from __future__ import annotations

import json
import os
import platform
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Mapping, Sequence

try:
    from scripts import package_plugin
except ModuleNotFoundError:  # Direct execution puts this script's directory on sys.path.
    import package_plugin  # type: ignore[no-redef]


APPLICATION_ROOT = Path(__file__).resolve().parents[1]
SERVER_ENTRY = APPLICATION_ROOT / "server.py"
PLUGIN_NAME = "UnrealMCP"
MAX_PROJECT_DESCRIPTOR_BYTES = 1024 * 1024
MAX_PROJECT_DIRECTORY_ENTRIES = 4096
MAX_MODULE_RULE_BYTES = 64 * 1024
MAX_REGISTRY_INSTALLATIONS = 256
DEBUG_SUFFIXES = frozenset(
    {".pdb", ".ipdb", ".iobj", ".idb", ".ilk", ".obj", ".pch", ".map", ".debug"}
)
IMPLEMENTATION_SOURCE_SUFFIXES = frozenset({".c", ".cc", ".cpp", ".h", ".hh", ".hpp", ".inl"})
MODULE_RULE_INSERTION_POINT = "        PCHUsage ="
PRECOMPILED_MODULE_RULE = "        bUsePrecompiled = true;\n"
_WINDOWS_REPARSE_POINT = 0x0400


class DeploymentError(RuntimeError):
    """Raised when a deployment input or result is unsafe or unusable."""


@dataclass(frozen=True)
class ProjectInfo:
    folder: Path
    descriptor: Path
    engine_association: str


def resolved(path: Path) -> Path:
    return path.expanduser().resolve()


def is_within(path: Path, directory: Path) -> bool:
    try:
        path.relative_to(directory)
    except ValueError:
        return False
    return True


def is_reparse_point(path: Path) -> bool:
    try:
        return bool(path.lstat().st_file_attributes & _WINDOWS_REPARSE_POINT)
    except (AttributeError, OSError):
        return path.is_symlink()


def read_json_object(path: Path, label: str) -> dict[str, object]:
    try:
        with path.open("rb") as stream:
            data = stream.read(MAX_PROJECT_DESCRIPTOR_BYTES + 1)
        if len(data) > MAX_PROJECT_DESCRIPTOR_BYTES:
            raise DeploymentError(f"{label} is larger than 1 MiB: {path}")
        value = json.loads(data.decode("utf-8-sig"))
    except DeploymentError:
        raise
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise DeploymentError(f"{label} is not readable JSON: {path}: {error}") from error
    if not isinstance(value, dict):
        raise DeploymentError(f"{label} must contain one JSON object: {path}")
    return value


def locate_project(folder: Path) -> ProjectInfo:
    folder = resolved(folder)
    if not folder.is_dir():
        raise DeploymentError(f"project folder does not exist: {folder}")
    descriptors: list[Path] = []
    try:
        for index, path in enumerate(folder.iterdir()):
            if index >= MAX_PROJECT_DIRECTORY_ENTRIES:
                raise DeploymentError(
                    f"project folder contains more than {MAX_PROJECT_DIRECTORY_ENTRIES} entries"
                )
            if path.is_file() and path.suffix.casefold() == ".uproject":
                descriptors.append(path)
    except DeploymentError:
        raise
    except OSError as error:
        raise DeploymentError(f"could not inspect project folder {folder}: {error}") from error
    descriptors.sort(key=lambda path: path.name.casefold())
    if not descriptors:
        raise DeploymentError(f"project folder contains no .uproject file: {folder}")
    if len(descriptors) != 1:
        names = ", ".join(path.name for path in descriptors[:5])
        raise DeploymentError(
            f"project folder must contain exactly one .uproject file; found {len(descriptors)}: {names}"
        )
    descriptor = descriptors[0]
    project = read_json_object(descriptor, "project descriptor")
    association = project.get("EngineAssociation", "")
    if association is None:
        association = ""
    if not isinstance(association, str) or len(association) > 128:
        raise DeploymentError("EngineAssociation must be a string of at most 128 characters")
    return ProjectInfo(folder, descriptor, association.strip())


def registry_installations() -> list[tuple[str, Path]]:
    if platform.system() != "Windows":
        return []
    try:
        import winreg
    except ImportError:
        return []

    installations: list[tuple[str, Path]] = []
    try:
        with winreg.OpenKey(
            winreg.HKEY_LOCAL_MACHINE,
            r"SOFTWARE\EpicGames\Unreal Engine",
            0,
            winreg.KEY_READ | getattr(winreg, "KEY_WOW64_64KEY", 0),
        ) as base:
            index = 0
            while index < MAX_REGISTRY_INSTALLATIONS:
                try:
                    association = winreg.EnumKey(base, index)
                except OSError:
                    break
                index += 1
                try:
                    with winreg.OpenKey(base, association) as version_key:
                        directory, _ = winreg.QueryValueEx(version_key, "InstalledDirectory")
                    if isinstance(directory, str):
                        installations.append((association, Path(directory)))
                except OSError:
                    continue
    except OSError:
        pass

    try:
        with winreg.OpenKey(
            winreg.HKEY_CURRENT_USER,
            r"SOFTWARE\Epic Games\Unreal Engine\Builds",
        ) as builds:
            index = 0
            while index < MAX_REGISTRY_INSTALLATIONS:
                try:
                    association, directory, _ = winreg.EnumValue(builds, index)
                except OSError:
                    break
                index += 1
                if isinstance(directory, str):
                    installations.append((association, Path(directory)))
    except OSError:
        pass
    return installations


def engine_candidates(
    project: ProjectInfo,
    *,
    environment: Mapping[str, str] | None = None,
    installations: Sequence[tuple[str, Path]] | None = None,
) -> list[Path]:
    environment = os.environ if environment is None else environment
    installations = registry_installations() if installations is None else installations
    candidates: list[Path] = []

    configured = environment.get("UNREAL_MCP_ENGINE_ROOT")
    association = project.engine_association
    if association:
        for name, directory in installations:
            if name.casefold() == association.casefold():
                candidates.append(directory)
        for variable in ("ProgramW6432", "ProgramFiles"):
            program_files = environment.get(variable)
            if program_files:
                candidates.append(Path(program_files) / "Epic Games" / f"UE_{association}")
        if configured:
            candidates.append(Path(configured))
    else:
        if configured:
            candidates.append(Path(configured))
        candidates.extend(directory for _, directory in installations)

    unique: list[Path] = []
    identities: set[str] = set()
    for candidate in candidates:
        identity = os.path.normcase(os.path.abspath(os.fspath(candidate)))
        if identity not in identities:
            identities.add(identity)
            unique.append(candidate)
    return unique


def default_engine_root(environment: Mapping[str, str] | None = None) -> str:
    environment = os.environ if environment is None else environment
    return environment.get("UNREAL_MCP_ENGINE_ROOT", "").strip()


def resolve_engine_root(project: ProjectInfo, configured: Path | None = None) -> Path:
    if configured is not None:
        candidate = resolved(configured)
        try:
            validate_supported_engine_root(candidate)
        except (package_plugin.PackagingError, DeploymentError) as error:
            raise DeploymentError(str(error)) from error
        return candidate

    for candidate in engine_candidates(project):
        try:
            validate_supported_engine_root(candidate)
        except (package_plugin.PackagingError, DeploymentError):
            continue
        return resolved(candidate)
    association = project.engine_association or "<not specified>"
    raise DeploymentError(
        "could not locate the Unreal Engine installation for EngineAssociation "
        f"{association}; select the engine folder manually"
    )


def validate_supported_engine_root(engine_root: Path) -> Path:
    run_uat = package_plugin.validate_engine_root(engine_root, "Windows")
    version_file = resolved(engine_root) / "Engine" / "Build" / "Build.version"
    version = read_json_object(version_file, "Unreal Engine build version")
    major = version.get("MajorVersion")
    minor = version.get("MinorVersion")
    if type(major) is not int or type(minor) is not int:
        raise DeploymentError(f"Unreal Engine build version has invalid major/minor fields: {version_file}")
    if (major, minor) < (5, 8):
        raise DeploymentError(
            f"Unreal MCP requires Unreal Engine 5.8 or newer; selected Engine is {major}.{minor}"
        )
    return run_uat


def build_command(engine_root: Path, output: Path) -> list[str]:
    try:
        run_uat = validate_supported_engine_root(engine_root)
        output = package_plugin.validate_output(output, engine_root)
    except (package_plugin.PackagingError, DeploymentError) as error:
        raise DeploymentError(str(error)) from error
    return package_plugin.build_command(
        run_uat,
        output,
        "Win64",
        strict_includes=False,
        unversioned=False,
    )


def run_packaging(engine_root: Path, output: Path, log: Callable[[str], None]) -> None:
    command = build_command(engine_root, output)
    log(f"Building installed Win64 plugin with {engine_root}")
    creation_flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    try:
        process = subprocess.Popen(
            command,
            cwd=package_plugin.WORKSPACE_ROOT,
            env=os.environ.copy(),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            creationflags=creation_flags,
        )
    except OSError as error:
        raise DeploymentError(f"could not start Unreal AutomationTool: {error}") from error
    assert process.stdout is not None
    for line in process.stdout:
        log(line.rstrip())
    return_code = process.wait()
    if return_code != 0:
        raise DeploymentError(f"Unreal AutomationTool failed with exit code {return_code}")
    try:
        package_plugin.verify_package(output)
    except package_plugin.PackagingError as error:
        raise DeploymentError(str(error)) from error


def ignored_binary_items(directory: str, names: list[str]) -> set[str]:
    current = Path(directory)
    ignored: set[str] = set()
    for name in names:
        lowered = name.casefold()
        is_module_source_root = (
            current.name.casefold() == PLUGIN_NAME.casefold()
            and current.parent.name.casefold() == "source"
        )
        if is_module_source_root and lowered != f"{PLUGIN_NAME.casefold()}.build.cs":
            ignored.add(name)
        elif lowered.endswith(".dsym"):
            ignored.add(name)
        elif Path(name).suffix.casefold() in DEBUG_SUFFIXES:
            ignored.add(name)
    return ignored


def verify_binary_plugin(plugin_root: Path) -> None:
    descriptor = plugin_root / f"{PLUGIN_NAME}.uplugin"
    value = read_json_object(descriptor, "installed plugin descriptor")
    if value.get("Installed") is not True:
        raise DeploymentError("installed plugin descriptor is not marked Installed")
    module_rules = plugin_root / "Source" / PLUGIN_NAME / f"{PLUGIN_NAME}.Build.cs"
    if not module_rules.is_file():
        raise DeploymentError(f"binary deployment is missing Unreal Build Tool module rules: {module_rules}")
    try:
        rules_text = read_bounded_module_rules(module_rules)
    except (OSError, UnicodeError) as error:
        raise DeploymentError(f"binary module rules are unreadable: {module_rules}: {error}") from error
    if PRECOMPILED_MODULE_RULE.strip() not in rules_text:
        raise DeploymentError("binary module rules do not require the packaged precompiled module")
    implementation_source = next(
        (
            path
            for path in plugin_root.rglob("*")
            if path.is_file() and path.suffix.casefold() in IMPLEMENTATION_SOURCE_SUFFIXES
        ),
        None,
    )
    if implementation_source is not None:
        raise DeploymentError(
            f"binary deployment unexpectedly contains implementation source: {implementation_source}"
        )
    binary_root = plugin_root / "Binaries" / "Win64"
    if not binary_root.is_dir() or not any(
        path.is_file() and path.suffix.casefold() == ".dll" for path in binary_root.iterdir()
    ):
        raise DeploymentError(f"binary deployment contains no Win64 plugin DLL: {binary_root}")
    precompiled_root = plugin_root / "Intermediate" / "Build" / "Win64"
    if not precompiled_root.is_dir() or not any(
        path.is_file() and path.suffix.casefold() == ".lib"
        for path in precompiled_root.rglob("*")
    ):
        raise DeploymentError(
            f"binary deployment contains no Win64 precompiled import library: {precompiled_root}"
        )
    debug_artifact = next(
        (
            path
            for path in plugin_root.rglob("*")
            if path.is_file() and path.suffix.casefold() in DEBUG_SUFFIXES
        ),
        None,
    )
    if debug_artifact is not None:
        raise DeploymentError(f"binary deployment still contains a debug artifact: {debug_artifact}")


def configure_precompiled_module_rules(plugin_root: Path) -> None:
    module_rules = plugin_root / "Source" / PLUGIN_NAME / f"{PLUGIN_NAME}.Build.cs"
    try:
        rules_text = read_bounded_module_rules(module_rules)
    except (OSError, UnicodeError) as error:
        raise DeploymentError(f"packaged module rules are unreadable: {module_rules}: {error}") from error
    if PRECOMPILED_MODULE_RULE.strip() in rules_text:
        return
    if rules_text.count(MODULE_RULE_INSERTION_POINT) != 1:
        raise DeploymentError(
            "packaged module rules do not contain the expected single PCHUsage assignment"
        )
    configured = rules_text.replace(
        MODULE_RULE_INSERTION_POINT,
        PRECOMPILED_MODULE_RULE + MODULE_RULE_INSERTION_POINT,
        1,
    )
    try:
        module_rules.write_text(configured, encoding="utf-8", newline="\n")
    except OSError as error:
        raise DeploymentError(f"could not configure precompiled module rules: {error}") from error


def read_bounded_module_rules(module_rules: Path) -> str:
    with module_rules.open("rb") as stream:
        data = stream.read(MAX_MODULE_RULE_BYTES + 1)
    if len(data) > MAX_MODULE_RULE_BYTES:
        raise DeploymentError(f"module rules are larger than 64 KiB: {module_rules}")
    return data.decode("utf-8")


def plugin_destination(project: ProjectInfo) -> Path:
    plugins = project.folder / "Plugins"
    if plugins.exists() and is_reparse_point(plugins):
        raise DeploymentError(f"refusing to install through a reparse-point Plugins directory: {plugins}")
    destination = plugins / PLUGIN_NAME
    if destination.exists() and is_reparse_point(destination):
        raise DeploymentError(f"refusing to replace a reparse-point plugin directory: {destination}")
    resolved_parent = resolved(plugins)
    resolved_destination = resolved_parent / PLUGIN_NAME
    if not is_within(resolved_destination, project.folder):
        raise DeploymentError("plugin destination escapes the selected project folder")
    return destination


def install_binary_plugin(
    package_root: Path,
    project: ProjectInfo,
    *,
    replace_existing: bool,
) -> Path:
    destination = plugin_destination(project)
    if destination.exists() and not destination.is_dir():
        raise DeploymentError(f"plugin destination exists and is not a directory: {destination}")
    if destination.exists() and not replace_existing:
        raise DeploymentError(f"plugin is already installed: {destination}")

    destination.parent.mkdir(parents=True, exist_ok=True)
    nonce = uuid.uuid4().hex
    staging = destination.parent / f".{PLUGIN_NAME}.install-{nonce}"
    backup = destination.parent / f".{PLUGIN_NAME}.backup-{nonce}"
    try:
        shutil.copytree(package_root, staging, ignore=ignored_binary_items)
        configure_precompiled_module_rules(staging)
        verify_binary_plugin(staging)
        if destination.exists():
            destination.rename(backup)
        try:
            staging.rename(destination)
            verify_binary_plugin(destination)
        except BaseException:
            if destination.exists():
                shutil.rmtree(destination, ignore_errors=True)
            if backup.exists():
                backup.rename(destination)
            raise
        if backup.exists():
            shutil.rmtree(backup)
    except DeploymentError:
        raise
    except OSError as error:
        raise DeploymentError(f"could not install plugin into {destination}: {error}") from error
    finally:
        if staging.exists():
            shutil.rmtree(staging, ignore_errors=True)
    return destination


def lm_studio_json(project: ProjectInfo, python_executable: Path | None = None) -> str:
    executable = resolved(Path(sys.executable) if python_executable is None else python_executable)
    value = {
        "mcpServers": {
            "unreal-editor": {
                "command": str(executable),
                "args": [str(SERVER_ENTRY), str(project.descriptor)],
            }
        }
    }
    return json.dumps(value, indent=2, ensure_ascii=False)


def deploy(
    project: ProjectInfo,
    engine_root: Path,
    *,
    replace_existing: bool,
    log: Callable[[str], None],
) -> Path:
    with tempfile.TemporaryDirectory(prefix="unreal-mcp-package-") as temporary:
        package_root = Path(temporary) / PLUGIN_NAME
        run_packaging(engine_root, package_root, log)
        log("Removing implementation source and debug-symbol artifacts")
        destination = install_binary_plugin(
            package_root,
            project,
            replace_existing=replace_existing,
        )
    log(f"Installed binary plugin at {destination}")
    return destination


class DeploymentWindow:
    def __init__(self) -> None:
        import tkinter as tk
        from tkinter import ttk

        self.tk = tk
        self.ttk = ttk
        self.root = tk.Tk()
        self.root.title("Unreal MCP — Windows Deployment")
        self.root.geometry("820x650")
        self.root.minsize(680, 560)
        self.events: queue.Queue[tuple[str, object]] = queue.Queue()
        self.project_value = tk.StringVar()
        self.engine_value = tk.StringVar(value=default_engine_root())
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
        frame.rowconfigure(5, weight=1)
        frame.rowconfigure(8, weight=1)

        self.ttk.Label(frame, text="Unreal project folder").grid(row=0, column=0, sticky="w")
        self.project_entry = self.ttk.Entry(frame, textvariable=self.project_value)
        self.project_entry.grid(row=0, column=1, sticky="ew", padx=8)
        self.project_button = self.ttk.Button(frame, text="Browse…", command=self._browse_project)
        self.project_button.grid(row=0, column=2)

        self.ttk.Label(frame, text="Unreal Engine folder").grid(
            row=1, column=0, sticky="w", pady=(10, 0)
        )
        self.engine_entry = self.ttk.Entry(frame, textvariable=self.engine_value)
        self.engine_entry.grid(row=1, column=1, sticky="ew", padx=8, pady=(10, 0))
        self.engine_button = self.ttk.Button(frame, text="Browse…", command=self._browse_engine)
        self.engine_button.grid(row=1, column=2, pady=(10, 0))

        self.ttk.Label(
            frame,
            text="Close Unreal Editor before installing. The build uses the selected Engine and Visual Studio.",
            wraplength=760,
        ).grid(row=2, column=0, columnspan=3, sticky="w", pady=(12, 0))
        self.install_button = self.ttk.Button(frame, text="Build and install plugin", command=self._install)
        self.install_button.grid(row=3, column=0, columnspan=3, sticky="ew", pady=12)
        self.ttk.Label(frame, textvariable=self.status_value, wraplength=760).grid(
            row=4, column=0, columnspan=3, sticky="w"
        )

        self.log_text = scrolledtext.ScrolledText(frame, height=12, state="disabled", wrap="word")
        self.log_text.grid(row=5, column=0, columnspan=3, sticky="nsew", pady=(8, 12))

        self.ttk.Label(frame, text="LM Studio mcp.json entry").grid(
            row=6, column=0, columnspan=2, sticky="w"
        )
        self.copy_button = self.ttk.Button(
            frame, text="Copy JSON", command=self._copy_json, state="disabled"
        )
        self.copy_button.grid(row=6, column=2, sticky="e")
        self.json_text = scrolledtext.ScrolledText(frame, height=11, state="disabled", wrap="none")
        self.json_text.grid(row=8, column=0, columnspan=3, sticky="nsew", pady=(8, 0))

    def _browse_project(self) -> None:
        from tkinter import filedialog, messagebox

        selected = filedialog.askdirectory(title="Select the Unreal project folder", mustexist=True)
        if not selected:
            return
        self.project_value.set(selected)
        try:
            project = locate_project(Path(selected))
            self.status_value.set(f"Selected {project.descriptor.name}")
            try:
                configured_text = self.engine_value.get().strip()
                configured = Path(configured_text) if configured_text else None
                engine = resolve_engine_root(project, configured)
            except DeploymentError:
                self.engine_value.set("")
                try:
                    engine = resolve_engine_root(project)
                except DeploymentError:
                    return
            else:
                self.engine_value.set(str(engine))
                self.status_value.set(
                    f"Selected {project.descriptor.name}; detected Engine at {engine}"
                )
                return
            self.engine_value.set(str(engine))
            self.status_value.set(
                f"Selected {project.descriptor.name}; detected Engine at {engine}"
            )
        except DeploymentError as error:
            messagebox.showerror("Invalid Unreal project", str(error))

    def _browse_engine(self) -> None:
        from tkinter import filedialog, messagebox

        selected = filedialog.askdirectory(title="Select the Unreal Engine installation", mustexist=True)
        if not selected:
            return
        try:
            project = locate_project(Path(self.project_value.get()))
            engine = resolve_engine_root(project, Path(selected))
        except DeploymentError as error:
            messagebox.showerror("Invalid Unreal Engine folder", str(error))
            return
        self.engine_value.set(str(engine))
        self.status_value.set(f"Using Engine at {engine}")

    def _set_busy(self, busy: bool) -> None:
        self.busy = busy
        state = "disabled" if busy else "normal"
        for widget in (
            self.project_entry,
            self.project_button,
            self.engine_entry,
            self.engine_button,
            self.install_button,
        ):
            widget.configure(state=state)

    def _append_log(self, message: str) -> None:
        self.log_text.configure(state="normal")
        self.log_text.insert("end", message + "\n")
        self.log_text.see("end")
        self.log_text.configure(state="disabled")

    def _install(self) -> None:
        from tkinter import messagebox

        try:
            project = locate_project(Path(self.project_value.get()))
            configured = Path(self.engine_value.get()) if self.engine_value.get().strip() else None
            engine = resolve_engine_root(project, configured)
            destination = plugin_destination(project)
        except DeploymentError as error:
            messagebox.showerror("Cannot install Unreal MCP", str(error))
            return
        replace_existing = destination.exists()
        if replace_existing and not messagebox.askyesno(
            "Replace existing plugin?",
            f"{destination} already exists.\n\nReplace it with a newly built binary plugin?",
        ):
            return

        self._set_busy(True)
        self.copy_button.configure(state="disabled")
        self.status_value.set("Building Unreal MCP. This can take several minutes…")
        self.log_text.configure(state="normal")
        self.log_text.delete("1.0", "end")
        self.log_text.configure(state="disabled")

        def worker() -> None:
            try:
                destination_path = deploy(
                    project,
                    engine,
                    replace_existing=replace_existing,
                    log=lambda message: self.events.put(("log", message)),
                )
                result = (destination_path, lm_studio_json(project))
                self.events.put(("done", result))
            except Exception as error:
                self.events.put(("error", str(error)))

        threading.Thread(target=worker, name="UnrealMCPDeployment", daemon=True).start()

    def _poll_events(self) -> None:
        from tkinter import messagebox

        try:
            while True:
                kind, payload = self.events.get_nowait()
                if kind == "log":
                    self._append_log(str(payload))
                elif kind == "done":
                    destination, configuration = payload  # type: ignore[misc]
                    self.json_text.configure(state="normal")
                    self.json_text.delete("1.0", "end")
                    self.json_text.insert("1.0", configuration)
                    self.json_text.configure(state="disabled")
                    self.copy_button.configure(state="normal")
                    self._set_busy(False)
                    self.status_value.set(
                        "Installation complete. Open the project, then copy the JSON into LM Studio."
                    )
                    messagebox.showinfo(
                        "Unreal MCP installed",
                        f"Installed at:\n{destination}\n\nThe LM Studio JSON is ready to copy.",
                    )
                elif kind == "error":
                    self._set_busy(False)
                    self.status_value.set("Installation failed. Review the build log and try again.")
                    messagebox.showerror("Unreal MCP installation failed", str(payload))
        except queue.Empty:
            pass
        self.root.after(100, self._poll_events)

    def _copy_json(self) -> None:
        configuration = self.json_text.get("1.0", "end-1c")
        self.root.clipboard_clear()
        self.root.clipboard_append(configuration)
        self.root.update()
        self.status_value.set("LM Studio JSON copied to the clipboard.")

    def _close(self) -> None:
        if self.busy:
            from tkinter import messagebox

            messagebox.showwarning(
                "Deployment in progress",
                "Wait for the Unreal plugin build and installation to finish before closing this window.",
            )
            return
        self.root.destroy()

    def run(self) -> None:
        self.root.mainloop()


def main() -> int:
    if platform.system() != "Windows":
        print("This graphical deployment helper is supported only on Windows.", file=sys.stderr)
        return 1
    try:
        DeploymentWindow().run()
    except ImportError as error:
        print(f"Tkinter is required for the graphical deployment helper: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

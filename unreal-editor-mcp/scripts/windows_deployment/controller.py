"""Deployment-window validation, background work, and event handling."""

from __future__ import annotations

import queue
import threading
from pathlib import Path

from .configuration import format_mcp_settings_preview, mcp_server_definition
from .discovery import DeploymentError, locate_project, resolve_engine_root, windows_editor_lifecycle_executable
from .workflow import deploy, deployment_destinations, validate_install_method
from .view import DeploymentView


class DeploymentController(DeploymentView):
    def _browse_project(self) -> None:
        from tkinter import filedialog, messagebox
        selected = filedialog.askdirectory(title="Select the Unreal project folder", mustexist=True)
        if not selected:
            return
        self.project_value.set(selected)
        try:
            project = locate_project(Path(selected))
            self.status_value.set(f"Selected {project.descriptor.name}")
            configured_text = self.engine_value.get().strip()
            try:
                engine = resolve_engine_root(project, Path(configured_text) if configured_text else None)
            except DeploymentError:
                self.engine_value.set("")
                engine = resolve_engine_root(project)
            self.engine_value.set(str(engine))
            self.status_value.set(f"Selected {project.descriptor.name}; detected Engine at {engine}")
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

    def _install(self) -> None:
        from tkinter import messagebox
        try:
            project = locate_project(Path(self.project_value.get()))
            configured = Path(self.engine_value.get()) if self.engine_value.get().strip() else None
            engine = resolve_engine_root(project, configured)
            editor_lifecycle = windows_editor_lifecycle_executable(engine) if bool(self.lifecycle_value.get()) else None
            include_gas = bool(self.include_gas_value.get())
            include_commonui = bool(self.include_commonui_value.get())
            include_enhanced_input = bool(self.include_enhanced_input_value.get())
            install_method = validate_install_method(self.install_method_value.get())
            destinations = deployment_destinations(
                project,
                engine,
                install_method,
                include_gas=include_gas,
                include_commonui=include_commonui,
                include_enhanced_input=include_enhanced_input,
            )
        except DeploymentError as error:
            messagebox.showerror("Cannot install Unreal MCP", str(error))
            return
        existing = tuple(destination for destination in destinations if destination.exists())
        replace_existing = bool(existing)
        include_pdb = bool(self.include_pdb_value.get())
        writable = bool(self.writable_value.get())
        if replace_existing and not messagebox.askyesno("Replace existing plugins?", "These plugin installations already exist:\n\n" + "\n".join(str(destination) for destination in existing) + "\n\nReplace them with newly built binary plugins?"):
            return
        self._set_busy(True)
        self.copy_button.configure(state="disabled")
        self._clear_configuration()
        self.status_value.set("Building Unreal MCP. This can take several minutes…")
        self.log_text.configure(state="normal")
        self.log_text.delete("1.0", "end")
        self.log_text.configure(state="disabled")

        def worker() -> None:
            try:
                destination_paths = deploy(
                    project,
                    engine,
                    replace_existing=replace_existing,
                    include_pdb=include_pdb,
                    include_gas=include_gas,
                    include_commonui=include_commonui,
                    include_enhanced_input=include_enhanced_input,
                    install_method=install_method,
                    log=lambda message: self.events.put(("log", message)),
                )
                definition = mcp_server_definition(project, writable=writable, editor_lifecycle=editor_lifecycle)
                self.events.put(("done", (destination_paths, format_mcp_settings_preview(definition))))
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
                    destinations, configuration = payload  # type: ignore[misc]
                    self._set_configuration(configuration)
                    self.copy_button.configure(state="normal")
                    self._set_busy(False)
                    self.status_value.set("Installation complete. Open the project, then copy the required MCP settings entry.")
                    messagebox.showinfo("Unreal MCP installed", "Installed at:\n" + "\n".join(str(destination) for destination in destinations) + "\n\nThe MCP settings preview is ready to copy.")
                elif kind == "error":
                    self._set_busy(False)
                    self.status_value.set("Installation failed. Review the build log and try again.")
                    messagebox.showerror("Unreal MCP installation failed", str(payload))
        except queue.Empty:
            pass
        self.root.after(100, self._poll_events)

    def _close(self) -> None:
        if self.busy:
            from tkinter import messagebox
            messagebox.showwarning("Deployment in progress", "Wait for the Unreal plugin build and installation to finish before closing this window.")
            return
        self.root.destroy()


DeploymentWindow = DeploymentController

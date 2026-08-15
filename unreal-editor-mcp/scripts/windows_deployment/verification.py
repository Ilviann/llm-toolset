"""Binary-package filtering and layered deployment verification."""

from __future__ import annotations

import re
from collections.abc import Sequence
from pathlib import Path

from .discovery import DeploymentError, read_json_object
from .models import PLUGIN_NAME


MAX_MODULE_RULE_BYTES = 64 * 1024
MAX_PLUGIN_MODULES = 64
MAX_MODULE_NAME_CHARS = 128
DEBUG_SUFFIXES = frozenset({".pdb", ".ipdb", ".iobj", ".idb", ".ilk", ".obj", ".pch", ".map", ".debug"})
IMPLEMENTATION_SOURCE_SUFFIXES = frozenset({".c", ".cc", ".cpp", ".h", ".hh", ".hpp", ".inl"})
MODULE_RULE_INSERTION_POINT = "        PCHUsage ="
PRECOMPILED_MODULE_RULE = "        bUsePrecompiled = true;\n"
MODULE_NAME_PATTERN = re.compile(r"[A-Za-z][A-Za-z0-9_]*\Z")


def read_plugin_module_names(plugin_root: Path, plugin_name: str) -> tuple[str, ...]:
    descriptor = read_json_object(
        plugin_root / f"{plugin_name}.uplugin",
        "installed plugin descriptor",
    )
    modules = descriptor.get("Modules")
    if not isinstance(modules, list) or not modules:
        raise DeploymentError("installed plugin descriptor Modules must be a non-empty array")
    if len(modules) > MAX_PLUGIN_MODULES:
        raise DeploymentError(
            f"installed plugin descriptor contains more than {MAX_PLUGIN_MODULES} modules"
        )
    names: list[str] = []
    seen: set[str] = set()
    for module in modules:
        if not isinstance(module, dict):
            raise DeploymentError("installed plugin descriptor modules must be objects")
        name = module.get("Name")
        if (
            not isinstance(name, str)
            or len(name) > MAX_MODULE_NAME_CHARS
            or MODULE_NAME_PATTERN.fullmatch(name) is None
        ):
            raise DeploymentError(
                "installed plugin descriptor module Name must be a valid identifier "
                f"of at most {MAX_MODULE_NAME_CHARS} characters"
            )
        key = name.casefold()
        if key in seen:
            raise DeploymentError(
                f"installed plugin descriptor contains duplicate module Name: {name}"
            )
        seen.add(key)
        names.append(name)
    return tuple(names)


def ignored_binary_items(
    directory: str,
    names: list[str],
    *,
    include_pdb: bool = False,
    plugin_name: str = PLUGIN_NAME,
    module_names: Sequence[str] | None = None,
) -> set[str]:
    current = Path(directory)
    is_win64_binary_root = current.name.casefold() == "win64" and current.parent.name.casefold() == "binaries"
    dll_stems = {Path(name).stem.casefold() for name in names if Path(name).suffix.casefold() == ".dll"}
    modules = module_names if module_names is not None else (plugin_name,)
    current_module = next(
        (
            module_name
            for module_name in modules
            if current.name.casefold() == module_name.casefold()
            and current.parent.name.casefold() == "source"
        ),
        None,
    )
    ignored: set[str] = set()
    for name in names:
        lowered = name.casefold()
        suffix = Path(name).suffix.casefold()
        if current_module is not None and lowered != f"{current_module.casefold()}.build.cs":
            ignored.add(name)
        elif suffix in IMPLEMENTATION_SOURCE_SUFFIXES:
            ignored.add(name)
        elif lowered.endswith(".dsym"):
            ignored.add(name)
        elif suffix in DEBUG_SUFFIXES and not (
            include_pdb and suffix == ".pdb" and is_win64_binary_root and Path(name).stem.casefold() in dll_stems
        ):
            ignored.add(name)
    return ignored


def verify_descriptor(plugin_root: Path, plugin_name: str, enabled_by_default: bool | None) -> None:
    descriptor = plugin_root / f"{plugin_name}.uplugin"
    value = read_json_object(descriptor, "installed plugin descriptor")
    if value.get("Installed") is not True:
        raise DeploymentError("installed plugin descriptor is not marked Installed")
    if enabled_by_default is not None and value.get("EnabledByDefault") is not enabled_by_default:
        raise DeploymentError(
            f"installed {plugin_name} descriptor does not set EnabledByDefault to {str(enabled_by_default).lower()}"
        )


def read_bounded_module_rules(module_rules: Path) -> str:
    with module_rules.open("rb") as stream:
        data = stream.read(MAX_MODULE_RULE_BYTES + 1)
    if len(data) > MAX_MODULE_RULE_BYTES:
        raise DeploymentError(f"module rules are larger than 64 KiB: {module_rules}")
    return data.decode("utf-8")


def verify_module_rules(plugin_root: Path, module_names: Sequence[str]) -> None:
    for module_name in module_names:
        module_rules = plugin_root / "Source" / module_name / f"{module_name}.Build.cs"
        if not module_rules.is_file():
            raise DeploymentError(f"binary deployment is missing Unreal Build Tool module rules: {module_rules}")
        try:
            rules_text = read_bounded_module_rules(module_rules)
        except (OSError, UnicodeError) as error:
            raise DeploymentError(f"binary module rules are unreadable: {module_rules}: {error}") from error
        if PRECOMPILED_MODULE_RULE.strip() not in rules_text:
            raise DeploymentError(
                f"binary module rules do not require the packaged precompiled module: {module_rules}"
            )


def verify_binary_and_symbols(plugin_root: Path, include_pdb: bool) -> set[str]:
    binary_root = plugin_root / "Binaries" / "Win64"
    binary_dlls = [path for path in binary_root.iterdir() if path.is_file() and path.suffix.casefold() == ".dll"] if binary_root.is_dir() else []
    if not binary_dlls:
        raise DeploymentError(f"binary deployment contains no Win64 plugin DLL: {binary_root}")
    dll_stems = {path.stem.casefold() for path in binary_dlls}
    pdb_stems = {
        path.stem.casefold()
        for path in binary_root.iterdir()
        if path.is_file() and path.suffix.casefold() == ".pdb" and path.stem.casefold() in dll_stems
    }
    if include_pdb and pdb_stems != dll_stems:
        raise DeploymentError(
            "binary deployment is missing matching Win64 PDB crash symbols for: "
            + ", ".join(sorted(dll_stems - pdb_stems))
        )
    return dll_stems


def verify_precompiled_artifacts(plugin_root: Path) -> None:
    precompiled_root = plugin_root / "Intermediate" / "Build" / "Win64"
    if not precompiled_root.is_dir() or not any(
        path.is_file() and path.suffix.casefold() == ".lib" for path in precompiled_root.rglob("*")
    ):
        raise DeploymentError(f"binary deployment contains no Win64 precompiled import library: {precompiled_root}")


def verify_forbidden_files(plugin_root: Path, include_pdb: bool, dll_stems: set[str]) -> None:
    implementation_source = next((path for path in plugin_root.rglob("*") if path.is_file() and path.suffix.casefold() in IMPLEMENTATION_SOURCE_SUFFIXES), None)
    if implementation_source is not None:
        raise DeploymentError(f"binary deployment unexpectedly contains implementation source: {implementation_source}")
    binary_root = plugin_root / "Binaries" / "Win64"
    debug_artifact = next((
        path for path in plugin_root.rglob("*")
        if path.is_file() and path.suffix.casefold() in DEBUG_SUFFIXES
        and not (include_pdb and path.parent == binary_root and path.suffix.casefold() == ".pdb" and path.stem.casefold() in dll_stems)
    ), None)
    if debug_artifact is not None:
        raise DeploymentError(f"binary deployment still contains a debug artifact: {debug_artifact}")


def verify_binary_plugin(
    plugin_root: Path,
    *,
    include_pdb: bool = False,
    plugin_name: str = PLUGIN_NAME,
    enabled_by_default: bool | None = None,
) -> None:
    verify_descriptor(plugin_root, plugin_name, enabled_by_default)
    module_names = read_plugin_module_names(plugin_root, plugin_name)
    verify_module_rules(plugin_root, module_names)
    dll_stems = verify_binary_and_symbols(plugin_root, include_pdb)
    verify_precompiled_artifacts(plugin_root)
    verify_forbidden_files(plugin_root, include_pdb, dll_stems)


def configure_precompiled_module_rules(plugin_root: Path, plugin_name: str = PLUGIN_NAME) -> None:
    for module_name in read_plugin_module_names(plugin_root, plugin_name):
        module_rules = plugin_root / "Source" / module_name / f"{module_name}.Build.cs"
        try:
            rules_text = read_bounded_module_rules(module_rules)
        except (OSError, UnicodeError) as error:
            raise DeploymentError(f"packaged module rules are unreadable: {module_rules}: {error}") from error
        if PRECOMPILED_MODULE_RULE.strip() in rules_text:
            continue
        if rules_text.count(MODULE_RULE_INSERTION_POINT) != 1:
            raise DeploymentError(
                "packaged module rules do not contain the expected single PCHUsage assignment: "
                f"{module_rules}"
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

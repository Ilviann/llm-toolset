"""Startup configuration for the rooted filesystem server."""

from __future__ import annotations

import configparser
import os
import stat
import sys
from dataclasses import dataclass
from pathlib import Path


CONFIG_PATH = Path(".mcp") / "rooted-files-mcp.ini"
MAX_CONFIG_BYTES = 64 * 1024
MAX_HIDDEN_ALLOWLIST_ENTRIES = 64
MAX_HIDDEN_NAME_LENGTH = 255

BUILTIN_HIDDEN_ALLOWLIST = frozenset({".gitignore", ".env.template"})
PROTECTED_NAMES = frozenset({".mcp"})

_ALLOWED_SETTINGS = {
    "paths": {"root"},
    "permissions": {"read", "write"},
    "features": {"show_hidden", "hidden_allowlist", "line_access"},
}


class ConfigurationError(Exception):
    """A concise configuration error suitable for a startup diagnostic."""


@dataclass(frozen=True)
class Settings:
    """Resolved, immutable settings shared by the server and filesystem."""

    workspace: Path
    root: Path
    read: bool = True
    write: bool = True
    show_hidden: bool = True
    hidden_allowlist: frozenset[str] = BUILTIN_HIDDEN_ALLOWLIST
    line_access: bool = True
    case_sensitive: bool = True

    @classmethod
    def for_root(cls, root: str | os.PathLike[str]) -> "Settings":
        """Create backward-compatible defaults for a trusted explicit root."""

        resolved = _resolve_folder(root, "Root")
        return cls(
            workspace=resolved,
            root=resolved,
            case_sensitive=_filesystem_is_case_sensitive(resolved),
        )


@dataclass(frozen=True)
class IniSettings:
    """Validated values present in an INI file before precedence merging."""

    root: Path | None = None
    read: bool | None = None
    write: bool | None = None
    show_hidden: bool | None = None
    hidden_allowlist: tuple[str, ...] | None = None
    line_access: bool | None = None


def _filesystem_is_case_sensitive(path: Path) -> bool:
    """Detect native name matching without writing a probe file."""

    if os.name == "nt":
        return False
    for candidate in (path, *path.parents):
        name = candidate.name
        alternate = "".join(
            char.swapcase() if char.isalpha() else char for char in name
        )
        if not name or alternate == name:
            continue
        try:
            if alternate and candidate.with_name(alternate).exists():
                return not os.path.samefile(candidate, candidate.with_name(alternate))
            return True
        except OSError:
            continue
    return sys.platform != "darwin"


def _resolve_folder(path: str | os.PathLike[str], label: str) -> Path:
    try:
        resolved = Path(path).expanduser().resolve(strict=True)
    except (OSError, RuntimeError, ValueError):
        raise ConfigurationError(f"{label} is inaccessible") from None
    if not resolved.is_dir():
        raise ConfigurationError(f"{label} is not a folder")
    return resolved


def _read_config_file(workspace: Path) -> str | None:
    configured_path = workspace / CONFIG_PATH
    try:
        configured_path.lstat()
    except FileNotFoundError:
        return None
    except OSError:
        raise ConfigurationError("Configuration file is inaccessible") from None

    try:
        resolved_path = configured_path.resolve(strict=True)
        resolved_path.relative_to(workspace)
        file_stat = resolved_path.stat()
    except (OSError, RuntimeError, ValueError):
        raise ConfigurationError("Configuration file must remain inside workspace") from None

    if not stat.S_ISREG(file_stat.st_mode):
        raise ConfigurationError("Configuration path is not a regular file")
    if file_stat.st_size > MAX_CONFIG_BYTES:
        raise ConfigurationError("Configuration file exceeds 64 KiB limit")

    try:
        with resolved_path.open("rb") as config_file:
            data = config_file.read(MAX_CONFIG_BYTES + 1)
    except OSError:
        raise ConfigurationError("Configuration file is inaccessible") from None
    if len(data) > MAX_CONFIG_BYTES:
        raise ConfigurationError("Configuration file exceeds 64 KiB limit")
    if b"\x00" in data:
        raise ConfigurationError("Configuration file contains NUL bytes")
    try:
        return data.decode("utf-8-sig")
    except UnicodeDecodeError:
        raise ConfigurationError("Configuration file is not valid UTF-8") from None


def _validate_schema(parser: configparser.ConfigParser) -> None:
    if parser.defaults():
        key = next(iter(parser.defaults()))
        raise ConfigurationError(f"Unknown setting: [DEFAULT] {key}")

    for section in parser.sections():
        if section not in _ALLOWED_SETTINGS:
            raise ConfigurationError(f"Unknown configuration section: [{section}]")
        for key in parser.options(section):
            if key not in _ALLOWED_SETTINGS[section]:
                raise ConfigurationError(f"Unknown setting: [{section}] {key}")


def _optional_boolean(
    parser: configparser.ConfigParser, section: str, key: str
) -> bool | None:
    if not parser.has_option(section, key):
        return None
    try:
        return parser.getboolean(section, key)
    except ValueError:
        raise ConfigurationError(f"Invalid boolean: [{section}] {key}") from None


def _hidden_allowlist(parser: configparser.ConfigParser) -> tuple[str, ...] | None:
    if not parser.has_option("features", "hidden_allowlist"):
        return None
    raw_value = parser.get("features", "hidden_allowlist", raw=True)
    names = tuple(line.strip() for line in raw_value.splitlines() if line.strip())
    if not names:
        raise ConfigurationError("Hidden allowlist must contain at least one name")
    if len(names) > MAX_HIDDEN_ALLOWLIST_ENTRIES:
        raise ConfigurationError(
            f"Hidden allowlist exceeds {MAX_HIDDEN_ALLOWLIST_ENTRIES} entries"
        )

    seen: set[str] = set()
    for name in names:
        if len(name) > MAX_HIDDEN_NAME_LENGTH:
            raise ConfigurationError(
                f"Hidden allowlist name exceeds {MAX_HIDDEN_NAME_LENGTH} characters"
            )
        if name in {".", ".."} or "/" in name or "\\" in name or "\x00" in name:
            raise ConfigurationError("Hidden allowlist contains an invalid name")
        if name.casefold() in {protected.casefold() for protected in PROTECTED_NAMES}:
            raise ConfigurationError("Hidden allowlist contains a protected name")
        if name in seen:
            raise ConfigurationError("Hidden allowlist contains a duplicate name")
        seen.add(name)
    return names


def _effective_hidden_allowlist(
    configured: tuple[str, ...] | None, *, case_sensitive: bool
) -> frozenset[str]:
    additions = configured or ()
    comparison = (lambda value: value) if case_sensitive else str.casefold
    seen = {comparison(name) for name in BUILTIN_HIDDEN_ALLOWLIST}
    for name in additions:
        key = comparison(name)
        if key in seen:
            raise ConfigurationError("Hidden allowlist contains a duplicate name")
        seen.add(key)
    return frozenset((*BUILTIN_HIDDEN_ALLOWLIST, *additions))


def load_ini(workspace: Path) -> IniSettings | None:
    """Load and validate the fixed workspace configuration, if it exists."""

    text = _read_config_file(workspace)
    if text is None:
        return None

    parser = configparser.ConfigParser(interpolation=None, strict=True)
    try:
        parser.read_string(text, source=str(CONFIG_PATH))
    except configparser.Error as exc:
        raise ConfigurationError(f"Malformed configuration: {exc}") from None
    _validate_schema(parser)

    configured_root: Path | None = None
    if parser.has_option("paths", "root"):
        raw_root = parser.get("paths", "root", raw=True).strip()
        if not raw_root or "\x00" in raw_root:
            raise ConfigurationError("Configured root is invalid")
        candidate = Path(raw_root).expanduser()
        if not candidate.is_absolute():
            candidate = workspace / candidate
        try:
            configured_root = candidate.resolve(strict=True)
        except (OSError, RuntimeError, ValueError):
            raise ConfigurationError("Configured root is inaccessible") from None
        try:
            configured_root.relative_to(workspace)
        except ValueError:
            raise ConfigurationError("Configured root must remain inside workspace") from None
        if not configured_root.is_dir():
            raise ConfigurationError("Configured root is not a folder")

    return IniSettings(
        root=configured_root,
        read=_optional_boolean(parser, "permissions", "read"),
        write=_optional_boolean(parser, "permissions", "write"),
        show_hidden=_optional_boolean(parser, "features", "show_hidden"),
        hidden_allowlist=_hidden_allowlist(parser),
        line_access=_optional_boolean(parser, "features", "line_access"),
    )


def _merge(cli_value: bool | None, ini_value: bool | None, default: bool) -> bool:
    if cli_value is not None:
        return cli_value
    if ini_value is not None:
        return ini_value
    return default


def load_settings(
    *,
    root: str | os.PathLike[str] | None = None,
    workspace: str | os.PathLike[str] | None = None,
    read: bool | None = None,
    write: bool | None = None,
    show_hidden: bool | None = None,
    line_access: bool | None = None,
    cwd: str | os.PathLike[str] | None = None,
) -> Settings:
    """Resolve workspace, validate INI values, and apply CLI precedence."""

    workspace_source = workspace if workspace is not None else root
    if workspace_source is None:
        workspace_source = cwd if cwd is not None else Path.cwd()
    resolved_workspace = _resolve_folder(workspace_source, "Workspace")

    ini = load_ini(resolved_workspace)
    if root is not None:
        resolved_root = (
            resolved_workspace
            if workspace is None
            else _resolve_folder(root, "Root")
        )
    elif ini is not None and ini.root is not None:
        resolved_root = ini.root
    elif ini is None:
        raise ConfigurationError("No root provided and configuration file is missing")
    else:
        raise ConfigurationError("Configuration must define [paths] root")

    ini = ini or IniSettings()
    case_sensitive = _filesystem_is_case_sensitive(resolved_root)
    return Settings(
        workspace=resolved_workspace,
        root=resolved_root,
        read=_merge(read, ini.read, True),
        write=_merge(write, ini.write, True),
        show_hidden=_merge(show_hidden, ini.show_hidden, True),
        hidden_allowlist=_effective_hidden_allowlist(
            ini.hidden_allowlist, case_sensitive=case_sensitive
        ),
        line_access=_merge(line_access, ini.line_access, True),
        case_sensitive=case_sensitive,
    )

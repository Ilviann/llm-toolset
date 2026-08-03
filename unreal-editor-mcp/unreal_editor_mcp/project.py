"""Validated project layout and generated-state paths."""

from __future__ import annotations

import hashlib
from dataclasses import dataclass
from pathlib import Path

from .errors import ConfigurationError
from .platforms import DEFAULT_PLATFORM, PlatformAdapter


@dataclass(frozen=True)
class ProjectIdentity:
    name: str
    project_hash: str


@dataclass(frozen=True)
class ProjectLayout:
    root: Path
    descriptor: Path
    state_dir: Path
    token_file: Path
    discovery_file: Path

    @classmethod
    def resolve(cls, project: str | Path) -> "ProjectLayout":
        candidate = Path(project).expanduser()
        try:
            resolved = candidate.resolve(strict=True)
        except (OSError, RuntimeError):
            raise ConfigurationError("Project must be an existing .uproject file or its folder") from None
        if resolved.is_file():
            if resolved.suffix.lower() != ".uproject":
                raise ConfigurationError("Project file must use the .uproject extension")
            descriptor = resolved
            root = resolved.parent
        elif resolved.is_dir():
            descriptors = sorted(resolved.glob("*.uproject"))
            if len(descriptors) != 1:
                raise ConfigurationError("Project folder must contain exactly one .uproject file")
            root = resolved
            descriptor = descriptors[0]
        else:
            raise ConfigurationError("Project must be an existing .uproject file or its folder")
        state_dir = root / "Saved" / "UnrealMCP"
        return cls(
            root=root,
            descriptor=descriptor,
            state_dir=state_dir,
            token_file=state_dir / "bridge.token",
            discovery_file=state_dir / "discovery.json",
        )

    def project_hash(self, platform: PlatformAdapter = DEFAULT_PLATFORM) -> str:
        identity = platform.path_identity(str(self.descriptor))
        return hashlib.sha1(identity.encode("utf-8")).hexdigest()

    def identity(self, platform: PlatformAdapter = DEFAULT_PLATFORM) -> ProjectIdentity:
        return ProjectIdentity(name=self.descriptor.stem, project_hash=self.project_hash(platform))

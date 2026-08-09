"""Errors shared by Unreal-local repository tooling."""


class ToolingError(RuntimeError):
    """Raised when a bounded local-tooling contract is not satisfied."""

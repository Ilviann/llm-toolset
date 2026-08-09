"""Bounded cursor-page collection shared by headless scenarios."""

from __future__ import annotations

from typing import Callable


def collect_cursor_pages(
    first_page: dict[str, object],
    fetch: Callable[[str], dict[str, object]],
    *,
    maximum_pages: int = 64,
    records_key: str = "records",
) -> list[object]:
    if type(maximum_pages) is not int or not 1 <= maximum_pages <= 256:
        raise ValueError("maximum_pages must be an integer from 1 to 256")
    records: list[object] = []
    page = first_page
    seen: set[str] = set()
    for _ in range(maximum_pages):
        values = page.get(records_key, [])
        if not isinstance(values, list):
            raise AssertionError(f"cursor page {records_key} must be a list")
        records.extend(values)
        cursor = page.get("next_cursor")
        if cursor is None:
            return records
        if not isinstance(cursor, str) or not cursor or cursor in seen:
            raise AssertionError("cursor pagination returned an invalid or repeated cursor")
        seen.add(cursor)
        page = fetch(cursor)
    raise AssertionError(f"cursor pagination exceeded {maximum_pages} pages")

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from internal.tools.lint_docs import lint_repository


class DocumentationLinterTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name)

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def write(self, relative_path: str, text: str) -> None:
        path = self.root / relative_path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def codes(self) -> list[str]:
        return [diagnostic.code for diagnostic in lint_repository(self.root)]

    def write_feature(
        self,
        status: str,
        feature_id: str,
        depends_on: list[str] | None = None,
        released_in: str | None = None,
    ) -> None:
        dependencies = depends_on or []
        dependency_yaml = "[]" if not dependencies else "\n" + "\n".join(f"  - {item}" for item in dependencies)
        release_yaml = "null" if released_in is None else f'"{released_in}"'
        dependency_section = ""
        if dependencies:
            dependency_section = "\n**Depends on:**\n\n" + "\n".join(
                f"- `{item}`" for item in dependencies
            ) + "\n"
        self.write(
            f"docs/features/{status}/{feature_id}.md",
            "\n".join(
                [
                    "---",
                    f"feature_id: {feature_id}",
                    f"status: {status}",
                    f"depends_on: {dependency_yaml}",
                    f"released_in: {release_yaml}",
                    "---",
                    "",
                    f"# {feature_id}",
                    dependency_section,
                ]
            ),
        )

    def write_feature_indexes(self, groups: dict[str, list[str]]) -> None:
        root_links = "\n".join(f"- [{status}]({status}/index.md)" for status in groups)
        self.write("docs/features/index.md", f"# Features\n\n{root_links}\n")
        for status, feature_ids in groups.items():
            links = "\n".join(f"- [{feature_id}]({feature_id}.md)" for feature_id in feature_ids)
            self.write(f"docs/features/{status}/index.md", f"# {status.title()} features\n\n{links}\n")

    def test_valid_indexed_tree_features_issue_dependency_and_skills_exclusion(self) -> None:
        self.write(
            "docs/index.md",
            "# Documentation\n\n- [Guide](guide.md)\n- [Features](features/index.md)\n- [Issues](issues/index.md)\n",
        )
        self.write("docs/guide.md", "# Guide\n\n## Section\n\nText.\n")
        self.write_feature("completed", "base", released_in="1.0.0")
        self.write_feature("planned", "next", ["base"])
        self.write_feature("completed", "issue-fix", ["issue-1"], released_in="1.0.1")
        self.write_feature_indexes({"completed": ["base", "issue-fix"], "planned": ["next"]})
        self.write("docs/issues/index.md", "# Issues\n\n- [Issue 1](issue-1.md)\n")
        self.write("docs/issues/issue-1.md", "# Issue 1\n\n## Status\n\nResolved.\n")
        self.write("skills/template/docs/broken.md", "This excluded template is intentionally invalid.\n")

        self.assertEqual([], lint_repository(self.root))

    def test_reports_missing_link_and_anchor_but_ignores_fenced_examples(self) -> None:
        self.write("docs/index.md", "# Documentation\n\n- [Page](page.md)\n- [Other](other.md)\n")
        self.write(
            "docs/page.md",
            "# Page\n\n[Missing](missing.md)\n\n[Bad anchor](other.md#absent)\n\n```md\n[Example](ignored.md)\n```\n",
        )
        self.write("docs/other.md", "# Other\n\n## Present\n")

        self.assertEqual(["DOC017", "DOC018"], self.codes())

    def test_reports_missing_directory_index_and_parent_navigation(self) -> None:
        self.write("docs/index.md", "# Documentation\n")
        self.write("docs/topic/page.md", "# Page\n")

        codes = self.codes()
        self.assertIn("DOC013", codes)
        self.assertIn("DOC015", codes)

    def test_validates_anchor_in_linked_markdown_outside_docs_tree(self) -> None:
        self.write("docs/index.md", "# Documentation\n\n- [Page](page.md)\n")
        self.write("docs/page.md", "# Page\n\n[Roadmap section](../ROADMAP.md#native-test-backlog)\n")
        self.write("ROADMAP.md", "# Roadmap\n\n## Native test backlog\n")

        self.assertEqual([], lint_repository(self.root))

    def test_reports_duplicate_section_heading(self) -> None:
        self.write("docs/index.md", "# Documentation\n\n- [Page](page.md)\n")
        self.write("docs/page.md", "# Page\n\n## Same section\n\n## Same section\n")

        self.assertEqual(["DOC010"], self.codes())

    def test_reports_front_matter_identity_status_release_and_unknown_dependency(self) -> None:
        self.write("docs/index.md", "# Documentation\n\n- [Features](features/index.md)\n")
        self.write_feature("completed", "wrong-name", ["missing"])
        source = self.root / "docs/features/completed/wrong-name.md"
        destination = source.with_name("actual-name.md")
        source.rename(destination)
        self.write_feature_indexes({"completed": ["actual-name"]})

        codes = self.codes()
        self.assertIn("FEAT004", codes)
        self.assertIn("FEAT010", codes)
        self.assertIn("FEAT015", codes)

    def test_reports_body_dependency_mismatch(self) -> None:
        self.write("docs/index.md", "# Documentation\n\n- [Features](features/index.md)\n")
        self.write_feature("planned", "a", ["b"])
        self.write_feature("planned", "b")
        path = self.root / "docs/features/planned/a.md"
        path.write_text(path.read_text(encoding="utf-8").replace("`b`", "`a`"), encoding="utf-8")
        self.write_feature_indexes({"planned": ["a", "b"]})

        self.assertIn("FEAT013", self.codes())

    def test_reports_incomplete_dependency_and_cycle(self) -> None:
        self.write("docs/index.md", "# Documentation\n\n- [Features](features/index.md)\n")
        self.write_feature("planned", "a", ["b"])
        self.write_feature("planned", "b", ["a"])
        self.write_feature("completed", "done", ["a"], released_in="1.0.0")
        self.write_feature_indexes({"planned": ["a", "b"], "completed": ["done"]})

        codes = self.codes()
        self.assertIn("FEAT016", codes)
        self.assertIn("FEAT022", codes)


if __name__ == "__main__":
    unittest.main()

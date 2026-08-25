from __future__ import annotations

import unittest

from markdown_mcp.markdown import (
    MarkdownError,
    append_section,
    decode_fragment,
    delete_section,
    overwrite_section,
    parse_markdown,
    section_listing,
    select_markdown,
    set_front_matter,
)


class MarkdownParsingTests(unittest.TestCase):
    def test_atx_sections_include_descendants_and_stop_at_peers(self) -> None:
        text = (
            "# Intro\nopening\n"
            "## Child\nnested\n"
            "### Deep\ndeep\n"
            "## Next\nnext\n"
            "# Finish\nlast"
        )
        self.assertEqual(
            select_markdown(text, "intro"),
            "# Intro\nopening\n## Child\nnested\n### Deep\ndeep\n## Next\nnext\n",
        )
        self.assertEqual(
            select_markdown(text, "child"),
            "## Child\nnested\n### Deep\ndeep\n",
        )
        self.assertEqual(select_markdown(text, "finish"), "# Finish\nlast")

    def test_setext_crlf_closing_atx_and_source_are_exact(self) -> None:
        text = "Title\r\n=====\r\nbody\r\n## Child ##\r\nnested\r\nNext\r\n====\r\nend"
        self.assertEqual(
            select_markdown(text, "title"),
            "Title\r\n=====\r\nbody\r\n## Child ##\r\nnested\r\n",
        )
        self.assertEqual(select_markdown(text, "child"), "## Child ##\r\nnested\r\n")
        self.assertEqual(select_markdown(text, "next"), "Next\r\n====\r\nend")

    def test_anchors_cover_duplicates_unicode_markup_and_collisions(self) -> None:
        text = (
            "# Hello,   WORLD!\nfirst\n"
            "# Hello, WORLD!\nsecond\n"
            "# hello-world-1\ncollision\n"
            "# Привет, Мир!\nunicode\n"
            r"# A [link](https://example.test) &amp; \_value\_" "\nmarkup\n"
        )
        expected = [
            "hello-world",
            "hello-world-1",
            "hello-world-1-1",
            "привет-мир",
            "a-link--_value_",
        ]
        self.assertEqual(
            [item["anchor"] for item in section_listing(text, 6)["sections"]],
            expected,
        )
        self.assertEqual(
            select_markdown(text, decode_fragment("%D0%BF%D1%80%D0%B8%D0%B2%D0%B5%D1%82-%D0%BC%D0%B8%D1%80")),
            "# Привет, Мир!\nunicode\n",
        )

    def test_front_matter_aliases_require_exact_matching_delimiters(self) -> None:
        for delimiter in ("---", "==="):
            text = f"{delimiter}\r\ntitle: T\r\n{delimiter}\r\n# Body\r\n"
            expected = f"{delimiter}\r\ntitle: T\r\n{delimiter}\r\n"
            for selector in ("---", "==="):
                self.assertEqual(select_markdown(text, selector), expected)
            listing = section_listing(text)
            self.assertTrue(listing["has_front_matter"])
            self.assertEqual(listing["sections"][0]["anchor"], "body")

        malformed = "---\n# hidden\n===\n"
        self.assertEqual(parse_markdown(malformed).headings, ())
        with self.assertRaisesRegex(MarkdownError, "malformed"):
            select_markdown(malformed, "---")
        with self.assertRaisesRegex(MarkdownError, "not found"):
            select_markdown("# Body\n", "---")

    def test_fenced_indented_and_front_matter_heading_text_is_ignored(self) -> None:
        text = (
            "---\n# metadata\n---\n"
            "```md\n# fenced\n```\n"
            "~~~\nSetext\n===\n~~~\n"
            "    # indented\n"
            "# Real\nbody\n"
        )
        self.assertEqual(
            [item["anchor"] for item in section_listing(text, 6)["sections"]],
            ["real"],
        )

    def test_fragment_validation_is_strict_and_one_pass(self) -> None:
        self.assertEqual(decode_fragment("hello%2Dworld"), "hello-world")
        self.assertEqual(decode_fragment("hello%2520world"), "hello%20world")
        for fragment in ("", "bad%2", "bad%FF", "two words", "a/b", "a#b"):
            with self.subTest(fragment=fragment), self.assertRaises(MarkdownError):
                decode_fragment(fragment)

    def test_listing_filters_absolute_heading_level(self) -> None:
        listing = section_listing("# One\n### Three\n#### Four\n## Two\n", 3)
        self.assertEqual(
            [(item["level"], item["anchor"]) for item in listing["sections"]],
            [(1, "one"), (3, "three"), (2, "two")],
        )
        for value in (0, 7, True, "3"):
            with self.subTest(value=value), self.assertRaises(MarkdownError):
                section_listing("# One\n", value)  # type: ignore[arg-type]


class MarkdownMutationTests(unittest.TestCase):
    def test_overwrite_preserves_heading_and_deletes_descendants(self) -> None:
        text = "# Parent\nold\n## Child\nnested\n# Sibling\nkeep\n"
        self.assertEqual(
            overwrite_section(text, "parent", "new"),
            "# Parent\nnew\n# Sibling\nkeep\n",
        )
        self.assertEqual(
            overwrite_section(text, "parent", ""),
            "# Parent\n# Sibling\nkeep\n",
        )
        setext = "Parent\r\n======\r\nold\r\nNext\r\n====\r\n"
        self.assertEqual(
            overwrite_section(setext, "parent", "new"),
            "Parent\r\n======\r\nnew\r\nNext\r\n====\r\n",
        )

    def test_append_root_and_child_uses_exact_next_level(self) -> None:
        self.assertEqual(
            append_section("# One\nbody\n", None, "Two", "new"),
            "# One\nbody\n# Two\nnew\n",
        )
        text = "# Parent\nbody\n# Sibling\nkeep\n"
        self.assertEqual(
            append_section(text, "parent", "Child", "nested"),
            "# Parent\nbody\n## Child\nnested\n# Sibling\nkeep\n",
        )
        self.assertEqual(append_section("", None, "Empty", ""), "# Empty")
        with self.assertRaisesRegex(MarkdownError, "level-6"):
            append_section("###### Deep\n", "deep", "Nope", "")

    def test_append_rejects_invalid_titles_and_reserved_parent(self) -> None:
        for title in ("", " \t", "two\nlines", "bad\x00title"):
            with self.subTest(title=title), self.assertRaises(MarkdownError):
                append_section("# One\n", None, title, "")
        with self.assertRaisesRegex(MarkdownError, "heading fragment"):
            append_section("---\n---\n", "---", "Child", "")

    def test_delete_removes_complete_section_only(self) -> None:
        text = "before\n# Parent\nbody\n## Child\nnested\n# Sibling\nkeep"
        self.assertEqual(
            delete_section(text, "parent"), "before\n# Sibling\nkeep"
        )

    def test_front_matter_add_update_delete_and_noop_are_exact(self) -> None:
        self.assertEqual(
            set_front_matter("# Body\n", "title: T"),
            "---\ntitle: T\n---\n# Body\n",
        )
        self.assertEqual(set_front_matter("", "title: T"), "---\ntitle: T\n---")
        existing = "===\r\nold: yes\r\n===\r\n# Body\r\n"
        self.assertEqual(
            set_front_matter(existing, "new: yes"),
            "===\r\nnew: yes\r\n===\r\n# Body\r\n",
        )
        self.assertEqual(set_front_matter(existing, ""), "# Body\r\n")
        self.assertEqual(set_front_matter("# Body", ""), "# Body")

    def test_front_matter_rejects_injection_and_malformed_source(self) -> None:
        with self.assertRaisesRegex(MarkdownError, "delimiter"):
            set_front_matter("# Body\n", "one\n---\ntwo")
        with self.assertRaisesRegex(MarkdownError, "delimiter"):
            set_front_matter("===\nold\n===\n", "one\n===\ntwo")
        with self.assertRaisesRegex(MarkdownError, "malformed"):
            set_front_matter("---\nunclosed\n", "new")


if __name__ == "__main__":
    unittest.main()

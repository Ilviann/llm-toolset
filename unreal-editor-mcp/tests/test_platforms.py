import unittest

from unreal_editor_mcp.platforms import PlatformAdapter


class PlatformAdapterTests(unittest.TestCase):
    def test_windows_paths_are_case_and_separator_insensitive(self):
        adapter = PlatformAdapter("windows", process_probe=lambda pid: pid == 10)
        self.assertEqual(adapter.path_identity(r"C:\Game\Demo.uproject"), "c:/game/demo.uproject")
        self.assertTrue(adapter.process_is_alive(10))
        self.assertFalse(adapter.process_is_alive(11))

    def test_macos_and_linux_paths_remain_case_sensitive(self):
        for system in ("macos", "linux"):
            adapter = PlatformAdapter(system, process_probe=lambda _pid: True)
            self.assertEqual(adapter.path_identity("/Game/Demo.uproject"), "/Game/Demo.uproject")
            self.assertTrue(adapter.process_is_alive(1))

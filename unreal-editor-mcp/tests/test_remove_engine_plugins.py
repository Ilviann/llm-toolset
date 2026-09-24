import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from scripts.unreal_tooling.plugins import PLUGINS


@unittest.skipUnless(os.name == 'nt', 'Windows CMD helper')
class RemoveEnginePluginsTests(unittest.TestCase):
    def run_script(self, engine):
        script = Path(__file__).resolve().parents[1] / 'scripts' / 'remove_engine_plugins.cmd'
        return subprocess.run(
            ['cmd.exe', '/d', '/c', str(script)], env=dict(os.environ, UE58=str(engine)),
            input='\n', capture_output=True, text=True, timeout=10,
        )

    def test_removal_and_repeated_run(self):
        with tempfile.TemporaryDirectory(prefix='engine removal ') as temp:
            engine = Path(temp)
            build = engine / 'Engine' / 'Build'
            build.mkdir(parents=True)
            (build / 'Build.version').write_text('{}', encoding='utf-8')
            targets = []
            for location in ('Engine/Plugins', 'Engine/Plugins/Marketplace'):
                for name in PLUGINS:
                    target = engine / location / name
                    target.mkdir(parents=True)
                    (target / 'payload.bin').write_bytes(b'test')
                    targets.append(target)
            unrelated = engine / 'Engine' / 'Plugins' / 'Unrelated'
            unrelated.mkdir()
            for _ in range(2):
                result = self.run_script(engine)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertTrue(all(not target.exists() for target in targets))
                self.assertTrue(unrelated.exists())

    def test_missing_or_invalid_ue58(self):
        self.assertNotEqual(self.run_script('').returncode, 0)
        with tempfile.TemporaryDirectory() as temp:
            target = Path(temp) / 'Engine' / 'Plugins' / 'UnrealMCP'
            target.mkdir(parents=True)
            self.assertNotEqual(self.run_script(temp).returncode, 0)
            self.assertTrue(target.exists())

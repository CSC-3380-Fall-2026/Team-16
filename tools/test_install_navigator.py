import json
from pathlib import Path
import tempfile
import unittest

from install_navigator import install


class TestInstallNavigator(unittest.TestCase):
    def test_install_preserves_project_and_repeated_install(self):
        with tempfile.TemporaryDirectory() as folder:
            project = Path(folder) / "Test.uproject"
            project.write_text(json.dumps({"EngineAssociation": "5.8", "Modules": [{"Name": "Test"}]}))
            install(project)
            first = project.read_bytes()
            install(project)
            self.assertEqual(project.read_bytes(), first)
            self.assertEqual(json.loads(first)["Modules"], [{"Name": "Test"}])
            plugin = Path(folder) / "Plugins" / "PFTNavigator"
            self.assertTrue((plugin / "PFTNavigator.uplugin").is_file())
            self.assertTrue((plugin / "Content" / "Data" / "courses.json").is_file())

    def test_wrong_engine_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            project = Path(folder) / "Test.uproject"
            project.write_text('{"EngineAssociation":"5.7"}')
            with self.assertRaises(ValueError):
                install(project)
            self.assertFalse((Path(folder) / "Plugins").exists())

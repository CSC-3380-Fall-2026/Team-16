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

    def test_check_reports_drift_without_changing_files(self):
        with tempfile.TemporaryDirectory() as folder:
            project = Path(folder) / "Test.uproject"
            project.write_text('{"EngineAssociation":"5.8"}')
            install(project)
            install(project, check=True)
            plugin = project.parent / "Plugins" / "PFTNavigator"
            changed = plugin / "PFTNavigator.uplugin"
            missing = plugin / "Content" / "Data" / "courses.json"
            changed.write_bytes(b"local edits")
            missing.unlink()
            project.write_text('{"EngineAssociation":"5.8"}')
            with self.assertRaises(ValueError) as error:
                install(project, check=True)
            for problem in ("Changed", "PFTNavigator.uplugin", "Missing", "courses.json", "not enabled"):
                self.assertIn(problem, str(error.exception))
            self.assertEqual(changed.read_bytes(), b"local edits")
            self.assertFalse(missing.exists())
            self.assertEqual(json.loads(project.read_text()), {"EngineAssociation": "5.8"})

    def test_wrong_engine_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            project = Path(folder) / "Test.uproject"
            project.write_text('{"EngineAssociation":"5.7"}')
            with self.assertRaises(ValueError):
                install(project)
            self.assertFalse((Path(folder) / "Plugins").exists())

    def test_check_reports_extra_build_inputs(self):
        with tempfile.TemporaryDirectory() as folder:
            project = Path(folder) / "Test.uproject"
            project.write_text('{"EngineAssociation":"5.8"}')
            install(project)
            plugin = project.parent / "Plugins" / "PFTNavigator"
            extra = (plugin / "Source" / "OldRoom.cpp", plugin / "Content" / "Data" / "old_rooms.json")
            for path in extra:
                path.write_text("old branch files")
            with self.assertRaisesRegex(ValueError, "Unexpected .*OldRoom.cpp") as error:
                install(project, check=True)
            self.assertIn("old_rooms.json", str(error.exception))
            self.assertTrue(all(path.is_file() for path in extra))

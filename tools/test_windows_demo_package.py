"""Exercise Windows demo publication with a small stand-in for Unreal."""

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
REPORTS = {
    "CommonsWalk": "PFTFirstFloor.WalkToCommons",
    "Navigator": "PFTNavigator.Panel.AfterOnboarding",
    "GuidedWalk": "PFTFirstFloor.GuidedWalk",
    "C1007Walk": "PFTFirstFloor.WalkToC1007",
}


def stage_fixture(archive, case):
    """Stand in for UAT staging and the game's automation report export."""
    package, = Path(archive).glob("Windows.pending-*")
    (package / "PFTDigitalTwin.exe").write_text(case)
    (package / "Manifest_UFSFiles_Win64.txt").touch()
    redist = package / "Engine" / "Extras" / "Redist" / "en-us"
    redist.mkdir(parents=True)
    (redist / "vc_redist.x64.exe").touch()
    for suite, test_path in REPORTS.items():
        report = package / "PFTDigitalTwin" / "Saved" / "Automation" / suite / "index.json"
        report.parent.mkdir(parents=True)
        failed = case == "bad report" and suite == "Navigator"
        report.write_text(json.dumps({
            "succeeded": int(not failed),
            "succeededWithWarnings": 0,
            "failed": int(failed),
            "notRun": 0,
            "inProcess": 0,
            "tests": [{"fullTestPath": test_path, "state": "Fail" if failed else "Success"}],
        }))


@unittest.skipUnless(sys.platform == "win32", "Requires the Windows packaging launcher")
class TestWindowsDemoPackage(unittest.TestCase):
    def setUp(self):
        temp = self.enterContext(tempfile.TemporaryDirectory(prefix="PFT test "))
        self.root = Path(temp)
        self.tools = self.root / "tools"
        self.tools.mkdir()
        self.archive = self.root / "Review Builds"
        self.project = self.root / "PFTDigitalTwin.uproject"
        self.project.write_text('{"EngineAssociation":"5.8"}')
        self.engine = self.root / "Unreal Engine"
        batch = self.engine / "Engine" / "Build" / "BatchFiles"
        batch.mkdir(parents=True)
        (batch / "RunUAT.bat").write_text(
            '@echo off\n"%PFT_TEST_PYTHON%" "%PFT_TEST_SCRIPT%" "%PFT_TEST_ARCHIVE%" "%PFT_TEST_CASE%"\nexit /b %errorlevel%\n'
        )
        (self.tools / "Play PFT Demo.cmd").write_text(
            '@echo off\nrem /Game/Level/PFTFirstFloorComplete\nif not "%~1"=="verifyextended" exit /b 1\nexit /b 0\n'
        )
        for name in ("package_first_floor_demo.ps1", "install_navigator.py", "validate_data.py",
                     "measure_demo_frames.ps1", "check_demo_assets.ps1", "Read Me - PFT Demo.txt"):
            shutil.copyfile(ROOT / "tools" / name, self.tools / name)
        for directory in ("unreal", "data"):
            shutil.copytree(ROOT / directory, self.root / directory)
        self.map_name = "PFTFirstFloorReview"
        level = self.root / "Content" / "Level"
        level.mkdir(parents=True)
        (level / f"{self.map_name}.umap").touch()
        self.graph = self.root / "Candidate Routes.json"
        self.graph.write_text(json.dumps({
            "Map": f"/Game/Level/{self.map_name}",
            "Step": 60,
            "Destinations": {"PFT_COMMONS": [0, 0], "PFT_1200": [1, 0]},
            "Nodes": [[0, 0, 0.0, 1], [1, 0, 0.0, 4]],
        }))

    def run_package(self, case, *options):
        return subprocess.run(
            [shutil.which("pwsh") or "powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass",
             "-File", str(self.tools / "package_first_floor_demo.ps1"), "-Project", str(self.project),
             "-EngineRoot", str(self.engine), "-ArchiveDirectory", str(self.archive),
             "-MapName", self.map_name, "-VerifyExtended", *options],
            env={**os.environ, "PFT_TEST_PYTHON": sys.executable, "PFT_TEST_SCRIPT": str(Path(__file__).resolve()),
                 "PFT_TEST_ARCHIVE": str(self.archive), "PFT_TEST_CASE": case},
            capture_output=True, text=True, timeout=60, check=False,
        )

    def install(self, *options):
        subprocess.run([sys.executable, str(self.tools / "install_navigator.py"), str(self.project), *options],
                       check=True, capture_output=True, text=True, timeout=30)

    def test_candidate_publication_preserves_last_passed_archive(self):
        self.install()
        wrong_map = self.run_package("wrong map")
        self.assertNotEqual(wrong_map.returncode, 0)
        self.assertIn("The walking graph is for", wrong_map.stderr)
        self.assertFalse(self.archive.exists())

        graph_options = ("-WalkingGraph", str(self.graph))
        self.install("--walking-graph", str(self.graph))
        good = self.run_package("passed", *graph_options)
        self.assertEqual(good.returncode, 0, good.stdout + good.stderr)
        package = self.archive / "Windows"
        manifest = package / "PFTDemoBuild.json"
        original = manifest.read_bytes()
        record = json.loads(original.decode("utf-8-sig"))
        self.assertEqual(record["Map"], f"/Game/Level/{self.map_name}")
        self.assertEqual(record["Verification"], "Passed")
        self.assertEqual(set(record["TestReports"]), set(REPORTS))
        self.assertEqual(record["WalkingGraphSha256"], hashlib.sha256(self.graph.read_bytes()).hexdigest().upper())

        failed = self.run_package("bad report", *graph_options)
        self.assertNotEqual(failed.returncode, 0)
        self.assertIn("did not confirm a successful PFTNavigator.Panel.AfterOnboarding", failed.stderr)
        self.assertEqual(manifest.read_bytes(), original)
        self.assertEqual((package / "PFTDigitalTwin.exe").read_text(), "passed")


if __name__ == "__main__":
    stage_fixture(*sys.argv[1:])

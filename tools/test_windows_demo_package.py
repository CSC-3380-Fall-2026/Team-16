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
    "Reroute": "PFTFirstFloor.RefocusAfterReroute",
}


def build_makefiles(project):
    targets = project.parent / "Intermediate" / "Build" / "Win64" / "x64"
    return [targets / target / "Development" / "Makefile.bin"
            for target in (project.stem, f"{project.stem}Editor")]


def stage_fixture(archive, case, *arguments):
    """Stand in for UAT staging and the game's automation report export."""
    project, = (Path(arg.split("=", 1)[1]) for arg in arguments if arg.startswith("-project="))
    if any(path.exists() for path in build_makefiles(project)):
        sys.exit("Both Unreal source lists must be cleared before building.")
    package, = Path(archive).glob("Windows.pending-*")
    (package / "PFTDigitalTwin.exe").write_text(case)
    if case == "cook failure":
        sys.exit(7)
    (package / "Manifest_UFSFiles_Win64.txt").touch()
    redist = package / "Engine" / "Extras" / "Redist" / "en-us"
    redist.mkdir(parents=True)
    (redist / "vc_redist.x64.exe").touch()
    saved = package / "PFTDigitalTwin" / "Saved"
    logs = saved / "Logs"
    logs.mkdir(parents=True)
    (logs / "Navigator.log").write_text(case)
    for suite, test_path in REPORTS.items():
        if case == "basic" and suite in {"C1007Walk", "Reroute"}:
            continue
        report = saved / "Automation" / suite / "index.json"
        report.parent.mkdir(parents=True)
        failed = (case, suite) in {("bad report", "Navigator"), ("bad reroute", "Reroute")}
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
        for path in build_makefiles(self.project):
            path.parent.mkdir(parents=True)
            path.touch()
        self.compiled_object = build_makefiles(self.project)[0].with_name("PFTDigitalTwin.obj")
        self.compiled_object.touch()
        self.engine = self.root / "Unreal Engine"
        batch = self.engine / "Engine" / "Build" / "BatchFiles"
        batch.mkdir(parents=True)
        (batch / "RunUAT.bat").write_text(
            '@echo off\n'
            '"%PFT_TEST_PYTHON%" "%PFT_TEST_SCRIPT%" "%PFT_TEST_ARCHIVE%" "%PFT_TEST_CASE%" %*\n'
            'exit /b %errorlevel%\n'
        )
        (self.tools / "Play PFT Demo.cmd").write_text(
            '@echo off\nrem /Game/Level/PFTFirstFloorComplete\nif not "%~1"=="%PFT_TEST_MODE%" exit /b 1\nexit /b 0\n'
        )
        for name in ("package_first_floor_demo.ps1", "install_navigator.py", "validate_data.py",
                     "measure_demo_frames.ps1", "check_demo_assets.ps1", "Read Me - PFT Demo.txt",
                     "Play PFT Demo Offline.cmd"):
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

    def run_package(self, case, *options, mode="verifyextended"):
        verify_flag = {"verify": "-Verify", "verifyextended": "-VerifyExtended", "verifyoffline": "-VerifyOffline"}[mode]
        return subprocess.run(
            [shutil.which("pwsh") or "powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass",
             "-File", str(self.tools / "package_first_floor_demo.ps1"), "-Project", str(self.project),
             "-EngineRoot", str(self.engine), "-ArchiveDirectory", str(self.archive),
             "-MapName", self.map_name, verify_flag, *options],
            env={**os.environ, "PFT_TEST_PYTHON": sys.executable, "PFT_TEST_SCRIPT": str(Path(__file__).resolve()),
                 "PFT_TEST_ARCHIVE": str(self.archive), "PFT_TEST_CASE": case, "PFT_TEST_MODE": mode},
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
        self.assertTrue(self.compiled_object.is_file())
        package = self.archive / "Windows"
        manifest = package / "PFTDemoBuild.json"
        original = manifest.read_bytes()
        record = json.loads(original.decode("utf-8-sig"))
        self.assertEqual(record["Map"], f"/Game/Level/{self.map_name}")
        self.assertEqual(record["Verification"], "Passed")
        self.assertEqual(record["VerificationMode"], "verifyextended")
        self.assertEqual(set(record["TestReports"]), set(REPORTS))
        self.assertEqual(record["WalkingGraphSha256"], hashlib.sha256(self.graph.read_bytes()).hexdigest().upper())

        failed = self.run_package("bad report", *graph_options)
        self.assertNotEqual(failed.returncode, 0)
        self.assertIn("did not confirm a successful PFTNavigator.Panel.AfterOnboarding", failed.stderr)
        self.assertEqual(manifest.read_bytes(), original)
        self.assertEqual((package / "PFTDigitalTwin.exe").read_text(), "passed")
        self.assertEqual(list(self.archive.glob("Windows.pending-*")), [])
        diagnostics, = self.archive.glob("Windows.failed-*")
        failed_report = diagnostics / "Automation" / "Navigator" / "index.json"
        self.assertEqual(json.loads(failed_report.read_text())["failed"], 1)
        self.assertEqual((diagnostics / "Logs" / "Navigator.log").read_text(), "bad report")
        self.assertIn(diagnostics.name, failed.stdout)
        self.assertFalse((diagnostics / "PFTDigitalTwin.exe").exists())

        cook_failure = self.run_package("cook failure", *graph_options)
        self.assertNotEqual(cook_failure.returncode, 0)
        self.assertIn("Unreal packaging failed with exit code 7", cook_failure.stderr)
        self.assertEqual(list(self.archive.glob("Windows.pending-*")), [])
        self.assertEqual(list(self.archive.glob("Windows.failed-*")), [diagnostics])
        self.assertEqual(manifest.read_bytes(), original)

        retry = self.run_package("retried", *graph_options)
        self.assertEqual(retry.returncode, 0, retry.stdout + retry.stderr)
        self.assertEqual((package / "PFTDigitalTwin.exe").read_text(), "retried")
        self.assertEqual(list(self.archive.glob("Windows.pending-*")), [])
        previous, = self.archive.glob("Windows.previous-*")
        self.assertEqual((previous / "PFTDemoBuild.json").read_bytes(), original)

    def test_offline_verification_runs_and_records_extended_checks(self):
        self.install()
        wrong_map = self.run_package("wrong map", mode="verifyoffline")
        self.assertNotEqual(wrong_map.returncode, 0)
        self.assertIn("The walking graph is for", wrong_map.stderr)
        self.assertFalse(self.archive.exists())

        self.install("--walking-graph", str(self.graph))
        result = self.run_package("offline", "-WalkingGraph", str(self.graph), mode="verifyoffline")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        manifest = self.archive / "Windows" / "PFTDemoBuild.json"
        original = manifest.read_bytes()
        record = json.loads(original.decode("utf-8-sig"))
        self.assertEqual(record["Verification"], "Passed")
        self.assertEqual(record["VerificationMode"], "verifyoffline")
        self.assertEqual(set(record["TestReports"]), set(REPORTS))
        failed = self.run_package("bad reroute", "-WalkingGraph", str(self.graph), mode="verifyoffline")
        self.assertNotEqual(failed.returncode, 0)
        self.assertIn("did not confirm a successful PFTFirstFloor.RefocusAfterReroute", failed.stderr)
        self.assertEqual(manifest.read_bytes(), original)

    def test_basic_verification_records_three_checks(self):
        self.install("--walking-graph", str(self.graph))
        result = self.run_package("basic", "-WalkingGraph", str(self.graph), mode="verify")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        manifest = self.archive / "Windows" / "PFTDemoBuild.json"
        record = json.loads(manifest.read_text(encoding="utf-8-sig"))
        self.assertEqual(set(record["TestReports"]), {"CommonsWalk", "Navigator", "GuidedWalk"})

        offline_launcher = self.archive / "Windows" / "Play PFT Demo Offline.cmd"
        self.assertTrue(offline_launcher.is_file())
        command = [os.environ.get("ComSpec", "cmd.exe"), "/d", "/c", "call", str(offline_launcher)]
        launch = subprocess.run(command, cwd=self.root, env={**os.environ, "PFT_TEST_MODE": "offline"},
                                capture_output=True, text=True, timeout=10, check=False)
        self.assertEqual(launch.returncode, 0, launch.stdout + launch.stderr)


if __name__ == "__main__":
    stage_fixture(*sys.argv[1:])

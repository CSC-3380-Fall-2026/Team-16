"""Check the Windows frame-time report used by the demo launcher."""

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


@unittest.skipUnless(sys.platform == "win32", "Requires Windows PowerShell")
class TestDemoFrames(unittest.TestCase):
    def test_guided_report_identifies_the_scenario_and_counts_slow_frames(self):
        with tempfile.TemporaryDirectory(prefix="PFT frame test ") as directory:
            saved = Path(directory)
            logs = saved / "Logs"
            logs.mkdir()
            capture = saved / "Profiling" / "CSV" / "Guided.csv"
            capture.parent.mkdir(parents=True)
            capture.write_text(
                "FrameTime,GameThreadTime,GPUTime\n0,0,0\n15,2,10\n20,3,14\n51,6,43\n105,8,88\ninvalid,,\n"
            )
            log = logs / "GuidedProfile-45FPS.log"
            log.write_text(f"Capture Ended. Writing CSV to file: {capture}\n")
            script = Path(__file__).with_name("measure_demo_frames.ps1")
            scenario = "Guided Commons walk with map (45 FPS cap)"

            result = subprocess.run(
                [shutil.which("pwsh") or "powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass",
                 "-File", str(script), "-Log", str(log), "-Scenario", scenario],
                capture_output=True, text=True, timeout=20, check=False,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            report = log.with_name(f"{log.stem}-FramePacing.txt").read_text(encoding="utf-8-sig")
            self.assertIn(f"{scenario} frame pacing", report)
            self.assertIn("Frames: 4", report)
            self.assertIn("Frames over 50 ms: 2", report)
            self.assertIn("Frames over 100 ms: 1", report)
            self.assertIn("Game thread 95th percentile 8.00 ms, maximum 8.00 ms", report)
            self.assertIn("GPU 95th percentile 88.00 ms, maximum 88.00 ms", report)

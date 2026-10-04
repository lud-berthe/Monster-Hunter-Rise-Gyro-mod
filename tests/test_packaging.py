"""Distribution regressions use temporary fixtures, never the real game."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("mhr_package", ROOT / "tools/package.py")
packaging = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packaging)


def write(root, relative, text="fixture"):
    file = root / relative
    file.parent.mkdir(parents=True, exist_ok=True)
    file.write_text(text, encoding="utf-8")
    return file


class Distribution(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="mhr-tooling-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_package_omits_user_files_and_does_not_need_reframework(self):
        stage = self.root / "stage"
        for path in ["reframework/plugins/MHRGyro.dll",
                     "reframework/autorun/mhr_gyro.lua", "reframework/autorun/mhr_gyro/profile.lua",
                     "licenses/MHRGyro.txt", "licenses/REFramework.txt", "licenses/Lua.txt",
                     "licenses/GyroLib.txt"]:
            write(stage, path)
        for path in ["gyrolib.dll", "dinput8.dll", "reframework/data/mhr_gyro_recommended.ini", "reframework/data/gyrolib.ini", "reframework/data/girolib.ini", "reframework/data/report.json",
                     "reframework/autorun/unrelated.lua", "licenses/GyroLib/sdl-changes/unused.patch", "debug.pdb"]:
            write(stage, path, "private")
        output = self.root / "mod.zip"
        packaging.package(stage, output)
        with zipfile.ZipFile(output) as archive:
            self.assertEqual(set(archive.namelist()), {"reframework/plugins/MHRGyro.dll",
                "reframework/autorun/mhr_gyro.lua", "reframework/MHRGyro/LICENSES.txt"})
            self.assertNotIn("dinput8.dll", archive.namelist())
            self.assertNotIn("gyrolib.dll", archive.namelist())
            self.assertFalse(any(Path(name).suffix.lower() == ".ini" for name in archive.namelist()))
            self.assertIn("reframework/plugins/MHRGyro.dll", archive.namelist())
            self.assertFalse(any(b"private" in archive.read(name) for name in archive.namelist()))
        before = output.read_bytes()
        with self.assertRaises(FileExistsError):
            packaging.package(stage, output)
        self.assertEqual(output.read_bytes(), before)
        (stage / "licenses/Lua.txt").unlink()
        with self.assertRaisesRegex(ValueError, "Missing package input"):
            packaging.package(stage, self.root / "missing.zip")

    def test_bundle_requires_all_referenced_modules(self):
        scripts = self.root / "scripts"
        write(scripts, "mhr_gyro.lua", 'require("mhr_gyro/missing")')
        write(scripts, "mhr_gyro/profile.lua", "return {}")
        with self.assertRaisesRegex(ValueError, "Missing Lua dependency"):
            packaging.bundle_scripts(scripts)

    @unittest.skipUnless(os.environ.get("MHR_TEST_RUNNER"), "Requires the built Lua fixture")
    def test_real_bundle_runs_without_module_files(self):
        entry = self.root / "mhr_gyro.lua"
        entry.write_bytes(packaging.bundle_scripts(ROOT / "reframework/autorun"))
        result = subprocess.run([os.environ["MHR_TEST_RUNNER"], str(ROOT), str(entry)],
            capture_output=True, text=True, timeout=30, cwd=self.root)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    @unittest.skipUnless(os.name == "nt", "Windows installer")
    def test_installer_first_install_update_conflict_and_dll_mismatch(self):
        game, build = self.root / "Game with spaces", self.root / "build"
        sdk, dependency = self.root / "sdk", self.root / "external REFramework"
        write(game, "MonsterHunterRise.exe")
        personal = write(game, "reframework/data/gyrolib.ini", "personal settings\n")
        write(build, "Release/MHRGyro.dll", "plugin v1")
        write(build, "Release/gyrolib.dll", "runtime v1")
        write(sdk, "bin/gyrolib.dll", "runtime v1")
        for name in ["THIRD_PARTY.md", "licenses/notice.txt", "sdl-changes/changes.txt"]:
            write(sdk, "notices/" + name)
        write(dependency, "LICENSE")
        write(dependency, "dependencies/lua/src/lua.h", "/******************************************************************************\nLua license\n*/")
        write(build, "mhr-stage-Release.txt", "\n".join(map(str, [build / "Release/MHRGyro.dll",
              sdk / "bin/gyrolib.dll", sdk / "notices", dependency])) + "\n")
        shell = shutil.which("pwsh") or shutil.which("powershell")
        self.assertIsNotNone(shell)

        def run(*flags, success=True):
            result = subprocess.run([shell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                str(ROOT / "tools/install-update.ps1"), "-GameDirectory", str(game),
                "-BuildDirectory", str(build), *flags], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
            self.assertEqual(personal.read_text(), "personal settings\n")
            return result

        run("-CheckOnly")
        self.assertFalse((game / "gyrolib.dll").exists())
        run()
        self.assertTrue((game / "reframework/MHRGyro/install-manifest.json").exists())
        self.assertTrue((game / "reframework/MHRGyro/licenses/Lua.txt").exists())
        write(build, "Release/MHRGyro.dll", "plugin v2")
        run("-ScriptsOnly", success=False)
        run()
        self.assertEqual((game / "reframework/plugins/MHRGyro.dll").read_text(), "plugin v2")
        write(game, "reframework/autorun/mhr_gyro.lua", "user edit")
        write(build, "Release/MHRGyro.dll", "plugin v3")
        run(success=False)
        self.assertEqual((game / "reframework/plugins/MHRGyro.dll").read_text(), "plugin v2")
        self.assertEqual((game / "reframework/autorun/mhr_gyro.lua").read_text(), "user edit")


if __name__ == "__main__":
    unittest.main()

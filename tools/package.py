"""Package a staged build; personal settings and reports are never included."""
import argparse
import hashlib
from pathlib import Path
import zipfile

def package(stage, output, readme):
    entries = {
        "reframework/plugins/MHRGyro.dll": (stage / "reframework/plugins/MHRGyro.dll").read_bytes(),
        "README_MHRGyro.txt": readme.read_bytes(),
    }
    scripts = stage / "reframework/autorun"
    licenses = stage / "licenses"
    for needed in [scripts / "mhr_gyro.lua", licenses / "MHRGyro.txt",
                   licenses / "REFramework.txt", licenses / "Lua.txt", licenses / "GyroLib/THIRD_PARTY.md"]:
        if not needed.is_file():
            raise ValueError(f"Missing package input: {needed}")
    entries["reframework/autorun/mhr_gyro.lua"] = (scripts / "mhr_gyro.lua").read_bytes()
    modules = list((scripts / "mhr_gyro").glob("*.lua"))
    if not modules:
        raise ValueError("Missing Lua modules")
    for file in modules:
        entries[file.relative_to(stage).as_posix()] = file.read_bytes()
    for file in licenses.rglob("*"):
        if file.is_file():
            entries["reframework/MHRGyro/licenses/" + file.relative_to(licenses).as_posix()] = file.read_bytes()
    if any(Path(path).suffix.lower() in {".ini", ".json", ".log", ".pdb"} for path in entries):
        raise ValueError("Unexpected private or debug file in package")
    if any(Path(path).suffix.lower() == ".dll" and path != "reframework/plugins/MHRGyro.dll" for path in entries):
        raise ValueError("External runtime in mod-only package")
    # x preserves any archive already produced under this name.
    with zipfile.ZipFile(output, "x", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path, content in sorted(entries.items()):
            archive.writestr(path, content)
    with zipfile.ZipFile(output) as archive:
        if archive.testzip() is not None or set(archive.namelist()) != set(entries):
            raise ValueError("Archive verification failed")
        for path, content in entries.items():
            if archive.read(path) != content:
                raise ValueError(f"Archive content mismatch: {path}")
    return len(entries)


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("name", help="New ZIP filename inside dist")
    parser.add_argument("--stage", type=Path, default=root / "dist/MHRGyro")
    args = parser.parse_args()
    if Path(args.name).name != args.name or not args.name.endswith(".zip"):
        parser.error("Provide a ZIP filename without a directory")
    output = root / "dist" / args.name
    output.parent.mkdir(exist_ok=True)
    count = package(args.stage, output, root / "docs/INSTALL.txt")
    print(f"{output}\n{count} entries, {output.stat().st_size / 1024**2:.2f} MiB")
    print("SHA256:", hashlib.sha256(output.read_bytes()).hexdigest())


if __name__ == "__main__":
    main()

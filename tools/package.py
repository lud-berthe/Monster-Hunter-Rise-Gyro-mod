"""Package a staged build; personal settings and reports are never included."""
import argparse
import hashlib
import re
from pathlib import Path
import zipfile

def bundle_scripts(scripts):
    modules = sorted((scripts / "mhr_gyro").glob("*.lua"))
    if not modules:
        raise ValueError("Missing Lua modules")
    chunks = [b"-- Generated from the MHRGyro Lua sources; edit the repository sources.\n"]
    names = set()
    sources = []
    for file in modules:
        if not re.fullmatch(r"[a-z_][a-z0-9_]*", file.stem):
            raise ValueError(f"Invalid module name: {file.name}")
        name = "mhr_gyro/" + file.stem
        names.add(name)
        source = file.read_text(encoding="utf-8-sig")
        sources.append(source)
        chunks.append((f'package.preload["{name}"] = function(...)\n' + source + "\nend\n").encode("utf-8"))
    entry = (scripts / "mhr_gyro.lua").read_text(encoding="utf-8-sig")
    sources.append(entry)
    for source in sources:
        for name in re.findall(r"require\s*\(\s*[\"'](mhr_gyro/[^\"']+)[\"']", source):
            if name not in names:
                raise ValueError(f"Missing Lua dependency: {name}")
    chunks.append(entry.encode("utf-8"))
    return b"\n".join(chunks)


def package(stage, output):
    scripts = stage / "reframework/autorun"
    licenses = stage / "licenses"
    license_names = ["MHRGyro", "REFramework", "Lua", "GyroLib"]
    for needed in [scripts / "mhr_gyro.lua", *(licenses / (name + ".txt") for name in license_names)]:
        if not needed.is_file():
            raise ValueError(f"Missing package input: {needed}")
    notices = "MHRGyro - bundled code and header licenses\n"
    for name in license_names:
        notices += "\n" + "=" * 72 + "\n" + name + "\n" + "=" * 72 + "\n\n"
        notices += (licenses / (name + ".txt")).read_text(encoding="utf-8-sig").strip() + "\n"
    entries = {
        "reframework/plugins/MHRGyro.dll": (stage / "reframework/plugins/MHRGyro.dll").read_bytes(),
        "reframework/autorun/mhr_gyro.lua": bundle_scripts(scripts),
        "reframework/MHRGyro/LICENSES.txt": notices.encode("utf-8"),
    }
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
    count = package(args.stage, output)
    print(f"{output}\n{count} entries, {output.stat().st_size / 1024**2:.2f} MiB")
    print("SHA256:", hashlib.sha256(output.read_bytes()).hexdigest())


if __name__ == "__main__":
    main()

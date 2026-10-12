import argparse
import hashlib
import json
import re
import subprocess
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--runtime", required=True, choices=("win-x64", "linux-x64"))
    parser.add_argument("--archive", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    package = args.directory.resolve()
    if package == root or not package.is_relative_to(root) or not package.is_dir():
        parser.error("Package must be an existing output directory beneath BGS.")
    if not re.fullmatch(r"[0-9a-f]{40}", args.commit):
        parser.error("Commit must be a full lowercase Git SHA.")
    native = {"win-x64": "radicle.dll", "linux-x64": "libradicle.so"}[args.runtime]
    if not (package / native).is_file():
        raise RuntimeError(f"Package is missing the native Radicle library {native}.")
    version = ET.parse(root / "app/BehaviourStudio.csproj").findtext(".//Version")
    executable = package / ("BehaviourGraphStudio.exe" if args.runtime == "win-x64" else "BehaviourGraphStudio")
    report = subprocess.check_output([str(executable), "--version"], text=True).strip()
    if f"Studio {version} " not in report or f"build {args.commit[:12]}" not in report or args.runtime not in report:
        raise RuntimeError(f"Published build identity mismatch: {report}")
    for symbol in package.rglob("*.pdb"):
        symbol.unlink()
    (package / "build-info.json").write_text(json.dumps({
        "version": version, "commit": args.commit, "runtime": args.runtime,
    }, indent=2) + "\n", encoding="utf-8")
    if args.archive:
        archive = args.archive.resolve()
        if archive.is_relative_to(package):
            parser.error("Archive must be outside the package directory.")
        with zipfile.ZipFile(archive, "x", compression=zipfile.ZIP_DEFLATED) as output:
            for file in sorted(package.rglob("*")):
                if file.is_file():
                    output.write(file, file.relative_to(package))
        with archive.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        archive.with_suffix(archive.suffix + ".sha256").write_text(
            f"{digest}  {archive.name}\n", encoding="utf-8")
    print(report)


if __name__ == "__main__":
    main()

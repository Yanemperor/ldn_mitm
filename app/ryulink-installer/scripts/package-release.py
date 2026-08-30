#!/usr/bin/env python3
"""Build a RyuLink Installer release with the current RyuLink payload."""

from __future__ import annotations
import argparse
import hashlib
import shutil
import subprocess
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = ROOT.parents[1]
PAYLOAD = ROOT / "payload"
PROJECT = ROOT / "src" / "RyuLink.Installer" / "RyuLink.Installer.csproj"
FILES = ("switch/RyuLink/RyuLink.nro", "switch/.overlays/ldnmitm_config.ovl", "atmosphere/contents/4200000000000010/exefs.nsp", "atmosphere/contents/4200000000000010/flags/boot2.flag", "config/ldn_mitm/relay.cfg")
RIDS = {"windows-x64": "win-x64", "macos-arm64": "osx-arm64", "macos-x64": "osx-x64"}

def prepare_payload() -> None:
    for relative in FILES:
        source = REPOSITORY / "out" / "sd" / relative
        if not source.is_file(): raise RuntimeError(f"missing approved artifact: {source}")
        destination = PAYLOAD / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)

def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", required=True)
    parser.add_argument("--target", choices=RIDS, action="append")
    parser.add_argument("--output", type=Path, default=ROOT / "release")
    args = parser.parse_args()
    prepare_payload()
    args.output.mkdir(parents=True, exist_ok=True)
    archives: list[Path] = []
    for target in args.target or list(RIDS):
        with tempfile.TemporaryDirectory() as temp:
            publish = Path(temp) / "publish"
            subprocess.run(["dotnet", "publish", str(PROJECT), "-c", "Release", "-r", RIDS[target], "--self-contained", "true", "-p:PublishSingleFile=true", "-p:IncludeAllContentForSelfExtract=true", "-p:DebugType=None", "-p:DebugSymbols=false", "-o", str(publish)], check=True)
            executable = publish / ("RyuLink.Installer.exe" if target == "windows-x64" else "RyuLink.Installer")
            if not executable.is_file(): raise RuntimeError(f"missing publish executable: {executable}")
            archive = args.output / f"RyuLink-Installer-{args.version}-{target}.zip"
            with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as output:
                for path in sorted(publish.rglob("*")):
                    if path.is_file(): output.write(path, path.relative_to(publish))
            archives.append(archive)
    (args.output / "SHA256SUMS.txt").write_text("".join(f"{sha256(path)}  {path.name}\n" for path in archives), encoding="utf-8")
    print("\n".join(map(str, archives)))

if __name__ == "__main__": main()

#!/usr/bin/env python3
"""Cuts the .ipa for a signed variant and records what is in it.

    python3 cut-ipa.py --variant steam

The archive is the repo's own convention (release/publish.sh): a zip of `Payload/<name>.app`,
made with `zip -qry -X`. Every member is then checked against the files on disk, because an
.ipa is what a user installs and a truncated or renamed member would only show up on the
device. The result is written beside the build as `ipa.json`, and `SHA256SUMS` in that
directory is refreshed (`shasum -a 256 -c` must pass there).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import zipfile
from pathlib import Path

# parents: [0] steam-app, [1] installation, [2] LinPad, [3] work, [4] the workspace root.
ROOT = Path(__file__).resolve().parents[4]

VARIANTS = {
    "desktop": {"dir": "steam-app-desktop", "app": "iSH ARM64.app", "ipa_prefix": "LinPad"},
    "steam": {"dir": "steam-app-steam", "app": "LinPad Steam.app", "ipa_prefix": "LinPad-Steam"},
}

CHUNK = 1 << 20


def digest(path: Path) -> str:
    sha = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(CHUNK):
            sha.update(chunk)
    return sha.hexdigest()


def plist_value(app: Path, key: str) -> str:
    out = subprocess.run(["/usr/libexec/PlistBuddy", "-c", f"Print :{key}", str(app / "Info.plist")],
                         capture_output=True, check=True)
    return out.stdout.decode().strip()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--variant", choices=sorted(VARIANTS), required=True)
    args = parser.parse_args()

    variant = VARIANTS[args.variant]
    build_dir = ROOT / "work/performance" / variant["dir"]
    app = build_dir / "Payload" / variant["app"]
    if not app.is_dir():
        sys.exit(f"no staged app at {app}; run build-local.sh {args.variant} first")
    if not (app / "embedded.mobileprovision").exists():
        sys.exit(f"{app} is not signed; run sign-variant.py --variant {args.variant} first")

    version = plist_value(app, "CFBundleShortVersionString")
    build = plist_value(app, "CFBundleVersion")
    bundle_id = plist_value(app, "CFBundleIdentifier")
    on_disk = {f"Payload/{variant['app']}/{p.relative_to(app)}": p
               for p in app.rglob("*") if p.is_file()}

    ipa = build_dir / f"{variant['ipa_prefix']}-{version}-build{build}.ipa"
    ipa.unlink(missing_ok=True)
    print(f"cutting {ipa.name} ({len(on_disk)} files, "
          f"{sum(p.stat().st_size for p in on_disk.values()) / 1e6:.0f} MB uncompressed)")
    subprocess.run(["zip", "-qry", "-X", str(ipa), "Payload"], cwd=build_dir, check=True)

    with zipfile.ZipFile(ipa) as archive:
        if archive.testzip() is not None:
            sys.exit(f"{ipa} is corrupt at {archive.testzip()}")
        names = [info.filename for info in archive.infolist() if not info.is_dir()]
        if len(names) != len(set(names)):
            sys.exit(f"{ipa} has duplicate members")
        if set(names) != set(on_disk):
            missing = sorted(set(on_disk) - set(names))[:5]
            extra = sorted(set(names) - set(on_disk))[:5]
            sys.exit(f"{ipa} contents differ from Payload (missing {missing}, extra {extra})")
        for name in names:
            with archive.open(name) as stream, on_disk[name].open("rb") as file:
                sha = hashlib.sha256()
                while chunk := stream.read(CHUNK):
                    sha.update(chunk)
                if sha.hexdigest() != digest(on_disk[name]):
                    sys.exit(f"{ipa}: {name} does not match the file on disk")
                file.seek(0)

    ipa_sha = digest(ipa)
    record = {
        "ipa": ipa.name,
        "bytes": ipa.stat().st_size,
        "sha256": ipa_sha,
        "variant": args.variant,
        "bundle_id": bundle_id,
        "display_name": plist_value(app, "CFBundleDisplayName"),
        "linpad_variant": plist_value(app, "LinPadVariant"),
        "version": version,
        "build": build,
        "members": len(on_disk),
        "app": str(app),
    }
    (build_dir / "ipa.json").write_text(json.dumps(record, indent=2, sort_keys=True) + "\n")

    sums = build_dir / "SHA256SUMS"
    entries = [f"{ipa_sha}  {ipa.name}"]
    dsym = ROOT / "work/ios-build/Release-iphoneos" / f"{variant['app'][:-4]}.app.dSYM"
    print(json.dumps(record, indent=2, sort_keys=True))
    sums.write_text("\n".join(entries) + "\n")
    print(f"\nwrote {sums} and {build_dir / 'ipa.json'}")
    if dsym.exists():
        print(f"dSYM for symbols: {dsym}")


if __name__ == "__main__":
    main()

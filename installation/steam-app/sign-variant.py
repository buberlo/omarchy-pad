#!/usr/bin/env python3
"""Signs and verifies one staged LinPad variant.

Both apps are built from the same target and staged as Payload/<name>.app by
build-local.sh; this script gives each its identity:

  desktop  com.konradkern.linpad        app group group.com.konradkern.linpad (as before)
  steam    com.konradkern.linpad.steam  app group group.com.konradkern.linpad (the same one)

The shared app group is the whole point of the game app: both apps read their Linux system
from the same container, so Steam, FEX, Mesa, games and saves are not installed twice.

  python3 sign-variant.py --variant steam  --app work/performance/steam-app-steam/Payload/"LinPad Steam.app"
  python3 sign-variant.py --variant desktop --app work/performance/steam-app-desktop/Payload/"iSH ARM64.app"

The provisioning profile must already cover the bundle id and the app group; Xcode-managed
signing produces it from work/signing (see installation/steam-app/README.md). Everything the
profile has to contain is asserted here, so a wrong or expired profile stops the build
instead of producing an app that cannot reach its own Linux system.
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import plistlib
import subprocess
import sys
from pathlib import Path

# parents: [0] steam-app, [1] installation, [2] LinPad, [3] work, [4] the workspace root.
ROOT = Path(__file__).resolve().parents[4]          # the workspace root (holds work/)
TEAM = "K5TW9AU245"
IDENTITY = "0B49D6C4CDB9B6FA1D15530AF43AE577037F93A5"   # Apple Development: Konrad Kern
DEVICE_UDID = "00008112-000924EE14D9A01E"              # the iPad Air 13" M2
APP_GROUP = "group.com.konradkern.linpad"              # shared by both apps, on purpose

VARIANTS = {
    "desktop": {
        "bundle_id": "com.konradkern.linpad",
        "display_name": "Linux for iPad",
        "app_group": APP_GROUP,
        # Stable copies of the Xcode-managed profiles (work/signing/profiles), refreshed by
        # building work/signing/LinPadSigning.xcodeproj; see installation/steam-app/README.md.
        "profile": ROOT / "work/signing/profiles/com.konradkern.linpad.mobileprovision",
    },
    "steam": {
        "bundle_id": "com.konradkern.linpad.steam",
        "display_name": "LinPad Steam",
        "app_group": APP_GROUP,
        "profile": ROOT / "work/signing/profiles/com.konradkern.linpad.steam.mobileprovision",
    },
}

MAGIC = {bytes.fromhex(x) for x in
         ["cffaedfe", "cefaedfe", "feedfacf", "feedface", "cafebabe", "bebafeca", "cafebabf", "bfbafeca"]}


def run(cmd: list[str], **kw) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, check=True, **kw)


def decode_profile(path: Path) -> dict:
    return plistlib.loads(run(["security", "cms", "-D", "-i", str(path)],
                              capture_output=True).stdout)


def expected_entitlements(bundle_id: str, app_group: str) -> dict:
    """What the signature must carry. get-task-allow is what lets StikDebug grant the JIT."""
    return {
        "application-identifier": f"{TEAM}.{bundle_id}",
        "com.apple.developer.team-identifier": TEAM,
        "keychain-access-groups": [f"{TEAM}.{bundle_id}"],
        "get-task-allow": True,
        "com.apple.security.application-groups": [app_group],
        "com.apple.developer.user-fonts": ["app-usage"],
        "com.apple.developer.kernel.increased-memory-limit": True,
        "com.apple.developer.kernel.extended-virtual-addressing": True,
    }


def check_profile(profile: Path, ent: dict, allow_missing_device: bool) -> dict:
    if not profile.exists():
        sys.exit(f"no provisioning profile at {profile}\n"
                 f"Build the matching target in work/signing first (installation/steam-app/README.md).")
    p = decode_profile(profile)
    certs = [hashlib.sha1(c).hexdigest() for c in p["DeveloperCertificates"]]
    if IDENTITY.lower() not in certs:
        sys.exit(f"{profile} is not signed for the development identity {IDENTITY}")
    devices = p.get("ProvisionedDevices", [])
    if DEVICE_UDID not in devices:
        message = f"{profile} does not list this iPad ({DEVICE_UDID})"
        if allow_missing_device:
            print(f"warning: {message}", file=sys.stderr)
        else:
            sys.exit(message)
    if p["ExpirationDate"] <= datetime.datetime.now():
        sys.exit(f"{profile} expired on {p['ExpirationDate']}")
    pent = p["Entitlements"]
    for key, value in ent.items():
        if key not in pent:
            sys.exit(f"{profile} has no entitlement {key}; enable it for this App ID")
        if isinstance(value, list):
            have = pent[key]

            def covered(wanted: str) -> bool:
                if wanted in have:
                    return True
                # A wildcard group ("K5TW9AU245.*") covers every id of this team, which is how
                # profiles express keychain access groups.
                return any(entry.endswith(".*") and wanted.startswith(entry[:-1]) for entry in have)

            missing = [v for v in value if not covered(v)]
            if missing:
                sys.exit(f"{profile} does not allow {key} = {missing}")
        elif pent[key] != value:
            sys.exit(f"{profile} has {key} = {pent[key]!r}, expected {value!r}")
    return p


def sign(app: Path, profile: Path, ent: dict, facts: dict, identity: str) -> None:
    info_path = app / "Info.plist"
    info = plistlib.loads(info_path.read_bytes())
    info["CFBundleIdentifier"] = ent["application-identifier"].split(".", 1)[1]
    info["CFBundleName"] = facts["display_name"]
    info["CFBundleDisplayName"] = facts["display_name"]
    if "LinPadVariant" in info and info["LinPadVariant"] != facts["variant"]:
        sys.exit(f"{app} was built as {info['LinPadVariant']!r}, not {facts['variant']!r}")
    info_path.write_bytes(plistlib.dumps(info))

    import shutil
    shutil.copy2(profile, app / "embedded.mobileprovision")

    ent_path = ROOT / f"work/{facts['variant']}-entitlements.plist"
    ent_path.write_bytes(plistlib.dumps(ent))

    for path in sorted(app.rglob("*")):
        if not path.is_file() or path.name == info["CFBundleExecutable"]:
            continue
        if path.read_bytes()[:4] in MAGIC:
            run(["codesign", "--force", "--sign", identity, "--timestamp=none", str(path)])
    run(["codesign", "--force", "--sign", identity, "--timestamp=none",
         "--entitlements", str(ent_path), "--generate-entitlement-der", str(app)])
    run(["codesign", "--verify", "--deep", "--strict", str(app)])


def verify_signature(app: Path, ent: dict) -> None:
    """Read back what the signature says, not what we meant to put there."""
    out = subprocess.run(["codesign", "-d", "--entitlements", "-", "--xml", str(app)],
                         capture_output=True)
    if out.returncode != 0:
        sys.exit(f"cannot read the signature of {app}: {out.stderr.decode()}")
    signed = plistlib.loads(out.stdout)
    for key, value in ent.items():
        if signed.get(key) != value:
            sys.exit(f"signature has {key} = {signed.get(key)!r}, expected {value!r}")
    described = subprocess.run(["codesign", "-dv", str(app)], capture_output=True)
    text = (described.stdout + described.stderr).decode()
    facts = {}
    for line in text.splitlines():
        if "=" in line:
            key, _, value = line.partition("=")
            facts[key.strip()] = value.strip()
    if facts.get("Identifier") != ent["application-identifier"].split(".", 1)[1]:
        sys.exit(f"signature Identifier is {facts.get('Identifier')!r}")
    if facts.get("TeamIdentifier") != TEAM:
        sys.exit(f"signature TeamIdentifier is {facts.get('TeamIdentifier')!r}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--variant", choices=sorted(VARIANTS), required=True)
    parser.add_argument("--app", required=True, type=Path, help="the staged Payload/<name>.app")
    parser.add_argument("--profile", type=Path, help="defaults to the variant's Xcode-managed profile")
    parser.add_argument("--identity", default=IDENTITY)
    parser.add_argument("--allow-missing-device", action="store_true",
                        help="warn instead of failing when the profile has no registered device")
    parser.add_argument("--manifest", type=Path, help="write a JSON record of what was signed")
    args = parser.parse_args()

    app = args.app.resolve()
    if not app.is_dir():
        sys.exit(f"no app bundle at {app}")
    variant = VARIANTS[args.variant]
    profile = (args.profile or variant["profile"]).resolve()
    ent = expected_entitlements(variant["bundle_id"], variant["app_group"])
    check_profile(profile, ent, args.allow_missing_device)

    facts = {"variant": args.variant, "display_name": variant["display_name"]}
    sign(app, profile, ent, facts, args.identity)
    verify_signature(app, ent)

    print(f"SIGNED AND VERIFIED {app}")
    print(f"  bundle id : {variant['bundle_id']}")
    print(f"  app group : {variant['app_group']} (shared with the other LinPad app)")
    print(f"  profile   : {profile.name}, expires {decode_profile(profile)['ExpirationDate']}")
    print(f"  variant   : {plistlib.loads((app / 'Info.plist').read_bytes()).get('LinPadVariant')}")

    if args.manifest:
        variant["profile"].parent.mkdir(parents=True, exist_ok=True)
        record = {
            "variant": args.variant,
            "bundle_id": variant["bundle_id"],
            "display_name": variant["display_name"],
            "app_group": variant["app_group"],
            "app": str(app),
            "profile": str(profile),
            "entitlements": ent,
            "signed_at": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
        }
        args.manifest.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n")


if __name__ == "__main__":
    main()

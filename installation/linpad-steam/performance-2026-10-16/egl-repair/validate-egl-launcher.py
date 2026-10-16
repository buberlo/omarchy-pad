#!/usr/bin/env python3
"""Host-only integration through the real Steam launch-game and a mock FEX.

No game, translated Linux binary, display, device, or credentials are accessed.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--launcher", required=True, type=Path)
parser.add_argument("--wrapper", type=Path, default=Path(__file__).with_name("fex-hollow-egl"))
args = parser.parse_args()
launcher, wrapper = args.launcher.resolve(), args.wrapper.resolve()
keys = [
    "LD_LIBRARY_PATH", "LD_PRELOAD", "LD_AUDIT", "VK_ICD_FILENAMES", "VK_DRIVER_FILES",
    "VK_LOADER_LAYERS_DISABLE", "VK_INSTANCE_LAYERS", "GALLIUM_DRIVER", "LP_NUM_THREADS",
    "LIBGL_ALWAYS_SOFTWARE", "MESA_LOADER_DRIVER_OVERRIDE", "LIBGL_DRIVERS_PATH",
    "__EGL_VENDOR_LIBRARY_FILENAMES", "SDL_VIDEODRIVER", "EGL_PLATFORM", "DISPLAY",
    "WAYLAND_DISPLAY", "XDG_RUNTIME_DIR", "FEX_ROOTFS", "FEX_DISABLETELEMETRY",
]
checks = 0

def check(condition, description):
    global checks
    assert condition, description
    checks += 1

with tempfile.TemporaryDirectory(prefix="linpad egl launch ") as td:
    root = Path(td).resolve()
    game_dir = root / "game with spaces"
    game_dir.mkdir()
    (game_dir / "hollow knight.x86_64").touch()
    base, mesa, egl = (root / name for name in ("base", "mesa26", "egl25"))
    loader = base / "usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2"
    icd = mesa / "usr/share/vulkan/icd.d/virtio_icd.json"
    vendor = egl / "usr/share/glvnd/egl_vendor.d/50_mesa.json"
    for path in (loader, icd, vendor):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.touch()
    mock = root / "mock patched FEX"
    output = root / "observed.json"
    mock.write_text(
        f"#!{sys.executable}\nimport json, os, sys\n"
        f"keys={keys!r}\n"
        "with open(os.environ['LINPAD_TEST_OUTPUT'], 'w') as out:\n"
        " json.dump({'argv':sys.argv[1:],'cwd':os.getcwd(),'env':{k:os.environ.get(k) for k in keys}},out)\n"
    )
    mock.chmod(0o755)
    base_env = {k: v for k, v in os.environ.items() if not k.startswith(("LINPAD_", "LD_", "VK_", "FEX_"))}
    base_env.update({
        "LINPAD_FEX_BINARY": str(wrapper), "LINPAD_EGL_FEX_BINARY": str(mock),
        "LINPAD_FEX_RUNTIME": str(base), "LINPAD_EGL_MESA_PREFIX": str(mesa),
        "LINPAD_EGL_PREFIX": str(egl), "LINPAD_FEX_ICD": str(icd),
        "LINPAD_FEX_DISPLAY_MODE": "wayland", "LINPAD_TEST_OUTPUT": str(output),
        "LD_PRELOAD": "/poison/arm64/overlay.so", "LD_LIBRARY_PATH": "/poison/arm64",
        "VK_DRIVER_FILES": "/poison/other-icd.json", "VK_INSTANCE_LAYERS": "poison",
        "GALLIUM_DRIVER": "poison", "LIBGL_ALWAYS_SOFTWARE": "poison",
        "LP_NUM_THREADS": "99", "DISPLAY": ":99", "SDL_VIDEODRIVER": "x11",
        "__EGL_VENDOR_LIBRARY_FILENAMES": "/poison/vendor.json",
    })
    game_argv = ["./hollow knight.x86_64", "-force-wayland", "-force-vulkan", "argument with spaces", "-timestamps"]
    for mode in ("swrast", "zink"):
        env = dict(base_env, LINPAD_EGL_MODE=mode)
        result = subprocess.run(["/bin/sh", str(launcher), *game_argv], cwd=game_dir, env=env, text=True, capture_output=True)
        check(result.returncode == 0, result.stderr)
        observed = json.loads(output.read_text())
        values = observed["env"]
        check(observed["argv"] == game_argv, f"{mode}: argv preservation")
        check(observed["cwd"] == str(game_dir), f"{mode}: cwd preservation")
        expected_paths = [egl / "usr/lib/x86_64-linux-gnu", mesa / "usr/lib/x86_64-linux-gnu", base / "usr/lib/x86_64-linux-gnu", base / "usr/lib/x86_64-linux-gnu/pulseaudio"]
        check(values["LD_LIBRARY_PATH"] == ":".join(map(str, expected_paths)), f"{mode}: ordered x86 runtime")
        check(values["VK_ICD_FILENAMES"] == str(icd), f"{mode}: explicit Venus26 ICD")
        check(values["__EGL_VENDOR_LIBRARY_FILENAMES"] == str(vendor), f"{mode}: explicit EGL25 vendor")
        for key in ("LD_PRELOAD", "LD_AUDIT", "VK_DRIVER_FILES", "VK_INSTANCE_LAYERS", "DISPLAY", "LP_NUM_THREADS"):
            check(values[key] is None, f"{mode}: {key} cleared")
        check(values["VK_LOADER_LAYERS_DISABLE"] == "*", f"{mode}: layers disabled")
        check(values["MESA_LOADER_DRIVER_OVERRIDE"] == mode, f"{mode}: EGL driver")
        check(values["LIBGL_ALWAYS_SOFTWARE"] == ("1" if mode == "swrast" else None), f"{mode}: software selector")
        check(values["GALLIUM_DRIVER"] == ("llvmpipe" if mode == "swrast" else None), f"{mode}: Gallium selector")
        check(values["SDL_VIDEODRIVER"] == values["EGL_PLATFORM"] == "wayland", f"{mode}: Wayland selectors")
        check(values["WAYLAND_DISPLAY"] == "wayland-0-gpu" and values["XDG_RUNTIME_DIR"] == "/tmp/xdg-runtime", f"{mode}: GPU Wayland socket")
        check(values["FEX_ROOTFS"] == str(base) and values["FEX_DISABLETELEMETRY"] == "1", f"{mode}: FEX root and telemetry")
        check(f"mode={mode}" in result.stderr and "LD_PRELOAD=unset" in result.stderr, f"{mode}: diagnostic logging")
    for existing in (False, True):
        argv = game_argv if existing else game_argv[:-1]
        env = dict(base_env, LINPAD_EGL_ADD_TIMESTAMPS="1", LINPAD_FEX_ROOTFS="")
        result = subprocess.run(["/bin/sh", str(launcher), *argv], cwd=game_dir, env=env, text=True, capture_output=True)
        check(result.returncode == 0, result.stderr)
        observed = json.loads(output.read_text())
        check(observed["argv"] == game_argv, "timestamps appended once, preserves existing")
        check(observed["env"]["FEX_ROOTFS"] == "", "empty FEX_ROOTFS override preserved")
        check(observed["env"]["MESA_LOADER_DRIVER_OVERRIDE"] == "swrast", "default diagnostic mode is swrast")
    result = subprocess.run(["/bin/sh", str(wrapper), *game_argv], env=dict(base_env, LINPAD_EGL_MODE="invalid"), capture_output=True)
    check(result.returncode == 64, "invalid EGL mode rejected")
    result = subprocess.run(["/bin/sh", str(wrapper)], env=base_env, capture_output=True)
    check(result.returncode == 64, "empty argv rejected")
print(f"EGL_LAUNCHER_HOST_MOCK_PASS checks={checks}; no Linux/device execution")

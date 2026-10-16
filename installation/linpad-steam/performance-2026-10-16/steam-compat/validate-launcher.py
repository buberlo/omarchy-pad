#!/usr/bin/env python3
"""Host-only wrapper check using mock FEX and X11 programs; no device access."""
from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile

base = Path(__file__).resolve().parent
tool = base / 'linpad-fex-native'
checks = []
for name in ('run', 'launch-game'):
    subprocess.run(['/bin/sh', '-n', str(tool / name)], check=True)
    checks.append(f'PASS shell syntax: {name}')

with tempfile.TemporaryDirectory(prefix='linpad-launcher-check-') as tmp:
    root = Path(tmp).resolve()
    runtime = root / 'x86 runtime'
    lib = runtime / 'usr/lib/x86_64-linux-gnu'
    lib.mkdir(parents=True)
    (lib / 'ld-linux-x86-64.so.2').touch()
    icd = runtime / 'usr/share/vulkan/icd.d/virtio_icd.json'
    icd.parent.mkdir(parents=True)
    icd.write_text('{}')
    game_dir = root / 'game directory with spaces'
    game_dir.mkdir()
    (game_dir / 'game with spaces.x86_64').touch()
    (game_dir / 'game.exe').touch()
    fake_fex = root / 'mock fex'
    fake_fex.write_text('#!' + sys.executable + '\n'
        'import os,sys,json\nfrom pathlib import Path\n'
        'Path(os.environ["CHECK_OUTPUT"]).write_text(json.dumps('
        '{"argv":sys.argv[1:],"cwd":os.getcwd(),"env":dict(os.environ)}))\n')
    fake_fex.chmod(0o755)
    fake_x11 = root / 'mock x11'
    fake_x11.write_text('#!' + sys.executable + '\n'
        'import os,sys,json\nfrom pathlib import Path\n'
        'assert sys.argv[1:3]==["-g","960x540"],sys.argv\n'
        'Path(os.environ["CHECK_X11_OUTPUT"]).write_text(json.dumps('
        '{"cwd":os.getcwd(),"env":dict(os.environ)}))\n'
        'os.environ["DISPLAY"]=":77"\n'
        'os.execv(sys.argv[3],sys.argv[3:])\n')
    fake_x11.chmod(0o755)
    poisoned = '''LD_PRELOAD LD_LIBRARY_PATH LD_AUDIT LD_DEBUG LD_DEBUG_OUTPUT
        LIBGL_ALWAYS_SOFTWARE LIBGL_ALWAYS_INDIRECT GALLIUM_DRIVER LP_NUM_THREADS
        MESA_LOADER_DRIVER_OVERRIDE LIBGL_DRIVERS_PATH __GLX_VENDOR_LIBRARY_NAME
        MESA_GL_VERSION_OVERRIDE MESA_GLES_VERSION_OVERRIDE MESA_GLSL_VERSION_OVERRIDE
        __EGL_VENDOR_LIBRARY_FILENAMES EGL_PLATFORM GDK_BACKEND QT_QPA_PLATFORM
        VK_ICD_FILENAMES VK_DRIVER_FILES VK_ADD_DRIVER_FILES VK_LAYER_PATH
        VK_ADD_LAYER_PATH VK_INSTANCE_LAYERS VK_LOADER_LAYERS_ENABLE
        VK_LOADER_LAYERS_DISABLE VK_LOADER_DRIVERS_SELECT VK_LOADER_DRIVERS_DISABLE
        SDL_VIDEODRIVER SDL_DYNAMIC_API SDL3_DYNAMIC_API STEAM_COMPAT_EMULATOR
        STEAM_COMPAT_GRAPHICS_PROVIDER STEAM_RUNTIME STEAM_RUNTIME_LIBRARY_PATH
        STEAM_RUNTIME_HEAVY'''.split()
    env = os.environ.copy()
    env.update({key: '/arm64/steam/poison' for key in poisoned})
    env.update(LINPAD_FEX_RUNTIME=str(runtime), LINPAD_FEX_BINARY=str(fake_fex),
               LINPAD_FEX_LIBRARY_OVERLAY='', LINPAD_FEX_ICD=str(icd),
               LINPAD_FEX_X11_RUNNER=str(fake_x11), CHECK_OUTPUT=str(root / 'fex.json'),
               CHECK_X11_OUTPUT=str(root / 'x11.json'), SteamAppId='367520',
               DISPLAY=':55', WAYLAND_DISPLAY='wrong-socket',
               XDG_RUNTIME_DIR='/wrong-runtime', XAUTHORITY='/steam/auth')
    argv = ['./game with spaces.x86_64', 'argument with spaces', '--example',
            'literal $and; characters']

    def invoke(args, test_env=env, expected=0):
        p = subprocess.run(args, cwd=game_dir, env=test_env, text=True, capture_output=True)
        assert p.returncode == expected, (p.returncode, p.stdout, p.stderr)

    def result():
        return json.loads((root / 'fex.json').read_text())

    invoke([str(tool / 'run'), 'waitforexitandrun', '--', *argv])
    got = result()
    x11 = json.loads((root / 'x11.json').read_text())
    assert got['argv'] == argv and got['cwd'] == str(game_dir), got
    assert x11['cwd'] == str(game_dir)
    assert got['env']['DISPLAY'] == ':77'
    assert got['env']['XDG_RUNTIME_DIR'] == '/tmp/xdg-runtime'
    assert got['env']['WAYLAND_DISPLAY'] == 'wayland-0-gpu'
    assert got['env']['SteamAppId'] == '367520'
    assert got['env']['LD_LIBRARY_PATH'] == str(lib) + ':' + str(lib / 'pulseaudio')
    assert got['env']['VK_ICD_FILENAMES'] == str(icd)
    assert got['env']['VK_LOADER_LAYERS_DISABLE'] == '*'
    assert got['env']['FEX_ROOTFS'] == str(runtime)
    assert got['env']['STEAM_RUNTIME'] == '0'
    assert got['env']['MESA_VK_WSI_DEBUG'] == 'sw'
    for key in poisoned:
        assert got['env'].get(key) != '/arm64/steam/poison', key
    for key in ('LD_PRELOAD', 'LD_LIBRARY_PATH', 'LIBGL_ALWAYS_SOFTWARE',
                'VK_ICD_FILENAMES', 'DISPLAY', 'XAUTHORITY'):
        assert key not in x11['env'], (key, x11['env'].get(key))
    assert x11['env']['VK_LOADER_LAYERS_DISABLE'] == '*'
    checks += [
        'PASS Steam waitforexitandrun argv preserved, including spaces and shell metacharacters',
        'PASS Steam/game working directory and SteamAppId preserved',
        'PASS fresh X11 display and GPU Wayland socket selected; inherited Xvnc removed',
        'PASS ARM64 libraries, software GL, overlays and loader selectors removed before X11',
        'PASS full x86 library/ICD paths added only for FEX; all Vulkan layers disabled',
    ]
    env_none = env.copy()
    env_none.update(LINPAD_FEX_DISPLAY_MODE='none', LINPAD_FEX_ROOTFS='')
    invoke([str(tool / 'run'), 'run', '--', *argv], env_none)
    got = result()
    assert got['argv'] == argv and got['env']['FEX_ROOTFS'] == ''
    assert all(key not in got['env'] for key in ('DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY'))
    checks.append('PASS CLI mode preserves argv and empty FEX_ROOTFS; display variables removed')
    env_force = env_none.copy()
    env_force['LINPAD_FEX_FORCE_VULKAN'] = '1'
    invoke([str(tool / 'launch-game'), *argv], env_force)
    assert result()['argv'] == argv + ['-force-vulkan']
    checks.append('PASS force-vulkan appended only when explicitly configured')
    env_overlay = env_none.copy()
    env_overlay.update(LINPAD_FEX_LIBRARY_OVERLAY='/chosen/mesa/lib',
                       LINPAD_FEX_EXTRA_LIBRARY_PATH='/chosen/game/lib')
    invoke([str(tool / 'launch-game'), *argv], env_overlay)
    assert result()['env']['LD_LIBRARY_PATH'] == (
        '/chosen/mesa/lib:' + str(lib) + ':' + str(lib / 'pulseaudio') + ':/chosen/game/lib')
    checks.append('PASS optional Mesa overlay precedes base runtime; extra game libraries follow it')
    env_default_overlay = env_none.copy()
    env_default_overlay.pop('LINPAD_FEX_LIBRARY_OVERLAY')
    invoke([str(tool / 'launch-game'), *argv], env_default_overlay)
    assert result()['env']['LD_LIBRARY_PATH'].startswith('/opt/hollow-mesa26/usr/lib/x86_64-linux-gnu:')
    checks.append('PASS Mesa26 overlay is the default; explicit empty/custom overlays still work')
    invoke([str(tool / 'run'), 'unknown', '--', *argv], expected=64)
    invoke([str(tool / 'launch-game'), './game.exe'], expected=64)
    checks.append('PASS invalid protocol verb and Windows executable rejected with status 64')

report = ('Local wrapper validation\n\n' + '\n'.join(checks) + '\n\n'
          'All executions used mock FEX and X11 executables in a temporary directory.\n'
          'No device, real GPU, Steam registration/depot selection or game was exercised.\n')
(base / 'VALIDATION.txt').write_text(report)
print(report)

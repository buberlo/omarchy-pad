#!/usr/bin/env python3
"""Create an unapplied one-file candidate and record its exact source inputs."""
import datetime
import difflib
import hashlib
import json
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
REPO = ROOT / 'work/LinPad'

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    source = REPO / 'jit/translate.c'
    text = source.read_text()
    anchor = '        if (sys == 0xd53b4200 || sys == 0xd53b4420 || sys == 0xd53b4400 ||'
    assert text.count(anchor) == 1
    snippet = (HERE / 'msr-fpcr-handler.inc').read_text()
    assert 'if (sys == 0xd51b4400)' not in text
    candidate = text.replace(anchor, snippet + anchor)
    diff = ''.join(difflib.unified_diff(text.splitlines(True), candidate.splitlines(True),
                                      fromfile='a/jit/translate.c', tofile='b/jit/translate.c'))
    (HERE / 'msr-fpcr-candidate.patch').write_text(diff)
    inputs = [
        'jit/translate.c', 'jit/emit.h', 'jit/jit_internal.h', 'jit/jit.c',
        'emu/arch/arm64/cpu.h',
        'asbestos/guest-arm64/gadgets-aarch64/math.S',
        'asbestos/guest-arm64/gadgets-aarch64/entry.S',
        'asbestos/guest-arm64/gadgets-aarch64/gadgets.h',
    ]
    fex = ROOT / 'work/steam/FEX-static/FEX'
    fex_input = fex / 'FEXCore/Source/Interface/Core/JIT/MiscOps.cpp'
    provenance = {
        'created_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'status': 'unapplied candidate; no production, CLI, or device change',
        'linpad_head': subprocess.check_output(['git', '-C', str(REPO), 'rev-parse', 'HEAD'], text=True).strip(),
        'inputs': {p: sha(REPO / p) for p in inputs},
        'fex_head': subprocess.check_output(['git', '-C', str(fex), 'rev-parse', 'HEAD'], text=True).strip(),
        'fex_rounding_source': {'path': str(fex_input.relative_to(ROOT)), 'sha256': sha(fex_input)},
        'candidate_sha256': sha(HERE / 'msr-fpcr-candidate.patch'),
        'handler_sha256': sha(HERE / 'msr-fpcr-handler.inc'),
        'activation_gate': 'Review 4451 sampled opcodes first; then full JIT integration, regressions, device qualification.',
    }
    (HERE / 'source-provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
    print('Prepared unapplied candidate; production source unchanged.')

if __name__ == '__main__':
    main()

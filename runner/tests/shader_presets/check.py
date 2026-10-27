"""Check real OpenGL preset output without a ROM. Pass the built presenter."""
from pathlib import Path
import hashlib
import os
import struct
import subprocess
import sys
import tempfile

exe = Path(sys.argv[1]).resolve()
presets = Path(__file__).resolve().parents[2] / 'assets/shaders'
with tempfile.TemporaryDirectory(prefix='shader-presets-') as scratch:
    outputs = {}
    for name in ('off', 'crt-soft', 'sharp', 'lcd-grid', 'warm-composite', 'missing'):
        run = Path(scratch) / name
        run.mkdir()
        shader = '' if name == 'off' else str(presets / (name + '.glslp'))
        result = subprocess.run([str(exe), shader], cwd=run, capture_output=True, timeout=20)
        assert result.returncode == 0, (name, result.stderr.decode(errors='replace'))
        assert b'error:' not in result.stdout.lower(), result.stdout.decode(errors='replace')
        shots = list((run / 'screenshots').glob('*.bmp'))
        assert len(shots) == 1, name
        image = shots[0].read_bytes()
        offset = struct.unpack_from('<I', image, 10)[0]
        pixels = image[offset:]
        assert sum(pixels) > 100000, f'{name}: empty or black output'
        outputs[name] = hashlib.sha256(pixels).hexdigest()
        print(f'{name}: rendered {len(pixels)} bytes, {outputs[name][:12]}')
    assert outputs['missing'] == outputs['off'], 'missing preset must fall back to unfiltered output'
    assert len({outputs[name] for name in outputs if name != 'missing'}) == 5, 'presets must change the output'

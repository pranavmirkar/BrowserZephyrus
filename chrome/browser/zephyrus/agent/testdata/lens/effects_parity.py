"""Parity at the moments the effects are visible (ripple, flash, beam, tears, tint).

For each (state, frames) pair: capture the prototype's frame in headless Chrome and
the native frame (LENS_DUMP_FRAMES), then compare as compare.py does.
"""
import os, subprocess, shutil
import numpy as np
from PIL import Image

HERE = os.environ.get('LENS_WORK', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'work'))
CHROME = os.environ.get('LENS_CHROME', r'C:\Program Files\Google\Chrome\Application\chrome.exe')
TEST = os.environ.get('LENS_TEST_EXE', r'D:\chromium\src\out\Release\zephyrus_agent_unittests.exe')
BG = (14, 14, 20)
PAIRS = [('clicking', 64), ('clicking', 68), ('done', 20), ('done', 30), ('surprise', 6), ('surprise', 20),
         ('crying', 150), ('laughing', 150), ('sadness', 220), ('error', 30), ('searching', 60), ('anger', 200)]

ref = os.path.join(HERE, 'ref_fx'); nat = os.path.join(HERE, 'native_fx')
for d in (ref, nat):
    shutil.rmtree(d, ignore_errors=True); os.makedirs(d)

for frames in sorted({f for _, f in PAIRS}):
    dump = os.path.join(HERE, f'dump_{frames}')
    shutil.rmtree(dump, ignore_errors=True); os.makedirs(dump)
    env = dict(os.environ, LENS_DUMP_DIR=dump, LENS_DUMP_FRAMES=str(frames))
    subprocess.run([TEST, '--gtest_filter=LensPainterTest.Dump*'], env=env,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
for state, frames in PAIRS:
    shutil.copy(os.path.join(HERE, f'dump_{frames}', state + '.png'), os.path.join(nat, f'{state}_{frames}.png'))
    out = os.path.join(ref, f'{state}_{frames}.png')
    url = f'file:///{os.path.join(HERE, "capture.html").replace(os.sep, "/")}?state={state}&fill=static&frames={frames}&seed=4242'
    subprocess.run([CHROME, '--headless=new', '--disable-gpu', '--hide-scrollbars', '--force-device-scale-factor=1',
                    '--window-size=622,632', f'--screenshot={out}', url],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)

print(f"{'frame':18s} {'mean':>6s} {'>16 %':>7s} {'>48 %':>7s} {'iou':>7s}")
for state, frames in PAIRS:
    name = f'{state}_{frames}.png'
    a = np.asarray(Image.open(os.path.join(ref, name)).convert('RGB'), dtype=np.int16)
    im = Image.open(os.path.join(nat, name)).convert('RGBA')
    base = Image.new('RGBA', im.size, BG + (255,)); base.alpha_composite(im)
    b = np.asarray(base.convert('RGB'), dtype=np.int16)
    mx = np.abs(a - b).max(axis=2)
    bg = np.array(BG, dtype=np.int16)
    ma = np.abs(a - bg).max(axis=2) > 30; mb = np.abs(b - bg).max(axis=2) > 30
    print(f'{name[:-4]:18s} {np.abs(a - b).mean():6.2f} {(mx > 16).mean() * 100:7.2f} {(mx > 48).mean() * 100:7.3f} '
          f'{(ma & mb).sum() / max(1, (ma | mb).sum()):7.4f}')

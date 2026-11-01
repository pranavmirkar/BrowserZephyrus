"""Compares the native render with the prototype's, state by state.

ref/<state>.png   the prototype, headless Chrome, the frame from capture.html
native/<state>.png  the C++ painter (LENS_DUMP_DIR), same seed, same frame

Both are put on the same background (#0e0e14). Reports, per state:
  mean   mean absolute difference per channel, 0-255
  >16    share of pixels whose largest channel differs by more than 16
  >48    share of pixels that differ clearly (more than 48)
  iou    overlap of the two silhouettes (pixels that are not background)
and writes side-by-side sheets (ref | native | diff x4) for review.
"""
import os, sys
import numpy as np
from PIL import Image

HERE = os.environ.get('LENS_WORK', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'work'))
BG = (14, 14, 20)
ref_dir = os.path.join(HERE, sys.argv[1] if len(sys.argv) > 1 else 'ref')
nat_dir = os.path.join(HERE, sys.argv[2] if len(sys.argv) > 2 else 'native')
sheet_dir = os.path.join(HERE, 'sheets')
os.makedirs(sheet_dir, exist_ok=True)


def load_native(path):
    im = Image.open(path).convert('RGBA')
    base = Image.new('RGBA', im.size, BG + (255,))
    base.alpha_composite(im)
    return base.convert('RGB')


rows = []
for name in sorted(os.listdir(ref_dir)):
    if not name.endswith('.png'):
        continue
    key = name[:-4]
    npath = os.path.join(nat_dir, name)
    if not os.path.exists(npath):
        continue
    ref = Image.open(os.path.join(ref_dir, name)).convert('RGB')
    nat = load_native(npath)
    if ref.size != nat.size:
        print(key, 'SIZE MISMATCH', ref.size, nat.size)
        continue
    a = np.asarray(ref, dtype=np.int16)
    b = np.asarray(nat, dtype=np.int16)
    d = np.abs(a - b)
    mx = d.max(axis=2)
    bg = np.array(BG, dtype=np.int16)
    ma = np.abs(a - bg).max(axis=2) > 30
    mb = np.abs(b - bg).max(axis=2) > 30
    iou = (ma & mb).sum() / max(1, (ma | mb).sum())
    rows.append((key, d.mean(), (mx > 16).mean() * 100, (mx > 48).mean() * 100, iou))
    sheet = Image.new('RGB', (ref.width * 3, ref.height))
    sheet.paste(ref, (0, 0))
    sheet.paste(nat, (ref.width, 0))
    amp = np.clip(mx * 4, 0, 255).astype(np.uint8)
    sheet.paste(Image.fromarray(amp).convert('RGB'), (ref.width * 2, 0))
    sheet.save(os.path.join(sheet_dir, key + '.png'))

print(f"{'state':12s} {'mean':>6s} {'>16 %':>7s} {'>48 %':>7s} {'iou':>7s}")
for key, mean, p16, p48, iou in sorted(rows, key=lambda r: -r[1]):
    print(f'{key:12s} {mean:6.2f} {p16:7.2f} {p48:7.3f} {iou:7.4f}')
if rows:
    print(f"{'AVERAGE':12s} {np.mean([r[1] for r in rows]):6.2f} {np.mean([r[2] for r in rows]):7.2f} "
          f"{np.mean([r[3] for r in rows]):7.3f} {np.mean([r[4] for r in rows]):7.4f}")

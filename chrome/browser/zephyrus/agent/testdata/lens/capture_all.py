"""Screenshots every prototype state from the deterministic capture page."""
import os, subprocess, sys
import os
HERE = os.path.dirname(os.path.abspath(__file__))
WORK = os.environ.get('LENS_WORK', os.path.join(HERE, 'work'))   # screenshots and sheets go here
os.makedirs(WORK, exist_ok=True)
CAPTURE = os.path.join(WORK, 'capture.html').replace(os.sep, '/')

CHROME = os.environ.get('LENS_CHROME', r'C:\Program Files\Google\Chrome\Application\chrome.exe')
OUT = os.path.join(WORK, 'ref')
STATES = ('idle working searching clicking done error joy sadness anger fear surprise disgust '
          'contempt love excited laughing pleading shy curious confused thinking suspicious '
          'determined smug mischievous wink bored sleepy crying anxious dizzy').split()
fill = sys.argv[1] if len(sys.argv) > 1 else 'static'
out = OUT if fill == 'static' else OUT + '_' + fill
os.makedirs(out, exist_ok=True)
for s in STATES:
    path = os.path.join(out, s + '.png')
    if os.path.exists(path):
        os.remove(path)
    url = f'file:///{CAPTURE}?state={s}&fill={fill}&frames=108&seed=4242'
    subprocess.run([CHROME, '--headless=new', '--disable-gpu', '--hide-scrollbars',
                    '--force-device-scale-factor=1', '--window-size=622,632',
                    f'--screenshot={path}', url],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
    size = os.path.getsize(path) if os.path.exists(path) else 0
    print(f'{s:12s} {size:8d} bytes' + ('  <-- suspiciously small' if size < 20000 else ''))

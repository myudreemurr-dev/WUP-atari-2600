#!/usr/bin/env python3
"""Build the Atari 2600 launcher and its player and pack them the way the GamePad's File Sharing wants them:
build/atari.zip holding apps/atari/atari.wup (the launcher) and apps/atari/player.wup. Unzip it onto the pad's root (FTP).

    git clone https://github.com/ravensclawx/wup-homebrew        # libwup, the GamePad SDK
    LIBWUP=../wup-homebrew/libwup python3 package.py              # needs clang, ld.lld, python3 + Pillow"""
import os, re, subprocess, sys, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
LIBWUP = os.environ.get('LIBWUP') or next((p for p in (os.path.join(HERE, '..', 'libwup'), os.path.join(HERE, '..', 'wup-homebrew', 'libwup')) if os.path.isdir(p)), None)
if not LIBWUP:
    sys.exit('libwup not found: set LIBWUP to wup-homebrew/libwup')
LIBWUP = os.path.abspath(LIBWUP)
ENV = dict(os.environ, LIBWUP=LIBWUP)


def built(d):
    r = subprocess.run([sys.executable, os.path.join(LIBWUP, 'tools', 'wupbuild.py'), d], capture_output=True, text=True, cwd=HERE, env=ENV)
    m = re.search(r'(\S+\.wup) \(', r.stdout)
    if r.returncode or not m:
        sys.exit('%s did not build:\n%s' % (d, r.stdout[-2000:] + r.stderr[-2000:]))
    print(r.stdout.strip().splitlines()[-1])
    return open(m.group(1), 'rb').read()


out = os.path.join(HERE, 'build', 'atari.zip')
os.makedirs(os.path.dirname(out), exist_ok=True)
files = [('apps/atari/atari.wup', built(HERE)), ('apps/atari/player.wup', built(os.path.join(HERE, 'player')))]
with zipfile.ZipFile(out, 'w', zipfile.ZIP_STORED) as z:
    for name, data in files:
        z.writestr(zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0)), data)
print(out, '(%d KB)' % (sum(len(d) for n, d in files) // 1024))

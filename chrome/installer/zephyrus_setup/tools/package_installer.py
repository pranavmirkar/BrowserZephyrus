#!/usr/bin/env python3
"""Builds the shippable Zephyrus-Setup.exe.

Takes the installer UI and Chromium's mini_installer.exe and produces a single
executable with the payload embedded as a resource.

WHY A PACKAGING STEP AND NOT A .rc ENTRY
----------------------------------------
Resource scripts name files at compile time. mini_installer.exe is a build
OUTPUT of the same build, so naming it in setup_resources.rc creates a
dependency the build cannot order: the UI would have to be compiled after a
target that is only produced later, and a stale payload would silently be baked
in whenever the ordering happened to work.

Injecting afterwards with UpdateResource sidesteps that entirely: the UI builds
standalone (and runs standalone with --demo), and packaging is a separate,
explicit step that always uses the mini_installer sitting next to it.

    python3 package_installer.py --out-dir out/Release \\
        --output D:/Zephyrus-Setup-151.0.0.0.exe
"""

import argparse
import ctypes
from ctypes import wintypes
import hashlib
import os
import shutil
import sys

# Must match setup_resource_ids.h.
IDR_PAYLOAD_MINI_INSTALLER = 300
RT_RCDATA = 10
# LANG_NEUTRAL, SUBLANG_NEUTRAL.
LANG_NEUTRAL = 0

kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)

kernel32.BeginUpdateResourceW.argtypes = [wintypes.LPCWSTR, wintypes.BOOL]
kernel32.BeginUpdateResourceW.restype = wintypes.HANDLE
kernel32.UpdateResourceW.argtypes = [
    wintypes.HANDLE, wintypes.LPCWSTR, wintypes.LPCWSTR, wintypes.WORD,
    wintypes.LPVOID, wintypes.DWORD
]
kernel32.UpdateResourceW.restype = wintypes.BOOL
kernel32.EndUpdateResourceW.argtypes = [wintypes.HANDLE, wintypes.BOOL]
kernel32.EndUpdateResourceW.restype = wintypes.BOOL


def sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as handle:
        for block in iter(lambda: handle.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest().upper()


def inject(target, payload_bytes):
    handle = kernel32.BeginUpdateResourceW(target, False)
    if not handle:
        raise SystemExit('BeginUpdateResource failed: %d'
                         % ctypes.get_last_error())
    ok = kernel32.UpdateResourceW(
        handle,
        wintypes.LPCWSTR(RT_RCDATA),
        wintypes.LPCWSTR(IDR_PAYLOAD_MINI_INSTALLER),
        LANG_NEUTRAL,
        payload_bytes,
        len(payload_bytes))
    if not ok:
        kernel32.EndUpdateResourceW(handle, True)  # discard
        raise SystemExit('UpdateResource failed: %d' % ctypes.get_last_error())
    if not kernel32.EndUpdateResourceW(handle, False):
        raise SystemExit('EndUpdateResource failed: %d'
                         % ctypes.get_last_error())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out-dir', required=True,
                        help='build output directory, e.g. out/Release')
    parser.add_argument('--output', required=True,
                        help='path of the setup .exe to produce')
    args = parser.parse_args()

    ui = os.path.join(args.out_dir, 'zephyrus_setup.exe')
    payload = os.path.join(args.out_dir, 'mini_installer.exe')

    for path, what in ((ui, 'installer UI'), (payload, 'mini_installer')):
        if not os.path.exists(path):
            raise SystemExit('missing %s: %s' % (what, path))

    # Refuse a payload older than the UI. It means one of the two was rebuilt
    # and the other was not, and shipping a mismatched pair is the kind of
    # error that only shows up on a user's machine.
    if os.path.getmtime(payload) < os.path.getmtime(ui) - 3600:
        print('WARNING: mini_installer.exe is much older than the installer UI.'
              '\n         Rebuild it, or you will ship a stale browser.')

    if os.path.exists(args.output):
        raise SystemExit(
            'refusing to overwrite %s\n'
            'Move the existing build aside first -- two setup files with the '
            'same version are told apart only by their hash, and silently '
            'replacing one makes that impossible.' % args.output)

    shutil.copy2(ui, args.output)
    with open(payload, 'rb') as handle:
        payload_bytes = handle.read()

    print('injecting %s (%.1f MB) into %s'
          % (os.path.basename(payload), len(payload_bytes) / (1 << 20),
             os.path.basename(args.output)))
    inject(args.output, payload_bytes)

    size = os.path.getsize(args.output)
    print('\nbuilt %s' % args.output)
    print('  size    : %d bytes (%.1f MB)' % (size, size / (1 << 20)))
    print('  SHA-256 : %s' % sha256(args.output))
    print('\nThe version string cannot tell two builds apart. Record the hash.')
    return 0


if __name__ == '__main__':
    sys.exit(main())

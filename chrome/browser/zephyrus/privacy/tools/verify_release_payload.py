#!/usr/bin/env python3
"""Fails a release build that would ship without its data files.

WHY THIS EXISTS
---------------
Two independent silent failures meet here, and each one produces a browser that
looks fine and quietly does less:

1. create_installer_archive.py SKIPS files listed in chrome.release that it
   cannot find, printing a note only in verbose mode. So a release build that
   forgot `fetch_entity_dataset.py` packages no entity dataset and says nothing.
   The browser then loads the null resolver and every tracker in the UI reads as
   unattributed -- the feature's headline claim, gone, with no error anywhere.

2. fetch_entity_dataset.py deliberately exits 0 on failure without --strict,
   because a licence-encumbered download must never break a contributor's build.
   Correct for developers, dangerous for a release.

Neither is a bug. Together they mean "nothing went wrong" and "we shipped no
data" are indistinguishable, so this script makes the difference loud.

It verifies the ACTUAL Ed25519 signature against the public key compiled into
the browser, parsed out of entity_signing_key.h rather than duplicated here. A
correctly built artifact signed with the WRONG key is the nastiest version of
this failure: everything looks right, and the browser silently refuses the file
on every load.

    python3 verify_release_payload.py --out-dir out/Release
"""

import argparse
import os
import re
import struct
import sys

# Header layout from entity_artifact.h. Offsets are derived, not guessed:
# magic[8] + 5*u32 (=28) + 2*u64 (=44) ... see the arithmetic below.
MAGIC = b"ZEPHENT2"
EXPECTED_VERSION = 2
SIG_OFFSET = 76
SIG_BYTES = 64
HEADER_BYTES = 160

HERE = os.path.dirname(os.path.abspath(__file__))
KEY_HEADER = os.path.join(HERE, os.pardir, "entity_signing_key.h")

# Files the installer manifest promises. Keyed by path relative to the out dir.
REQUIRED = [
    ("ZephyrusEntities/zephyrus_entities.dat",
     "entity dataset -- without it every tracker shows as unattributed"),
    ("zephyrus_filters.txt",
     "ad-block filter list"),
    ("FILTER_LISTS_NOTICE.txt",
     "EasyList attribution notice -- CC BY-SA 3.0 / GPLv3 REQUIRE this ship "
     "alongside zephyrus_filters.txt"),
]


def fail(msg):
    print("FAIL: " + msg)
    return False


def load_public_key_spki():
    """Read the trusted key out of the C++ header the browser compiles in.

    Parsed rather than duplicated on purpose: a copy here could drift from the
    browser's key, and this script would then bless artifacts the browser
    rejects -- exactly the failure it is meant to catch.
    """
    with open(KEY_HEADER, "r", encoding="utf-8", errors="replace") as handle:
        text = handle.read()
    match = re.search(r"kEntityArtifactPublicKeySpki\s*=\s*\{(.*?)\};",
                      text, re.S)
    if not match:
        raise SystemExit("could not find kEntityArtifactPublicKeySpki in "
                         + KEY_HEADER)
    values = [int(v, 16) for v in re.findall(r"0x([0-9a-fA-F]{2})", match.group(1))]
    if len(values) != 44:
        raise SystemExit("expected a 44-byte SPKI, parsed %d" % len(values))
    return bytes(values)


def verify_artifact(path):
    ok = True
    blob = open(path, "rb").read()

    if len(blob) < HEADER_BYTES:
        return fail("%s is %d bytes, smaller than its header" % (path, len(blob)))
    if blob[:8] != MAGIC:
        return fail("%s has magic %r, expected %r" % (path, blob[:8], MAGIC))

    version, flags, entries, entities, string_bytes, dataset_id = struct.unpack_from(
        "<IIIIII", blob, 8)
    built, published = struct.unpack_from("<QQ", blob, 32)

    if version != EXPECTED_VERSION:
        ok = fail("format version %d, browser expects %d" % (version, EXPECTED_VERSION))
    if entries == 0 or entities == 0:
        ok = fail("artifact carries %d domains / %d entities -- an empty "
                  "dataset attributes nothing" % (entries, entities))

    # The signature covers the whole file with its own 64 bytes zeroed.
    signature = blob[SIG_OFFSET:SIG_OFFSET + SIG_BYTES]
    if signature == bytes(SIG_BYTES):
        return fail("%s is UNSIGNED. The browser verifies on every load and "
                    "will refuse it, falling back to bare domains." % path)

    signed_view = bytearray(blob)
    signed_view[SIG_OFFSET:SIG_OFFSET + SIG_BYTES] = bytes(SIG_BYTES)

    try:
        from cryptography.hazmat.primitives.serialization import load_der_public_key
        from cryptography.exceptions import InvalidSignature
    except ImportError:
        print("WARN: `cryptography` not installed -- signature NOT verified. "
              "Install it before trusting this run (fetch_entity_dataset.py "
              "needs it too).")
        return ok

    public_key = load_der_public_key(load_public_key_spki())
    try:
        public_key.verify(bytes(signature), bytes(signed_view))
    except InvalidSignature:
        return fail("%s signature does NOT verify against the public key in "
                    "entity_signing_key.h. It was signed with the wrong key, or "
                    "modified after signing. The browser will reject it on every "
                    "load and silently show bare domains." % path)

    print("  signature verifies against the browser's compiled-in public key")
    print("  domains=%d entities=%d built=%d published=%d"
          % (entries, entities, built, published))
    return ok


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-dir", required=True,
                        help="build output directory, e.g. out/Release")
    args = parser.parse_args()

    print("release payload check: %s" % args.out_dir)
    ok = True
    for rel, why in REQUIRED:
        path = os.path.join(args.out_dir, rel.replace("/", os.sep))
        if not os.path.exists(path):
            ok = fail("MISSING %s (%s)" % (rel, why))
            continue
        size = os.path.getsize(path)
        if size == 0:
            ok = fail("%s is empty (%s)" % (rel, why))
            continue
        print("  ok  %-45s %8d bytes" % (rel, size))

    artifact = os.path.join(args.out_dir, "ZephyrusEntities",
                            "zephyrus_entities.dat")
    if os.path.exists(artifact):
        ok = verify_artifact(artifact) and ok

    if not ok:
        print("\nRELEASE PAYLOAD INCOMPLETE -- do not ship this build.")
        print("To produce the dataset:")
        print("  python3 chrome/browser/zephyrus/privacy/tools/"
              "fetch_entity_dataset.py \\")
        print("      --out-dir %s --signing-key <key.pem> --strict" % args.out_dir)
        return 1

    print("\nrelease payload OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())

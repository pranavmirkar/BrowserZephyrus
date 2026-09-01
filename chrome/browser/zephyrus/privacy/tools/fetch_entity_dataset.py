#!/usr/bin/env python3
# Copyright 2026 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Fetches a pinned entity dataset, then converts and signs it.

Spec 4.2: **neither the source dataset nor the built artifact may be committed.**
Both are CC BY-NC-SA works, and this repository is published publicly under the
project's own licence; committing either would put NC-licensed, ShareAlike-
encumbered data inside a tree distributed under a different licence. Deleting it
later does not help, because git history keeps it.

So the repo holds this script, the converter, the format spec and the licence
manifest -- all original work -- and the data is fetched at build time.

The dataset is pinned by URL **and by SHA-256**. An unpinned fetch would mean
the artifact silently changes whenever upstream does, which defeats both
reproducible builds and the "we know exactly what we shipped" property the
attribution metadata exists to provide.

Usage (release/CI):
  python3 fetch_entity_dataset.py --out-dir out/Release \\
      --signing-key "$ENTITY_SIGNING_KEY"

Exit code is 0 even when the dataset cannot be fetched, unless --strict is
passed. Spec 4.2: a developer build must not fail because a licensing-encumbered
download was unavailable; the browser starts with the null resolver and shows
bare domains. Release builds should pass --strict so a silent loss of
attribution cannot ship unnoticed.
"""

import argparse
import hashlib
import io
import json
import os
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
CONVERTER = os.path.join(HERE, "build_entity_artifact.py")

# The pin. Update all four fields together, and update the manifest row in
# chrome/browser/zephyrus/THIRD_PARTY_DATA.md in the same change.
#
# `published` is the upstream release date, NOT the date you ran this. The
# staleness guard (spec 4.4.1) measures from it, and using a build date would
# report a fresh build of two-year-old data as current.
PIN = {
    "dataset": "tracker-radar-domain-map",
    "version": "tracker-radar@6253f5a0",
    # A RAW FILE AT A PINNED COMMIT, not a release archive. Two reasons:
    #
    #  - The repository is ~12 GB. A build step can fetch one 10 MB file; it
    #    cannot clone the tree.
    #  - GitHub's auto-generated /archive/ tarballs are not guaranteed
    #    byte-stable, and GitHub has changed their compression before, which
    #    breaks a sha256 pin for reasons that have nothing to do with the data.
    #    raw.githubusercontent.com at an explicit commit is content-addressed
    #    and therefore genuinely stable.
    "url": ("https://raw.githubusercontent.com/duckduckgo/tracker-radar/"
            "6253f5a053513120c61ad8221dc30a0e2cdbfeb9/"
            "build-data/generated/domain_map.json"),
    "sha256": "e5fa4c4dfd7575304e19cebce70f8d40cdca1f4b057560e1ec1b708caaafdd08",
    # Commit date of 6253f5a0, NOT the date this was fetched. The staleness
    # guard measures from here (spec 4.4.1).
    "published": 1785176938,
    "licence": "CC BY-NC-SA 4.0",
    "source": "https://github.com/duckduckgo/tracker-radar",
    "format": "json-file",
}


def fail(strict, message):
    print("entity dataset: %s" % message, file=sys.stderr)
    if strict:
        sys.exit(1)
    print("entity dataset: continuing without attribution (spec 4.1); the "
          "browser will show bare domains", file=sys.stderr)
    sys.exit(0)


def fetch(url, expected_sha256, strict):
    # https ONLY. The URL comes from the built-in PIN or from --pin-file, so it
    # is developer-supplied rather than attacker-supplied — but urlopen also
    # speaks file:// and ftp://, and neither has any business here. A pin file
    # carrying file:///etc/passwd would otherwise be read and hashed rather than
    # rejected. Cheap to forbid, and it documents that this fetch is meant to
    # reach exactly one kind of place.
    #
    # The sha256 check below is still the real guarantee: it is what stops a
    # substituted URL from injecting content, whatever the scheme.
    if not url.lower().startswith("https://"):
        fail(strict, "refusing non-https dataset URL: %s" % url)
    print("fetching %s" % url)
    try:
        with urllib.request.urlopen(url, timeout=120) as response:
            blob = response.read()
    except Exception as err:  # noqa: BLE001 - any failure is the same outcome
        fail(strict, "download failed: %s" % err)
    actual = hashlib.sha256(blob).hexdigest()
    if actual != expected_sha256:
        # Never build from an unverified snapshot: the artifact is signed, and
        # signing unverified input just launders it.
        fail(strict,
             "sha256 mismatch\n  expected %s\n  actual   %s"
             % (expected_sha256, actual))
    return blob


def write_plain(blob, dest):
    """The pinned source is a single JSON file; hand it to the converter as-is."""
    path = os.path.join(dest, "domain_map.json")
    with open(path, "wb") as handle:
        handle.write(blob)
    return path


def extract(blob, member_prefix, dest):
    count = 0
    with zipfile.ZipFile(io.BytesIO(blob)) as archive:
        for member in archive.namelist():
            if member.endswith("/") or member_prefix not in member:
                continue
            if not member.endswith(".json"):
                continue
            # Flatten; never trust archive paths.
            name = os.path.basename(member)
            if not name or os.path.isabs(name) or ".." in name:
                continue
            with open(os.path.join(dest, name), "wb") as handle:
                handle.write(archive.read(member))
            count += 1
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-dir", required=True,
                        help="artifact is written to <out-dir>/ZephyrusEntities/")
    parser.add_argument("--signing-key",
                        help="PEM Ed25519 private key. Without it the artifact "
                             "is unsigned and the browser will reject it.")
    parser.add_argument("--strict", action="store_true",
                        help="fail the build instead of degrading to no "
                             "dataset; also requires --signing-key, because a "
                             "release must not ship an artifact the browser "
                             "will refuse to load")
    parser.add_argument("--pin-file",
                        help="JSON overriding the built-in pin")
    args = parser.parse_args()

    # Checked BEFORE the pin is read and long before the download, so a
    # missing dependency costs a second rather than a 38,000-domain fetch that
    # then throws it away.
    #
    # `cryptography` is not part of the depot_tools Python. That is the whole
    # reason this check exists as a named step with an install line, instead of
    # an ImportError surfacing from the converter after the network work.
    if args.signing_key:
        try:
            import cryptography  # noqa: F401
        except ImportError:
            sys.exit(
                "--signing-key needs the 'cryptography' package, which is not "
                "part of the depot_tools Python.\n"
                "  %s -m pip install cryptography" % sys.executable)

    # A signature is not optional for a release. Without one the browser
    # rejects the artifact on load, so an unsigned --strict build would "pass"
    # and then ship with no company attribution at all -- the failure this flag
    # exists to prevent, arriving as a single warning line in a build log.
    if args.strict and not args.signing_key:
        sys.exit("--strict requires --signing-key: an unsigned artifact is "
                 "rejected by the browser at load, so shipping one silently "
                 "disables entity attribution.")

    pin = dict(PIN)
    if args.pin_file:
        with open(args.pin_file, "r", encoding="utf-8") as handle:
            pin.update(json.load(handle))

    if not pin["url"] or not pin["sha256"]:
        fail(args.strict,
             "no dataset pinned. Set PIN in this script (url + sha256 + "
             "published + version) or pass --pin-file. Nothing is committed to "
             "the repo on purpose (spec 4.2).")

    out_dir = os.path.join(args.out_dir, "ZephyrusEntities")
    os.makedirs(out_dir, exist_ok=True)
    output = os.path.join(out_dir, "zephyrus_entities.dat")

    blob = fetch(pin["url"], pin["sha256"], args.strict)
    with tempfile.TemporaryDirectory() as work:
        if pin.get("format") == "json-file":
            converter_input = write_plain(blob, work)
        else:
            found = extract(blob, pin.get("member_prefix", ""), work)
            if not found:
                fail(args.strict,
                     "archive contained no %s*.json" % pin.get("member_prefix"))
            print("extracted %d domain files" % found)
            converter_input = work

        cmd = [sys.executable, CONVERTER,
               "--dataset", pin["dataset"],
               "--input", converter_input,
               "--output", output,
               "--dataset-version", pin["version"],
               "--dataset-source", pin["source"],
               "--dataset-licence", pin["licence"],
               "--dataset-published", str(pin["published"])]
        if args.signing_key:
            cmd += ["--signing-key", args.signing_key]
        else:
            print("warning: no --signing-key; the artifact will be UNSIGNED "
                  "and the browser will reject it", file=sys.stderr)
        subprocess.check_call(cmd)

    print("entity dataset ready: %s" % output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
